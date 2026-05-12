// src/engine/ddl_executor.cpp
#include "../../include/engine/ddl_executor.h"
#include "../../include/storage/file_manager.h"
#include <iostream>
#include <cstring>
#include <ctime>

bool DDLExecutor::createTable(const std::string& tableName, const std::vector<ColumnDef>& fields) {
    std::string tbFile = tableName + ".tb";
    std::string tdfFile = tableName + ".tdf";
    std::string trdFile = tableName + ".trd";

    // 1. 检查表是否已存在
    if (FileManager::fileExists(tbFile)) {
        std::cerr << "[Error] Table already exists: " << tableName << std::endl;
        return false;
    }

    // 2. 遍历字段，计算每条记录的总长度 (recordSize) 和每个字段的内部偏移量 (offset)
    uint32_t currentOffset = 0;
    std::vector<ColumnDef> alignedFields = fields;

    for (auto& field : alignedFields) {
        field.offset = currentOffset;
        currentOffset += field.length;

        // 核心约束：保证每一个字段在文件中的长度都是 4 的倍数，以便对齐
        if (currentOffset % 4 != 0) {
            currentOffset += (4 - (currentOffset % 4));
        }
    }
    uint32_t totalRecordSize = currentOffset;

    // 3. 构造并生成 表描述文件 (.tb)
    TableHeader header;
    std::memset(&header, 0, sizeof(TableHeader));
    std::strncpy(header.tableName, tableName.c_str(), MAX_NAME_LEN - 1);
    header.recordCount = 0;
    header.fieldCount = static_cast<uint32_t>(alignedFields.size());
    header.createTime = static_cast<uint32_t>(std::time(nullptr));
    header.modifyTime = header.createTime;
    header.recordSize = totalRecordSize;

    FileManager::createFile(tbFile);
    FileManager::writeStruct(tbFile, header, 0);

    // 4. 构造并生成 表定义文件 (.tdf)
    FileManager::createFile(tdfFile);
    uint32_t tdfOffset = 0;
    for (const auto& field : alignedFields) {
        FileManager::writeStruct(tdfFile, field, tdfOffset);
        tdfOffset += sizeof(ColumnDef);
    }

    // 5. 生成空白的数据记录文件 (.trd)
    FileManager::createFile(trdFile);

    std::cout << "[Success] Table created: '" << tableName
              << "' | Fields: " << header.fieldCount
              << " | Record Size: " << totalRecordSize << " bytes." << std::endl;
    return true;
}

bool DDLExecutor::dropTable(const std::string& tableName) {
    FileManager::deleteFile(tableName + ".tb");
    FileManager::deleteFile(tableName + ".tdf");
    FileManager::deleteFile(tableName + ".trd");
    return true;
}


// ---------------- 新增的高层封装接口 (接收 ASTNode 返回 ExecuteResult) ----------------

ExecuteResult DDLExecutor::executeCreateTable(const ASTNode* ast) {
    ExecuteResult res;
    // 解析 ast 中的 columns，目前为了打通链路写固定假字段
    std::vector<ColumnDef> dummyFields;
    // ... 未来通过 ast->columns 转换 ...
    
    // 调用底层的建表逻辑
    if (createTable(ast->tbl, dummyFields)) {
        res.msg = "Query OK: Table '" + ast->tbl + "' created successfully.";
    } else {
        res.error = 1;
        res.msg = "Error: Failed to create table '" + ast->tbl + "'.";
    }
    return res;
}

ExecuteResult DDLExecutor::executeDropTable(const ASTNode* ast) {
    ExecuteResult res;
    if (dropTable(ast->tbl)) {
        res.msg = "Query OK: Table '" + ast->tbl + "' dropped successfully.";
    } else {
        res.error = 1;
        res.msg = "Error: Failed to drop table '" + ast->tbl + "'.";
    }
    return res;
}

ExecuteResult DDLExecutor::createDatabase(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Database '" + ast->db + "' created.";
    return res;
}

ExecuteResult DDLExecutor::dropDatabase(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Database '" + ast->db + "' dropped.";
    return res;
}

ExecuteResult DDLExecutor::useDatabase(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Database changed to '" + ast->db + "'.";
    return res;
}

ExecuteResult DDLExecutor::showTables() {
    ExecuteResult res;
    res.headers = {"Tables_in_db"};
    res.rows.push_back({"Users"}); // 假数据 Stub
    res.msg = "Query OK: 1 row in set";
    return res;
}
