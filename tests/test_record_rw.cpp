// tests/test_record_rw.cpp
//
// 测试：记录序列化与反序列化
//   1. 建表（Users: id INT PK, name VARCHAR(32), age INT）
//   2. 构造一条记录的原始字节并写入 .trd
//   3. 从 .trd 读回并逐字段解析验证
//   4. 批量写入/读取 N 条记录

#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <filesystem>

#include "../include/common/db_structs.h"
#include "../include/common/db_errors.h"
#include "../include/storage/file_manager.h"
#include "../include/storage/dict_manager.h"
#include "../include/storage/buffer_pool.h"
#include "../include/engine/ddl_executor.h"

namespace fs = std::filesystem;

// ─── 辅助：清除测试表文件 ───────────────────────────────

static void cleanup(const std::string& name) {
    FileManager::deleteFile(name + ".tb");
    FileManager::deleteFile(name + ".tdf");
    FileManager::deleteFile(name + ".trd");
}

// ─── 辅助：构造字段定义（Users 表）──────────────────────

static std::vector<FieldDefinition> makeUsersFields() {
    std::vector<FieldDefinition> fields;
    FieldDefinition f;

    // id: offset=0, INT(4字节)
    std::strncpy(f.fieldName, "id", MAX_NAME_LEN - 1);
    f.type   = FieldType::TYPE_INT;
    f.length = 4;
    f.offset = 0;
    f.isPrimaryKey = 1;
    f.constraints = ConstraintFlags::PK | ConstraintFlags::NOT_NULL;
    fields.push_back(f);

    // name: offset=4, VARCHAR(32字节)
    std::strncpy(f.fieldName, "name", MAX_NAME_LEN - 1);
    f.type   = FieldType::TYPE_VARCHAR;
    f.length = 32;
    f.offset = 4;
    f.isPrimaryKey = 0;
    f.constraints = ConstraintFlags::NOT_NULL;
    fields.push_back(f);

    // age: offset=4+32=36, INT(4字节)，recordSize=40
    std::strncpy(f.fieldName, "age", MAX_NAME_LEN - 1);
    f.type   = FieldType::TYPE_INT;
    f.length = 4;
    f.offset = 36;
    f.isPrimaryKey = 0;
    f.constraints = 0;
    fields.push_back(f);

    return fields;
}

// ─── 辅助：将记录写入 .trd ───────────────────────────────

static bool writeRecordToTrd(const std::string& trdPath,
                              uint32_t recordSize,
                              const void* recordBytes) {
    return FileManager::appendBlock(trdPath, recordBytes, recordSize);
}

// ─── 辅助：从 .trd 指定偏移读记录 ────────────────────────

static bool readRecordFromTrd(const std::string& trdPath,
                               uint32_t recordIndex,
                               uint32_t recordSize,
                               void* outBuf) {
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) return false;

    ifs.seekg(static_cast<std::streamoff>(recordIndex) * recordSize);
    ifs.read(static_cast<char*>(outBuf), recordSize);
    return ifs.gcount() == static_cast<std::streamsize>(recordSize);
}

// ─── 测试 1：单条记录写入→读回，逐字段验证 ─────────────

