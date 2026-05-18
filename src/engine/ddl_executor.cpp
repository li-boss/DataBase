// src/engine/ddl_executor.cpp
#include "engine/ddl_executor.h"
#include "storage/file_manager.h"
#include "storage/buffer_pool.h"
#include "storage/dict_manager.h"
#include "common/db_errors.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <ctime>

extern std::string g_currentDbDir;

bool DDLExecutor::createTable(const std::string& tableName, const std::vector<ColumnDef>& fields) {
    std::string dbDir = DictManager::GetCurrentDB();
    if (dbDir.empty()) {
        std::cerr << "[Error] No database selected." << std::endl;
        return false;
    }
    if (fields.empty()) {
        std::cerr << "[Error] Table must have at least one column: " << tableName << std::endl;
        return false;
    }
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
    std::string tbFile = dbDir + "/" + tableName + ".tb";
    if (!FileManager::fileExists(tbFile)) {
        std::cerr << "[Error] Table does not exist: " << tableName << std::endl;
        return false;
    }
    FileManager::deleteFile(dbDir + "/" + tableName + ".tb");
    FileManager::deleteFile(dbDir + "/" + tableName + ".tdf");
    FileManager::deleteFile(dbDir + "/" + tableName + ".trd");
    return true;
}

// ---------------- 新增的高层封装接口 (接收 ASTNode 返回 ExecuteResult) ----------------

