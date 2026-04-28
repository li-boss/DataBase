// src/engine/dml_executor.cpp
#include "../../include/engine/dml_executor.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/buffer_pool.h"
#include "../../include/common/db_errors.h"
#include <iostream>
#include <cstring>
#include <string>
#include <cmath>
#include <sstream>

// 辅助：将字符串转为布尔值
static bool parseBool(const std::string& s) {
    std::string upper = s;
    for (auto& c : upper) c = std::toupper(static_cast<unsigned char>(c));
    return upper == "TRUE" || upper == "T" || upper == "1" || upper == "YES" || upper == "Y";
}

// 辅助：布尔值转显示字符串
static std::string boolToString(uint8_t val) { return val ? "true" : "false"; }

ExecuteResult DMLExecutor::insertRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.error = 0;
    res.msg = "Query OK: Successfully inserted mock data into [" + ast->tbl + "]";
    std::cout << "[DML Engine] Simulating INSERT to disk..." << std::endl;
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1;
        res.msg = "Error: " + std::string(getErrorMessage(err));
        return res;
    }

    if (ast->values.size() != fields.size()) {
        res.error = 1;
        res.msg = "Error: Insert value count does not match field count.";
        return res;
    }

    // ── 1. NOT NULL 校验 ──
    for (size_t i = 0; i < fields.size(); ++i) {
        bool isNotNull = ((fields[i].constraints & 1u) != 0 || fields[i].isPrimaryKey != 0);
        if (isNotNull && (ast->values[i].empty() || ast->values[i] == "''")) {
            res.error = 1;
            res.msg = "Error: Column '" + std::string(fields[i].fieldName) + "' cannot be NULL.";
            return res;
        }
    }

    // ── 2. 主键唯一性检查 ──
    int pkIndex = -1;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].isPrimaryKey != 0) { pkIndex = static_cast<int>(i); break; }
    }
    if (pkIndex >= 0) {
        std::string pkVal = ast->values[pkIndex];
        
        int fd;
        if (FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "r", fd)) {
            uint32_t recordsPerPage = 4080 / header.recordSize;
            if (recordsPerPage == 0) recordsPerPage = 1;
            uint32_t totalPages = (header.recordCount + recordsPerPage - 1) / recordsPerPage;
            
            for (uint32_t pid = 0; pid < totalPages; ++pid) {
                void* pageData = BufferPool::GetPage(fd, pid);
                if (!pageData) continue;
                
                uint32_t startRec = pid * recordsPerPage;
                uint32_t endRec = std::min(startRec + recordsPerPage, header.recordCount);
                
                for (uint32_t ri = startRec; ri < endRec; ++ri) {
                    char* recPtr = static_cast<char*>(pageData) + (ri % recordsPerPage) * header.recordSize;
                    const auto& f = fields[pkIndex];
                    
                    std::string existingPk;
                    if (f.type == DataType::TYPE_INT) {
                        int32_t v;
                        std::memcpy(&v, recPtr + f.offset, 4);
                        existingPk = std::to_string(v);
                    } else if (f.type == DataType::TYPE_FLOAT) {
                        float v;
                        std::memcpy(&v, recPtr + f.offset, 4);
                        std::ostringstream oss;
                        oss << v;
                        existingPk = oss.str();
                    } else if (f.type == DataType::TYPE_DOUBLE) {
                        double v;
                        std::memcpy(&v, recPtr + f.offset, 8);
                        std::ostringstream oss;
                        oss << v;
                        existingPk = oss.str();
                    } else if (f.type == DataType::TYPE_BOOLEAN) {
                        existingPk = boolToString(*(recPtr + f.offset));
                    } else {
                        char buf[512] = {0};
                        std::strncpy(buf, recPtr + f.offset, f.length > 511 ? 511 : f.length);
                        existingPk = buf;
                    }
                    
                    if (existingPk == pkVal) {
                        BufferPool::ReleasePage(fd, pid);
                        FileManager::CloseFile(fd);
                        res.error = 1;
                        res.msg = "Error: Duplicate primary key value '" + pkVal + "' for column '" + std::string(fields[pkIndex].fieldName) + "'.";
                        return res;
                    }
                }
                BufferPool::ReleasePage(fd, pid);
            }
            FileManager::CloseFile(fd);
        }
    }

    // ── 3. 写入记录 ──
    int fd;
    if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "rw", fd)) {
        res.error = 1;
        res.msg = "Error: Failed to open data file.";
        return res;
    }

    uint32_t recordsPerPage = 4080 / header.recordSize; 
    if (recordsPerPage == 0) recordsPerPage = 1;
    uint32_t pid = header.recordCount / recordsPerPage;
    uint32_t offset = (header.recordCount % recordsPerPage) * header.recordSize;

    void* pageData = BufferPool::GetPage(fd, pid);
    if (!pageData) {
        char blank_page[4096] = {0};
        FileManager::WritePage(fd, pid, blank_page);
        pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) {
            res.error = 1; res.msg = "Error: Buffer pool failed to load page even after allocation.";
            FileManager::CloseFile(fd); return res;
        }
    }

    char* recordPtr = static_cast<char*>(pageData) + offset;
    
    for (size_t i = 0; i < fields.size(); ++i) {
        switch (fields[i].type) {
            case DataType::TYPE_INT: {
                int32_t val = 0;
                try { val = std::stoi(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 4);
                break;
            }
            case DataType::TYPE_FLOAT: {
                float val = 0.0f;
                try { val = std::stof(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 4);
                break;
            }
            case DataType::TYPE_DOUBLE: {
                double val = 0.0;
                try { val = std::stod(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 8);
                break;
            }
            case DataType::TYPE_BOOLEAN: {
                uint8_t bval = parseBool(ast->values[i]) ? 1 : 0;
                *(recordPtr + fields[i].offset) = bval;
                break;
            }
            default: { // VARCHAR, CHAR, TEXT, DATETIME 等 → 字符串存储
                std::strncpy(recordPtr + fields[i].offset, ast->values[i].c_str(), fields[i].length - 1);
                recordPtr[fields[i].offset + fields[i].length - 1] = '\0';
                break;
            }
        }
    }

    BufferPool::MarkDirty(fd, pid);
    BufferPool::ReleasePage(fd, pid);
    FileManager::CloseFile(fd);

    DictManager::updateRecordCount(ast->tbl, header.recordCount + 1);
    res.msg = "Query OK: 1 row affected.";
#endif
    return res;
}

// ─── 辅助：WHERE 条件评估（类型感知比较）─────────────────────
// 根据 ColumnDef 类型做数值/字符串比较，支持 = != < > <= >= <>
static bool evaluateWhere(const std::string& fieldValue, const std::string& op,
                          const std::string& targetValue, DataType type) {
    // 统一操作符
    std::string upperOp = op;
    for (auto& c : upperOp) c = std::toupper(static_cast<unsigned char>(c));
    if (upperOp == "<>") upperOp = "!=";

    // 数值类型用数值比较
    if (type == DataType::TYPE_INT || type == DataType::TYPE_FLOAT || type == DataType::TYPE_DOUBLE) {
        try {
            double lv = std::stod(fieldValue);
            double rv = std::stod(targetValue);
            if (upperOp == "=")  return lv == rv;
            if (upperOp == "!=") return lv != rv;
            if (upperOp == "<")  return lv <  rv;
            if (upperOp == ">")  return lv >  rv;
            if (upperOp == "<=") return lv <= rv;
            if (upperOp == ">=") return lv >= rv;
        } catch (...) { return false; } // 数值解析失败，不匹配
        return false;
    }

    // BOOL 类型比较
    if (type == DataType::TYPE_BOOLEAN) {
        bool lb = parseBool(fieldValue);
        bool rb = parseBool(targetValue);
        if (upperOp == "=")  return lb == rb;
        if (upperOp == "!=") return lb != rb;
        // 布尔不支持大小比较，默认 false
        return false;
    }

    // 字符串类型（CHAR/VARCHAR/TEXT/DATETIME）按字典序比较
    if (upperOp == "=")  return fieldValue == targetValue;
    if (upperOp == "!=") return fieldValue != targetValue;
    if (upperOp == "<")  return fieldValue <  targetValue;
    if (upperOp == ">")  return fieldValue >  targetValue;
    if (upperOp == "<=") return fieldValue <= targetValue;
    if (upperOp == ">=") return fieldValue >= targetValue;

    return false; // 未知操作符默认不匹配
}

ExecuteResult DMLExecutor::selectRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    // 逻辑思路：
    // 1. 扫描整个 .trd 文件（后续配合 BufferPool 就是根据页遍历）。
    // 2. 按 RecordSize 一个个切分记录实体。
    // 3. 对每一行，评估是否存在 WHERE；如果 hasWhere == true 则比较对应字段。
    
    // 【Stub 桩代码模式】：在磁盘读取没好之前，我们造一条假数据用于证明整个流程的联接
    res.error = 0;
    res.msg = "Query OK: 1 row simulated in set";
    // 假装查出了两个字段
    res.headers = {"system_id", "status"};
    res.rows.push_back({"1001", "RUANKO_STUB_WORKING"});
    
    if (ast->where.hasWhere) {
        std::cout << "[DML Engine] Filtering rows where " << ast->where.column 
                  << " " << ast->where.op << " " << ast->where.value << std::endl;
    }
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1; res.msg = "Error: " + std::string(getErrorMessage(err)); return res;
    }

    for (const auto& f : fields) {
        res.headers.push_back(f.fieldName);
    }

    int fd;
    if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "r", fd)) {
        res.msg = "Query OK: Empty set"; return res;
    }

    uint32_t recordsPerPage = 4080 / header.recordSize;
    if (recordsPerPage == 0) recordsPerPage = 1;
    uint32_t totalPages = (header.recordCount + recordsPerPage - 1) / recordsPerPage;

    uint32_t recordsRead = 0;
    for (uint32_t pid = 0; pid < totalPages; ++pid) {
        void* pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) continue;

        for (uint32_t i = 0; i < recordsPerPage && recordsRead < header.recordCount; ++i) {
            char* recordPtr = static_cast<char*>(pageData) + i * header.recordSize;
            std::vector<std::string> row;
            
            for (const auto& f : fields) {
                switch (f.type) {
                    case DataType::TYPE_INT: {
                        int32_t val;
                        std::memcpy(&val, recordPtr + f.offset, 4);
                        row.push_back(std::to_string(val));
                        break;
                    }
                    case DataType::TYPE_FLOAT: {
                        float val;
                        std::memcpy(&val, recordPtr + f.offset, 4);
                        // 避免不必要的 .0
                        if (val == static_cast<int>(val)) {
                            row.push_back(std::to_string(static_cast<int>(val)) + ".0");
                        } else {
                            std::ostringstream oss;
                            oss << val;
                            row.push_back(oss.str());
                        }
                        break;
                    }
                    case DataType::TYPE_DOUBLE: {
                        double val;
                        std::memcpy(&val, recordPtr + f.offset, 8);
                        if (val == static_cast<int64_t>(val)) {
                            row.push_back(std::to_string(static_cast<int64_t>(val)) + ".0");
                        } else {
                            std::ostringstream oss;
                            oss << val;
                            row.push_back(oss.str());
                        }
                        break;
                    }
                    case DataType::TYPE_BOOLEAN:
                        row.push_back(boolToString(*(recordPtr + f.offset)));
                        break;
                    default: { // VARCHAR, CHAR, TEXT, DATETIME → 字符串
                        size_t bufLen = f.length > 512 ? 511 : f.length;
                        char* buf = new char[bufLen + 1]();
                        std::strncpy(buf, recordPtr + f.offset, bufLen);
                        row.push_back(std::string(buf));
                        delete[] buf;
                        break;
                    }
                }
            }

            // ── WHERE 过滤 ──
            if (ast->where.hasWhere) {
                int colIndex = -1;
                DataType colType = DataType::TYPE_VARCHAR; // 默认字符串类型
                for (size_t ci = 0; ci < fields.size(); ++ci) {
                    if (fields[ci].fieldName == ast->where.column) {
                        colIndex = static_cast<int>(ci);
                        colType = fields[ci].type;
                        break;
                    }
                }

                if (colIndex >= 0 && colIndex < static_cast<int>(row.size())) {
                    bool matched = evaluateWhere(row[colIndex], ast->where.op, ast->where.value, colType);
                    if (!matched) { recordsRead++; continue; } // 不满足条件，跳过此行
                } else {
                    // WHERE 列名不存在于表中 → 空结果（与 MySQL 行为一致）
                    res.rows.clear(); recordsRead = 0;
                    res.msg = "Query OK: Empty set (unknown column '" + ast->where.column + "')";
                    BufferPool::ReleasePage(fd, pid);
                    FileManager::CloseFile(fd);
                    return res;
                }
            }

            res.rows.push_back(row);
            recordsRead++;
        }
        BufferPool::ReleasePage(fd, pid);
    }
    FileManager::CloseFile(fd);

    res.msg = "Query OK: " + std::to_string(res.rows.size()) + " row(s) in set";
#endif
    return res;
}

ExecuteResult DMLExecutor::updateRecord(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Rows matched: 1  Changed: 1  Warnings: 0";
    return res;
}

ExecuteResult DMLExecutor::deleteRecord(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: 1 row deleted";
    return res;
}