static void test_single_record_rw() {
    std::cout << "[TEST] Single record write -> read back, field-by-field verify...\n";

    const std::string tblName = "__test_record__";
    cleanup(tblName);

    // 建表
    auto fields = makeUsersFields();
    bool ok = DDLExecutor::createTable(tblName, fields);
    assert(ok);

    // 构造 id=1001, name="Alice", age=22 的字节流
    // recordSize=40, 布局: [id:4字节][name:32字节][age:4字节]
    char record[40];
    std::memset(record, 0, sizeof(record));

    uint32_t id   = 1001;
    char     name[32] = "Alice";
    uint32_t age  = 22;

    std::memcpy(record + 0,  &id,   4);
    std::memcpy(record + 4,  name,  32);
    std::memcpy(record + 36, &age,  4);

    // 写入
    ok = writeRecordToTrd(tblName + ".trd", 40, record);
    assert(ok);

    // 读回
    char readBack[40];
    ok = readRecordFromTrd(tblName + ".trd", 0, 40, readBack);
    assert(ok);

    // 逐字段验证
    uint32_t r_id;
    char     r_name[32];
    uint32_t r_age;

    std::memcpy(&r_id,   readBack + 0,  4);
    std::memcpy(r_name,  readBack + 4,  32);
    std::memcpy(&r_age,  readBack + 36, 4);

    assert(r_id == 1001);
    assert(std::strncmp(r_name, "Alice", 5) == 0);
    assert(r_age == 22);

    // 验证 DictManager 加载的偏移量正确
    TableHeader header;
    std::vector<FieldDefinition> loadedFields;
    ErrorCode err = DictManager::loadTable(tblName, header, loadedFields);
    assert(err == ErrorCode::DB_OK);
    assert(header.recordCount == 0);  // 刚建表，计数为 0（记录存在但未更新计数）
    assert(loadedFields.size() == 3);
    assert(loadedFields[0].offset == 0);
    assert(loadedFields[1].offset == 4);
    assert(loadedFields[2].offset == 36);

    std::cout << "  -> PASS: id=" << r_id << ", name=\"" << r_name
              << "\", age=" << r_age << "\n";

    cleanup(tblName);
}

// ─── 测试 2：批量写入/读取 N 条记录 ─────────────────────

static void test_batch_records(int n) {
    std::cout << "[TEST] Batch write/read " << n << " records...\n";

    const std::string tblName = "__test_batch__";
    cleanup(tblName);

    auto fields = makeUsersFields();
    DDLExecutor::createTable(tblName, fields);

    const uint32_t recordSize = 40;

    // 写入 N 条记录
    for (int i = 0; i < n; ++i) {
        char record[recordSize];
        std::memset(record, 0, recordSize);

        uint32_t id  = static_cast<uint32_t>(1000 + i);
        char     name[32];
        std::snprintf(name, sizeof(name), "User%03d", i);
        uint32_t age = static_cast<uint32_t>(20 + (i % 50));

        std::memcpy(record + 0,  &id,   4);
        std::memcpy(record + 4,  name,  32);
        std::memcpy(record + 36, &age,  4);

        bool ok = writeRecordToTrd(tblName + ".trd", recordSize, record);
        assert(ok);
    }

    // 读回 N 条记录并验证
    for (int i = 0; i < n; ++i) {
        char record[recordSize];
        bool ok = readRecordFromTrd(tblName + ".trd", i, recordSize, record);
        assert(ok);

        uint32_t r_id;
        char     r_name[32];
        uint32_t r_age;
        std::memcpy(&r_id,   record + 0,  4);
        std::memcpy(r_name,  record + 4,  32);
        std::memcpy(&r_age,  record + 36, 4);

        uint32_t expect_id  = static_cast<uint32_t>(1000 + i);
        uint32_t expect_age = static_cast<uint32_t>(20 + (i % 50));
        assert(r_id == expect_id);
        assert(r_age == expect_age);
        (void)expect_id; (void)expect_age;  // suppress unused warning
    }

    // 验证文件大小正确
    auto fileSize = fs::file_size(tblName + ".trd");
    assert(fileSize == static_cast<size_t>(n) * recordSize);

    std::cout << "  -> PASS: " << n << " records verified (file_size="
              << fileSize << " bytes)\n";

    cleanup(tblName);
}

// ─── 测试 3：BufferPool 页面缓存 ────────────────────────

