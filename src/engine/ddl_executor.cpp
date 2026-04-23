// src/engine/ddl_executor.cpp
#include "engine/ddl_executor.h"
#include "storage/file_manager.h"
#include "storage/dict_manager.h"
#include "common/db_errors.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <ctime>

bool DDLExecutor::createTable(const std::string& tableName, const std::vector<ColumnDef>& fields) {
    std::string dbDir = DictManager::GetCurrentDB();
    std::string tbFile = dbDir + "/" + tableName + ".tb";
    std::string tdfFile = dbDir + "/" + tableName + ".tdf";
    std::string trdFile = dbDir + "/" + tableName + ".trd";

    if (FileManager::fileExists(tbFile)) {
        std::cerr << "[Error] Table already exists: " << tableName << std::endl;
        return false;
    }

    uint32_t currentOffset = 0;
    std::vector<ColumnDef> alignedFields = fields;

    for (auto& field : alignedFields) {
        field.offset = currentOffset;
        currentOffset += field.length;
        if (currentOffset % 4 != 0) {
            currentOffset += (4 - (currentOffset % 4));
        }
    }
    uint32_t totalRecordSize = currentOffset;

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

    FileManager::createFile(tdfFile);
    uint32_t tdfOffset = 0;
    for (const auto& field : alignedFields) {
        FileManager::writeStruct(tdfFile, field, tdfOffset);
        tdfOffset += sizeof(ColumnDef);
    }

    FileManager::createFile(trdFile);

    std::cout << "[Success] Table created: '" << tableName
              << "' | Fields: " << header.fieldCount
              << " | Record Size: " << totalRecordSize << " bytes." << std::endl;
    return true;
}

bool DDLExecutor::dropTable(const std::string& tableName) {
    std::string dbDir = DictManager::GetCurrentDB();
    FileManager::deleteFile(dbDir + "/" + tableName + ".tb");
    FileManager::deleteFile(dbDir + "/" + tableName + ".tdf");
    FileManager::deleteFile(dbDir + "/" + tableName + ".trd");
    return true;
}

// ---------------- 新增的高层封装接口 (接收 ASTNode 返回 ExecuteResult) ----------------

ExecuteResult DDLExecutor::executeCreateTable(const ASTNode* ast) {
    ExecuteResult res;
    std::vector<ColumnDef> parsedFields;
    
    // 【解析阶段】将前端传递来的字符串列表，如 "id INT", 转换为底层的 ColumnDef 结构
    for (const auto& colStr : ast->columns) {
        std::stringstream ss(colStr);
        std::string cName, cType;
        ss >> cName >> cType;
        
        ColumnDef def;
        std::memset(&def, 0, sizeof(ColumnDef));
        std::strncpy(def.fieldName, cName.c_str(), MAX_NAME_LEN - 1);
        
        // 简单类型映射判断 (不区分大小写)
        std::string upperType = cType;
        for (auto& c : upperType) c = std::toupper(c);
        
        if (upperType == "INT") {
            def.type = DataType::TYPE_INT;
            def.length = 4;
        } else if (upperType == "VARCHAR" || upperType.find("CHAR") != std::string::npos) {
            def.type = DataType::TYPE_VARCHAR;
            def.length = 256; // 这里为简化，固定给定 256 字节长度
        } else {
            def.type = DataType::TYPE_INT; // 兜底类型
            def.length = 4;
        }
        
        parsedFields.push_back(def);
    }

#ifdef USE_STORAGE_STUB
    if (createTable(ast->tbl, parsedFields)) {
        res.msg = "Query OK: Table '" + ast->tbl + "' created successfully.";
    } else {
        res.error = 1;
        res.msg = "Error: Failed to create table '" + ast->tbl + "'.";
    }
#else
    if (createTable(ast->tbl, parsedFields)) {
        res.msg = "Query OK: Table '" + ast->tbl + "' created successfully.";
    } else {
        res.error = 1;
        res.msg = "Error: Failed to create table '" + ast->tbl + "'.";
    }
#endif
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
#ifdef USE_STORAGE_STUB
    res.msg = "Query OK: Database '" + ast->db + "' created.";
#else
    ErrorCode err = DictManager::CreateDatabase(ast->db);
    if (err == ErrorCode::DB_OK) {
        res.msg = "Query OK: Database '" + ast->db + "' created.";
    } else {
        res.error = 1;
        res.msg = std::string("Error: ") + getErrorMessage(err);
    }
#endif
    return res;
}

ExecuteResult DDLExecutor::dropDatabase(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.msg = "Query OK: Database '" + ast->db + "' dropped.";
#else
    ErrorCode err = DictManager::DropDatabase(ast->db);
    if (err == ErrorCode::DB_OK) {
        res.msg = "Query OK: Database '" + ast->db + "' dropped.";
    } else {
        res.error = 1;
        res.msg = std::string("Error: ") + getErrorMessage(err);
    }
#endif
    return res;
}

ExecuteResult DDLExecutor::useDatabase(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.msg = "Database changed to '" + ast->db + "'.";
#else
    ErrorCode err = DictManager::UseDatabase(ast->db);
    if (err == ErrorCode::DB_OK) {
        res.msg = "Database changed to '" + ast->db + "'.";
    } else {
        res.error = 1;
        res.msg = std::string("Error: ") + getErrorMessage(err);
    }
#endif
    return res;
}

ExecuteResult DDLExecutor::showTables() {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.headers = {"Tables_in_db"};
    res.rows.push_back({"Users"}); // 假数据 Stub
    res.msg = "Query OK: 1 row in set";
#else
    std::vector<std::string> outTables;
    ErrorCode err = DictManager::ShowTables(outTables);
    if (err == ErrorCode::DB_OK) {
        res.headers = {"Tables_in_db"};
        for (const auto& t : outTables) {
            res.rows.push_back({t});
        }
        res.msg = "Query OK: " + std::to_string(outTables.size()) + " row(s) in set";
    } else {
        res.error = 1;
        res.msg = std::string("Error: ") + getErrorMessage(err);
    }
#endif
    return res;
}