ExecuteResult DDLExecutor::executeCreateTable(const ASTNode* ast) {
    ExecuteResult res;
    std::vector<ColumnDef> parsedFields;
    
    // 【解析阶段】将前端传递来的字符串列表，如 "id INT NOT NULL", 转换为底层的 ColumnDef 结构
    for (const auto& colStr : ast->columns) {
        std::stringstream ss(colStr);
        std::string token;
        std::vector<std::string> tokens;
        while (ss >> token) {
            // 转大写用于关键字比较
            std::string upper = token;
            for (auto& c : upper) c = std::toupper(c);
            tokens.push_back(upper);
        }
        
        if (tokens.size() < 2) continue; // 至少需要 name + type
        
        // 跳过表级约束子句（如 PRIMARY KEY, UNIQUE, FOREIGN KEY, CHECK, CONSTRAINT, INDEX）
        if (tokens[0] == "PRIMARY" || tokens[0] == "UNIQUE" || tokens[0] == "FOREIGN" 
            || tokens[0] == "CHECK" || tokens[0] == "CONSTRAINT" || tokens[0] == "INDEX") {
            continue;
        }
        
        std::string cName = tokens[0];
        std::string cType = tokens[1];
        
        // 检测约束关键字
        bool isPK = false;
        bool notNull = false;
        for (size_t t = 2; t < tokens.size(); ++t) {
            if (tokens[t] == "PRIMARY" && t + 1 < tokens.size() && tokens[t + 1] == "KEY") {
                isPK = true;
                ++t; // 跳过 KEY
            } else if (tokens[t] == "NOT" && t + 1 < tokens.size() && tokens[t + 1] == "NULL") {
                notNull = true;
                ++t; // 跳过 NULL
            }
        }
        
        ColumnDef def;
        std::memset(&def, 0, sizeof(ColumnDef));
        std::strncpy(def.fieldName, cName.c_str(), MAX_NAME_LEN - 1);
        
        // 类型映射（不区分大小写）
        if (cType == "INT" || cType == "INTEGER") {
            def.type = DataType::TYPE_INT;
            def.length = 4;
        } else if (cType == "VARCHAR") {
            def.type = DataType::TYPE_VARCHAR;
            def.length = 256;
        } else if (cType.find("CHAR") != std::string::npos && cType != "VARCHAR") {
            def.type = DataType::TYPE_CHAR;
            def.length = 256;
        } else if (cType == "BOOL" || cType == "BOOLEAN") {
            def.type = DataType::TYPE_BOOLEAN;
            def.length = 1;
        } else if (cType == "FLOAT") {
            def.type = DataType::TYPE_FLOAT;
            def.length = 4;
        } else if (cType == "DOUBLE") {
            def.type = DataType::TYPE_DOUBLE;
            def.length = 8;
        } else if (cType == "TEXT") {
            def.type = DataType::TYPE_TEXT;
            def.length = 256;
        } else if (cType == "DATETIME") {
            def.type = DataType::TYPE_DATETIME;
            def.length = 256;
        } else {
            def.type = DataType::TYPE_VARCHAR; // 兜底
            def.length = 256;
        }
        
        def.isPrimaryKey = isPK ? 1u : 0u;
        def.constraints = notNull ? 1u : 0u; // bit 0 = NOT NULL
        
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

// ══════════════════════════════════════════════════
//  ALTER TABLE — 核心实现（ADD / DROP / MODIFY COLUMN）
// ══════════════════════════════════════════════════

// 辅助：类型字符串 → DataType + 长度
static ColumnDef parseColumnType(const std::string& typeName, const std::string& colName,
                                  bool isPK, bool notNull) {
    ColumnDef def;
    std::memset(&def, 0, sizeof(ColumnDef));
    std::strncpy(def.fieldName, colName.c_str(), MAX_NAME_LEN - 1);
    std::string upperType = typeName;
    for (auto& c : upperType) c = static_cast<unsigned char>(std::toupper(c));

    if (upperType == "INT" || upperType == "INTEGER") { def.type = DataType::TYPE_INT; def.length = 4; }
    else if (upperType == "VARCHAR")               { def.type = DataType::TYPE_VARCHAR; def.length = 256; }
    else if (upperType.find("CHAR") != std::string::npos && upperType != "VARCHAR") { def.type = DataType::TYPE_CHAR; def.length = 256; }
    else if (upperType == "BOOL" || upperType == "BOOLEAN") { def.type = DataType::TYPE_BOOLEAN; def.length = 1; }
    else if (upperType == "FLOAT")                { def.type = DataType::TYPE_FLOAT; def.length = 4; }
    else if (upperType == "DOUBLE")               { def.type = DataType::TYPE_DOUBLE; def.length = 8; }
    else if (upperType == "TEXT")                 { def.type = DataType::TYPE_TEXT; def.length = 256; }
    else if (upperType == "DATETIME")             { def.type = DataType::TYPE_DATETIME; def.length = 256; }
    else                                          { def.type = DataType::TYPE_VARCHAR; def.length = 256; } // 兜底

    def.isPrimaryKey = isPK ? 1u : 0u;
    def.constraints = notNull ? 1u : 0u;
    return def;
}

// 辅助：重新计算所有字段的 offset，返回新的 recordSize
static uint32_t recalcOffsets(std::vector<ColumnDef>& fields) {
    uint32_t off = 0;
    for (auto& f : fields) {
        f.offset = off;
        off += f.length;
        if (off % 4 != 0) off += (4 - (off % 4));
    }
    return off;
}

// 辅助：从 .trd 中读取全部记录到内存（每条记录是 raw bytes）
static bool readAllRecords(const std::string& trdPath, uint32_t recordSize, uint32_t recordCount,
                            std::vector<std::vector<uint8_t>>& outRecords) {
    int fd;
    if (!FileManager::OpenFile(trdPath, "r", fd)) return false;

    if (recordCount == 0) { FileManager::CloseFile(fd); return true; }

    uint32_t rpp = 4080 / recordSize; if (!rpp) rpp = 1;
    uint32_t totalPgs = (recordCount + rpp - 1) / rpp;

    for (uint32_t pid = 0; pid < totalPgs; ++pid) {
        void* pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) continue;
        char* base = static_cast<char*>(pageData);
        uint32_t startR = pid * rpp;
        uint32_t endR = std::min(startR + rpp, recordCount);
        for (uint32_t ri = startR; ri < endR; ++ri) {
            std::vector<uint8_t> rec(recordSize);
            std::memcpy(rec.data(), base + (ri % rpp) * recordSize, recordSize);
            outRecords.push_back(std::move(rec));
        }
        BufferPool::ReleasePage(fd, pid);
    }
    FileManager::CloseFile(fd);
    return true;
}

// 辅助：将记录写入 .trd（覆盖式）
static bool writeAllRecords(const std::string& trdPath, uint32_t newRecordSize,
                             const std::vector<std::vector<uint8_t>>& records) {
    int fd;
    if (!FileManager::OpenFile(trdPath, "rw", fd)) return false;

    // 清空文件：先 truncate 再写
    FileManager::CloseFile(fd);
    // 重新打开以清空
    // 简单方案：逐页写入
    if (!FileManager::OpenFile(trdPath, "rw", fd)) return false;

    uint32_t rpp = 4080 / newRecordSize; if (!rpp) rpp = 1;

    for (size_t i = 0; i < records.size(); ++i) {
        uint32_t pid = static_cast<uint32_t>(i) / rpp;
        uint32_t offset = (static_cast<uint32_t>(i) % rpp) * newRecordSize;

        void* pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) continue;
        std::memcpy(static_cast<char*>(pageData) + offset, records[i].data(), newRecordSize);
        BufferPool::MarkDirty(fd, pid);
        BufferPool::ReleasePage(fd, pid);
    }
    FileManager::CloseFile(fd);
    return true;
}