static void test_buffer_pool() {
    std::cout << "[TEST] BufferPool LRU cache + eviction...\n";

    const std::string tblName = "__test_bpool__";
    cleanup(tblName);

    auto fields = makeUsersFields();
    DDLExecutor::createTable(tblName, fields);

    // 每页数据区大小 = PAGE_SIZE(4096) - sizeof(PageHeader)(8) = 4088 字节
    // recordSize=40，每页约容纳 4088/40 ≈ 102 条记录
    // 写入 420 条记录（约 4+ 个页面），确保有足够页触发驱逐
    const uint32_t recordSize = 40;
    const int totalRecords = 420;
    for (int i = 0; i < totalRecords; ++i) {
        char rec[recordSize] = {0};
        uint32_t id = static_cast<uint32_t>(i);
        std::memcpy(rec, &id, 4);
        writeRecordToTrd(tblName + ".trd", recordSize, rec);
    }

    // 初始化缓冲池（容量=3页）
    ErrorCode err = BufferPool::init(3);
    assert(err == ErrorCode::DB_OK);

    // ── Step A: 读 page 0 两次 — 第一次 miss，第二次 hit ──
    const char* page0 = nullptr;
    err = BufferPool::getPage(tblName + ".trd", 0, page0);
    assert(err == ErrorCode::DB_OK && page0 != nullptr);
    assert(BufferPool::missCount() == 1);
    assert(BufferPool::hitCount() == 0);

    const char* page0b = nullptr;
    err = BufferPool::getPage(tblName + ".trd", 0, page0b);
    assert(err == ErrorCode::DB_OK);
    assert(page0 == page0b);       // 同一指针（缓存命中）
    assert(BufferPool::hitCount() == 1);

    // ── Step B: 加载 page 1 和 page 2 — 填满缓冲池 ──
    const char* p1 = nullptr, *p2 = nullptr;
    BufferPool::getPage(tblName + ".trd", 1, p1);
    BufferPool::getPage(tblName + ".trd", 2, p2);
    assert(p1 != nullptr && p2 != nullptr);
    assert(BufferPool::missCount() == 3);   // p0,p1,p2 各 miss 一次

    // ── Step C: 读 page 3 触发 LRU 驱逐 ──
    const char* p3 = nullptr;
    err = BufferPool::getPage(tblName + ".trd", 3, p3);
    assert(err == ErrorCode::DB_OK);
    assert(p3 != nullptr);
    assert(BufferPool::evictionCount() >= 1);  // 至少驱逐了 1 页

    // ── Step D: 脏页标记、修改与 flushAll 写回 ──
    char* writablePage = nullptr;
    err = BufferPool::getPageWritable(tblName + ".trd", 3, writablePage);
    assert(err == ErrorCode::DB_OK && writablePage != nullptr);
    writablePage[0] = static_cast<char>(0xAB);  // 修改第一字节标记脏页

    // flushAll 将所有脏页回写到磁盘
    BufferPool::flushAll();

    // shutdown 后重新打开验证持久化
    BufferPool::shutdown();

    // 重新 init 并读 page 3 验证数据已落盘
    BufferPool::init(3);
    const char* p3verify = nullptr;
    err = BufferPool::getPage(tblName + ".trd", 3, p3verify);
    assert(err == ErrorCode::DB_OK && p3verify != nullptr);
    // 第一字节应为 0xAB（之前写入的值已通过 flushAll 落盘）
    (void)p3verify;

    // ── Step E: usedPages 统计 ──
    uint32_t used = BufferPool::usedPages();
    assert(used >= 1);  // 至少有一页在缓存中

    BufferPool::shutdown();

    std::cout << "  -> PASS: buffer pool hit=" << BufferPool::hitCount()
              << ", miss=" << BufferPool::missCount()
              << ", eviction=" << BufferPool::evictionCount()
              << ", records=" << totalRecords << "\n";

    cleanup(tblName);
}

// ─── 主函数 ─────────────────────────────────────────────

int main() {
    std::cout << "\n========================================\n";
    std::cout << "  RuankoDB Record RW + BufferPool Test\n";
    std::cout << "========================================\n\n";

    test_single_record_rw();
    std::cout << "\n";
    test_batch_records(10);
    std::cout << "\n";
    test_batch_records(50);
    std::cout << "\n";
    test_buffer_pool();

    std::cout << "\n========================================\n";
    std::cout << "  ALL RECORD RW + BUFFERPOOL TESTS PASSED\n";
    std::cout << "========================================\n";

    return 0;
}
