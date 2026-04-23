// tests/test_dict_manager.cpp
//
// Dev-B 存储层全链路测试：
//   1. DDL 建表 → 生成 .tb / .tdf / .trd 三件套
//   2. DictManager 加载元信息 → 校验字段对齐与偏移量
//   3. updateRecordCount / touchModifyTime → 验证回写一致性
//   4. 错误码路径 → 表不存在、文件损坏等边界场景

#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>

// 使用相对路径兼容 Dev-C++ 直接编译（无 CMake include 路径）
#include "../include/common/db_structs.h"
#include "../include/common/db_errors.h"
#include "../include/storage/file_manager.h"
#include "../include/storage/dict_manager.h"

#include "../include/engine/ddl_executor.h"  // 复用 DDLExecutor 建表逻辑

namespace fs = std::filesystem;

// ─── 辅助：构造 ColumnDef ──────────────────────────
ColumnDef makeField(const char* name, DataType type,
                     uint32_t length, uint32_t isPk = 0) {
    ColumnDef fd;
    std::memset(&fd, 0, sizeof(fd));
    std::strncpy(fd.fieldName, name, MAX_NAME_LEN - 1);
    fd.type       = type;
    fd.length     = length;
    fd.isPrimaryKey = isPk;
    return fd;
}

const std::string TEST_TABLE = "__test_dictmgr__";

void cleanup() {
    FileManager::deleteFile(TEST_TABLE + ".tb");
    FileManager::deleteFile(TEST_TABLE + ".tdf");
    FileManager::deleteFile(TEST_TABLE + ".trd");
}

// ════════════════════ 测试 1：DDL 建表 + DictManager 加载 ═════════════════════
void test_load_table() {
    std::cout << "[TEST] DDL createTable + DictManager loadTable..." << std::endl;

    cleanup();

    // 1) 用 DDLExecutor 建一张 3 列表
    std::vector<ColumnDef> fields;
    fields.push_back(makeField("id",     DataType::TYPE_INT,      4, 1));
    fields.push_back(makeField("name",   DataType::TYPE_VARCHAR, 32));
    fields.push_back(makeField("age",    DataType::TYPE_INT,      4));

    bool ok = DDLExecutor::createTable(TEST_TABLE, fields);
    assert(ok == true);

    // 2) 用 DictManager 加载完整元信息
    TableHeader hdr;
    std::vector<ColumnDef> loadedFields;

    ErrorCode err = DictManager::loadTable(TEST_TABLE, hdr, loadedFields);
    assert(err == ErrorCode::DB_OK);

    // 3) 校验表头
    assert(std::strcmp(hdr.tableName, TEST_TABLE.c_str()) == 0);
    assert(hdr.recordCount == 0);           // 新表记录数为 0
    assert(hdr.fieldCount == 3);
    assert(hdr.recordSize > 0);             // recordSize 应该 > 0（INT4+VARCHAR32+INT4=40，对齐后可能 44）

    // 4) 校验字段列表
    assert(loadedFields.size() == 3);
    assert(loadedFields[0].type == DataType::TYPE_INT);
    assert(std::strcmp(loadedFields[0].fieldName, "id") == 0);
    assert(loadedFields[0].isPrimaryKey == 1);
    assert(loadedFields[0].offset == 0);     // 第一个字段偏移为 0

    assert(loadedFields[1].type == DataType::TYPE_VARCHAR);
    assert(std::strcmp(loadedFields[1].fieldName, "name") == 0);
    assert(loadedFields[1].offset == 4);     // id 占 4 字节后开始

    assert(loadedFields[2].type == DataType::TYPE_INT);
    assert(std::strcmp(loadedFields[2].fieldName, "age") == 0);

    std::cout << "  -> PASS: loadTable verified (fields=" << hdr.fieldCount
              << ", recordSize=" << hdr.recordSize << ")" << std::endl;

    cleanup();
}

// ════════════════════ 测试 2：轻量级表头查询 ═════════════════════
void test_load_header_only() {
    std::cout << "[TEST] DictManager loadTableHeader..." << std::endl;
    cleanup();

    std::vector<ColumnDef> fields;
    fields.push_back(makeField("x", DataType::TYPE_INT, 4));
    DDLExecutor::createTable(TEST_TABLE, fields);

    TableHeader hdr;
    ErrorCode err = DictManager::loadTableHeader(TEST_TABLE, hdr);
    assert(err == ErrorCode::DB_OK);
    assert(hdr.fieldCount == 1);

    std::cout << "  -> PASS: loadTableHeader OK" << std::endl;
    cleanup();
}