// ─── ALTER TABLE 入口 ──────────────────────────────
ExecuteResult DDLExecutor::executeAlterTable(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.msg = "Query OK: Table '" + ast->tbl + "' altered (stub mode).";
#else
    std::string dbDir = DictManager::GetCurrentDB();
    std::string tbFile   = dbDir + "/" + ast->tbl + ".tb";
    std::string tdfFile  = dbDir + "/" + ast->tbl + ".tdf";
    std::string trdFile  = dbDir + "/" + ast->tbl + ".trd";

    // 检查表是否存在
    if (!FileManager::fileExists(tbFile)) {
        res.error = 1; res.msg = "Error: Table '" + ast->tbl + "' does not exist.";
        return res;
    }

    // 加载当前 Schema
    TableHeader header;
    std::vector<ColumnDef> oldFields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, oldFields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1; res.msg = "Error: Failed to load table metadata.";
        return res;
    }

    std::vector<ColumnDef> newFields;

    switch (ast->alterAction) {

    case AlterAction::ADD_COLUMN: {
        // ── ADD COLUMN ──
        // 0. 校验列名非空
        if (ast->alterColumnName.empty()) {
            res.error = 1; res.msg = "Error: Column name cannot be empty.";
            return res;
        }
        // 1. 检查列名是否已存在
        for (const auto& f : oldFields) {
            if (f.fieldName == ast->alterColumnName) {
                res.error = 1; res.msg = "Error: Column '" + ast->alterColumnName + "' already exists.";
                return res;
            }
        }
        // 2. 构建新字段列表（旧字段 + 新字段）
        newFields = oldFields;
        newFields.push_back(parseColumnType(ast->alterColumnType.empty() ? "VARCHAR" : ast->alterColumnType,
                                            ast->alterColumnName, ast->alterPrimaryKey, ast->alterNotNull));
        break;
    }

    case AlterAction::DROP_COLUMN: {
        // ── DROP COLUMN ──
        if (ast->alterColumnName.empty()) {
            res.error = 1; res.msg = "Error: Column name cannot be empty.";
            return res;
        }
        bool found = false;
        for (const auto& f : oldFields) {
            if (f.fieldName != ast->alterColumnName)
                newFields.push_back(f);
            else
                found = true;
        }
        if (!found) {
            res.error = 1; res.msg = "Error: Column '" + ast->alterColumnName + "' does not exist.";
            return res;
        }
        break;
    }

    case AlterAction::MODIFY_COLUMN: {
        // ── MODIFY COLUMN ──
        if (ast->alterColumnName.empty()) {
            res.error = 1; res.msg = "Error: Column name cannot be empty.";
            return res;
        }
        newFields = oldFields;
        bool found = false;
        for (auto& f : newFields) {
            if (f.fieldName == ast->alterColumnName) {
                found = true;
                if (!ast->alterColumnType.empty()) {
                    ColumnDef updated = parseColumnType(ast->alterColumnType, ast->alterColumnName,
                                                        f.isPrimaryKey != 0, (f.constraints & 1u) != 0);
                    // 保留原有约束标记
                    if (ast->alterNotNull)   updated.constraints |= 1u;
                    if (ast->alterPrimaryKey) updated.isPrimaryKey = 1u;
                    f = updated;
                }
                if (ast->alterNotNull)   f.constraints |= 1u;
                if (ast->alterPrimaryKey){ f.isPrimaryKey = 1u; f.constraints |= 1u; }
                break;
            }
        }
        if (!found) {
            res.error = 1; res.msg = "Error: Column '" + ast->alterColumnName + "' does not exist.";
            return res;
        }
        break;
    }

    default:
        res.error = 1; res.msg = "Error: Unknown ALTER action.";
        return res;
    }

    // ── 通用后处理：重算偏移、迁移数据、写回磁盘 ──
    uint32_t newRecordSize = recalcOffsets(newFields);

    // 读取所有现有记录（按旧 schema）
    std::vector<std::vector<uint8_t>> oldRecords;
    readAllRecords(trdFile, header.recordSize, header.recordCount, oldRecords);

    // 数据迁移：根据新旧字段映射转换每条记录
    std::vector<std::vector<uint8_t>> newRecords;
    for (const auto& oldRec : oldRecords) {
        std::vector<uint8_t> newRec(newRecordSize, 0);

        for (size_t ni = 0; ni < newFields.size(); ++ni) {
            // 在旧字段中查找同名字段
            for (size_t oi = 0; oi < oldFields.size(); ++oi) {
                if (newFields[ni].fieldName == oldFields[oi].fieldName) {
                    // 复制数据（取 min(新旧长度) 字节）
                    uint32_t copyLen = std::min(newFields[ni].length, oldFields[oi].length);
                    if (newFields[ni].offset + copyLen <= newRecordSize &&
                        oldFields[oi].offset + copyLen <= header.recordSize) {
                        std::memcpy(newRec.data() + newFields[ni].offset,
                                    oldRec.data() + oldFields[oi].offset, copyLen);
                    }
                    break;
                }
            }
            // 新增的列（ADD COLUMN）保持全零（已 memset 为 0）
        }
        newRecords.push_back(std::move(newRec));
    }

    // 写回 .tb（更新 recordSize/fieldCount/modifyTime）
    header.fieldCount = static_cast<uint32_t>(newFields.size());
    header.recordSize = newRecordSize;
    header.modifyTime = static_cast<uint32_t>(std::time(nullptr));
    FileManager::writeStruct(tbFile, header, 0);

    // 写回 .tdf（新字段定义列表）
    {
        int tdfFd;
        // 清除旧的 tdf 内容，重新写入
        FileManager::createFile(tdfFile); // 覆盖创建
        uint32_t tdfOff = 0;
        for (const auto& nf : newFields) {
            FileManager::writeStruct(tdfFile, nf, tdfOff);
            tdfOff += sizeof(ColumnDef);
        }
    }

    // 写回 .trd（迁移后的数据）
    writeAllRecords(trdFile, newRecordSize, newRecords);

    // recordCount 不变
    DictManager::updateRecordCount(ast->tbl, header.recordCount);

    // 动作描述
    std::string actionStr;
    switch (ast->alterAction) {
        case AlterAction::ADD_COLUMN:    actionStr = "ADD COLUMN"; break;
        case AlterAction::DROP_COLUMN:   actionStr = "DROP COLUMN"; break;
        case AlterAction::MODIFY_COLUMN: actionStr = "MODIFY COLUMN"; break;
    }

    res.msg = "Query OK: " + actionStr + " '" + ast->alterColumnName + "' on '" +
              ast->tbl + "'. New record size: " + std::to_string(newRecordSize) + " bytes.";
#endif
    return res;
}

ExecuteResult DDLExecutor::executeCreateIndex(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Index '" + ast->columns[0] + "' created on '" + ast->tbl + "(" + ast->columns[1] + ")' (Stub).";
    return res;
}

ExecuteResult DDLExecutor::executeDropIndex(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Index '" + ast->columns[0] + "' dropped from '" + ast->tbl + "' (Stub).";
    return res;
}

ExecuteResult DDLExecutor::executeShowIndexes(const ASTNode* ast) {
    ExecuteResult res;
    res.headers = {"Table", "Non_unique", "Key_name", "Column_name"};
    res.rows.push_back({ast->tbl, "1", "stub_idx", "stub_col"});
    res.msg = "Query OK: 1 row in set (Stub).";
    return res;
}

ExecuteResult DDLExecutor::executeCreateView(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: View '" + ast->columns[0] + "' created as '" + ast->values[0] + "' (Stub).";
    return res;
}
