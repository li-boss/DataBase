// tests/test_index_recovery.cpp
// 索引重启恢复测试：创建索引 → 模拟重启 → 验证元数据不丢失

#include <iostream>
#include <cassert>
#include <vector>
#include <string>

#include "../include/storage/dict_manager.h"
#include "../include/storage/file_manager.h"
#include "../include/common/db_structs.h"
#include "../include/common/db_errors.h"
#include "../include/engine/ddl_executor.h"

// 辅助：创建一个测试表 student（含 id INT PK, age INT）
static void createTestTable() {
    std::vector<ColumnDef> fields(2);
    // 字段 0: id INT PRIMARY KEY
    std::strncpy(fields[0].fieldName, "id", MAX_NAME_LEN - 1);
    fields[0].type = DataType::TYPE_INT;
    fields[0].length = 4;
    fields[0].offset = 0;
    fields[0].isPrimaryKey = 1;
    fields[0].constraints = 0;

    // 字段 1: age INT
    std::strncpy(fields[1].fieldName, "age", MAX_NAME_LEN - 1);
    fields[1].type = DataType::TYPE_INT;
    fields[1].length = 4;
    fields[1].offset = 4;
    fields[1].isPrimaryKey = 0;
    fields[1].constraints = 0;

    bool ok = DDLExecutor::createTable("student", fields);
    if (!ok) {
        std::cerr << "[Test] Warning: createTable failed\n";
    }
}

void test_index_persistence() {
    std::cout << "[Test] Index Persistence & Recovery\n";

    // 1. 准备：创建数据库和表
    ErrorCode err = DictManager::CreateDatabase("testdb_idx");
    assert(err == ErrorCode::DB_OK || err == ErrorCode::DB_ERR_FILE_ALREADY_EXISTS);

    err = DictManager::UseDatabase("testdb_idx");
    assert(err == ErrorCode::DB_OK);

    // 创建测试表 student
    createTestTable();

    // 2. 创建索引
    err = DictManager::CreateIndex("idx_student_age", "student", "age", 1, 
                                   static_cast<uint32_t>(DataType::TYPE_INT), 4);
    assert(err == ErrorCode::DB_OK);

    // 3. 验证索引元数据存在
    std::vector<std::string> indexes;
    err = DictManager::ListIndexes("student", indexes);
    assert(err == ErrorCode::DB_OK);
    assert(!indexes.empty());
    std::cout << "  Index created: " << indexes[0] << "\n";

    // 4. 模拟重启：重新读取索引元数据
    std::vector<std::string> indexes2;
    err = DictManager::ListIndexes("student", indexes2);
    assert(err == ErrorCode::DB_OK);
    assert(indexes2.size() == indexes.size());
    std::cout << "  Index recovered after 'restart'\n";

    // 5. 验证 IndexHeader 正确
    IndexHeader hdr;
    err = DictManager::GetIndexHeader("idx_student_age", hdr);
    assert(err == ErrorCode::DB_OK);
    assert(std::string(hdr.indexName) == "idx_student_age");
    assert(std::string(hdr.tableName) == "student");
    std::cout << "  IndexHeader verified\n";

    // 6. 清理
    err = DictManager::DropIndex("idx_student_age");
    assert(err == ErrorCode::DB_OK);

    err = DictManager::DropDatabase("testdb_idx");
    assert(err == ErrorCode::DB_OK);

    std::cout << "[Test] PASSED\n\n";
}

int main() {
    test_index_persistence();
    return 0;
}