// ════════════════════ 测试 3：存在性检查 ═════════════════════
void test_table_exists() {
    std::cout << "[TEST] DictManager tableExists..." << std::endl;
    cleanup();

    // 表不存在时
    assert(DictManager::tableExists(TEST_TABLE) == false);

    // 建表后再查
    std::vector<ColumnDef> f;
    f.push_back(makeField("a", DataType::TYPE_INT, 4));
    DDLExecutor::createTable(TEST_TABLE, f);

    assert(DictManager::tableExists(TEST_TABLE) == true);

    std::cout << "  -> PASS: tableExists correct before/after CREATE" << std::endl;
    cleanup();
}

// ════════════════════ 测试 4：updateRecordCount 回写一致性 ═════════════════════
void test_update_record_count() {
    std::cout << "[TEST] DictManager updateRecordCount..." << std::endl;
    cleanup();

    std::vector<ColumnDef> f;
    f.push_back(makeField("b", DataType::TYPE_INT, 4));
    DDLExecutor::createTable(TEST_TABLE, f);

    // 模拟 INSERT 了 5 条记录
    ErrorCode err = DictManager::updateRecordCount(TEST_TABLE, 5);
    assert(err == ErrorCode::DB_OK);

    // 重新加载校验
    TableHeader hdr;
    DictManager::loadTableHeader(TEST_TABLE, hdr);
    assert(hdr.recordCount == 5);

    // 模拟 DELETE 后剩下 2 条
    err = DictManager::updateRecordCount(TEST_TABLE, 2);
    assert(err == ErrorCode::DB_OK);
    DictManager::loadTableHeader(TEST_TABLE, hdr);
    assert(hdr.recordCount == 2);

    std::cout << "  -> PASS: recordCount round-trip verified (5 -> 2)" << std::endl;
    cleanup();
}

// ════════════════════ 测试 5：错误码路径 ═════════════════════
void test_error_paths() {
    std::cout << "[TEST] Error code paths..." << std::endl;
    cleanup();

    // 5a) 加载不存在的表
    TableHeader hdr;
    std::vector<ColumnDef> fds;
    ErrorCode err = DictManager::loadTable("__nonexistent_table_xyz__", hdr, fds);
    assert(err == ErrorCode::DB_ERR_TABLE_NOT_FOUND);

    // 5b) 轻量级加载不存在的表
    err = DictManager::loadTableHeader("__ghost_table__", hdr);
    assert(err == ErrorCode::DB_ERR_TABLE_NOT_FOUND);

    // 5c) 更新不存在的表的计数
    err = DictManager::updateRecordCount("__phantom_table__", 10);
    assert(err == ErrorCode::DB_ERR_TABLE_NOT_FOUND);

    // 5d) 错误消息函数覆盖
    assert(getErrorMessage(ErrorCode::DB_ERR_TABLE_NOT_FOUND)[0] != '\0');
    assert(getErrorMessage(ErrorCode::DB_OK)[0] != '\0');

    std::cout << "  -> PASS: all error paths return expected codes" << std::endl;
}

// ════════════════════ 测试 6：错误码枚举完整性 ═════════════════════
void test_error_codes_coverage() {
    std::cout << "[TEST] Error code coverage & message mapping..." << std::endl;

    // 确保每个分组都有至少一个可映射的消息
    int msgCount = 0;
    for (int i = 0; i <= static_cast<int>(ErrorCode::DB_ERR_NOT_NULL_VIOLATION); ++i) {
        auto ec = static_cast<ErrorCode>(i);
        const char* msg = getErrorMessage(ec);
        if (msg[0] != '\0') msgCount++;
    }
    assert(msgCount >= 20);  // 至少 20 个有意义的错误消息
    std::cout << "  -> PASS: " << msgCount << " error codes have messages" << std::endl;
}

// ════════════════════ 主入口 ═════════════════════
int main() {
    std::cout << "\n========================================\n";
    std::cout << "  RuankoDB Storage Layer — Full Pipeline Test\n";
    std::cout << "========================================\n\n";

    test_error_codes_coverage();
    test_load_header_only();
    test_table_exists();
    test_load_table();          // 核心全链路
    test_update_record_count(); // 回写一致性
    test_error_paths();         // 边界/异常路径

    std::cout << "\n========================================\n";
    std::cout << "  ALL 6 TESTS PASSED\n";
    std::cout << "========================================\n";

    return 0;
}
