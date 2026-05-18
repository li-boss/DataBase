// src/engine/dml_executor.cpp
#include "../../include/engine/dml_executor.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/buffer_pool.h"
#include "../../include/storage/index_manager.h"
#include "../../include/common/db_errors.h"
#include "../../include/common/db_structs.h"
#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// 辅助：将字符串转为布尔值
static bool parseBool(const std::string& s) {
    std::string upper = s;
    for (auto& c : upper) c = std::toupper(static_cast<unsigned char>(c));
    return upper == "TRUE" || upper == "T" || upper == "1" || upper == "YES" || upper == "Y";
}

// 辅助：布尔值转显示字符串
static std::string boolToString(uint8_t val) { return val ? "true" : "false"; }

extern std::string g_currentDbDir;

// ─── 辅助：将字符串值按字段类型序列化为二进制（供索引 key 构建使用）─────────
static void serializeValue(char* outBuf,
                           const ColumnDef& col,
                           const std::string& value) {
    DataType t = static_cast<DataType>(col.type);
    if (t == DataType::TYPE_INT ||
        t == DataType::TYPE_DATETIME ||
        t == DataType::TYPE_BOOLEAN) {
        uint32_t v = static_cast<uint32_t>(std::atoi(value.c_str()));
        std::memcpy(outBuf + col.offset, &v, sizeof(uint32_t));
    } else {
        size_t len = value.size();
        if (len > col.length) len = col.length;
        std::memcpy(outBuf + col.offset, value.c_str(), len);
    }
}

// ─── INSERT ──────────────────────────────────────────────
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
                        int32_t v; std::memcpy(&v, recPtr + f.offset, 4);
                        existingPk = std::to_string(v);
                    } else if (f.type == DataType::TYPE_FLOAT) {
                        float v; std::memcpy(&v, recPtr + f.offset, 4);
                        std::ostringstream oss; oss << v; existingPk = oss.str();
                    } else if (f.type == DataType::TYPE_DOUBLE) {
                        double v; std::memcpy(&v, recPtr + f.offset, 8);
                        std::ostringstream oss; oss << v; existingPk = oss.str();
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
        res.error = 1; res.msg = "Error: Failed to open data file."; return res;
    }

    uint32_t recordsPerPage = 4080 / header.recordSize;
    if (recordsPerPage == 0) recordsPerPage = 1;
    uint32_t pid = header.recordCount / recordsPerPage;
    uint32_t offset = (header.recordCount % recordsPerPage) * header.recordSize;
    uint32_t recordOffset = header.recordCount * header.recordSize;

    void* pageData = BufferPool::GetPage(fd, pid);
    if (!pageData) {
        char blank_page[4096] = {0};
        FileManager::WritePage(fd, pid, blank_page);
        pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) {
            res.error = 1; res.msg = "Error: Buffer pool failed."; FileManager::CloseFile(fd); return res;
        }
    }

    char* recordPtr = static_cast<char*>(pageData) + offset;
    std::vector<char> recordBuf(header.recordSize, 0);

    for (size_t i = 0; i < fields.size(); ++i) {
        switch (fields[i].type) {
            case DataType::TYPE_INT: {
                int32_t val = 0; try { val = std::stoi(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 4);
                std::memcpy(recordBuf.data() + fields[i].offset, &val, 4);
                break;
            }
            case DataType::TYPE_FLOAT: {
                float val = 0.0f; try { val = std::stof(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 4);
                std::memcpy(recordBuf.data() + fields[i].offset, &val, 4);
                break;
            }
            case DataType::TYPE_DOUBLE: {
                double val = 0.0; try { val = std::stod(ast->values[i]); } catch(...) {}
                std::memcpy(recordPtr + fields[i].offset, &val, 8);
                std::memcpy(recordBuf.data() + fields[i].offset, &val, 8);
                break;
            }
            case DataType::TYPE_BOOLEAN: {
                uint8_t bval = parseBool(ast->values[i]) ? 1 : 0;
                *(recordPtr + fields[i].offset) = bval;
                *(recordBuf.data() + fields[i].offset) = bval;
                break;
            }
            default: {
                std::strncpy(recordPtr + fields[i].offset, ast->values[i].c_str(), fields[i].length - 1);
                recordPtr[fields[i].offset + fields[i].length - 1] = '\0';
                std::strncpy(recordBuf.data() + fields[i].offset, ast->values[i].c_str(), fields[i].length - 1);
                recordBuf.data()[fields[i].offset + fields[i].length - 1] = '\0';
                break;
            }
        }
    }

    BufferPool::MarkDirty(fd, pid);
    BufferPool::ReleasePage(fd, pid);
    FileManager::CloseFile(fd);
    DictManager::updateRecordCount(ast->tbl, header.recordCount + 1);

    // ── 4. 同步索引 ──
    ErrorCode idxErr = IndexManager::InsertEntry(ast->tbl, recordOffset, recordBuf.data(), fields);
    if (idxErr != ErrorCode::DB_OK) {
        std::cerr << "[DML] Warning: Index sync failed: " << getErrorMessage(idxErr) << "\n";
    }

    res.msg = "Query OK: 1 row affected.";
#endif
    return res;
}

// ─── 辅助：从记录二进制中按类型读取字段值 ──────────
static std::string readFieldValue(const char* recordPtr, const ColumnDef& f) {
    switch (f.type) {
        case DataType::TYPE_INT: { int32_t val; std::memcpy(&val, recordPtr + f.offset, 4); return std::to_string(val); }
        case DataType::TYPE_FLOAT: { float val; std::memcpy(&val, recordPtr + f.offset, 4); std::ostringstream oss; oss << val; return oss.str(); }
        case DataType::TYPE_DOUBLE: { double val; std::memcpy(&val, recordPtr + f.offset, 8); std::ostringstream oss; oss << val; return oss.str(); }
        case DataType::TYPE_BOOLEAN: return (*(recordPtr + f.offset) != 0) ? "true" : "false";
        default: { char buf[512] = {}; std::strncpy(buf, recordPtr + f.offset, f.length > 511 ? 511 : f.length - 1); return std::string(buf); }
    }
}

// 根据 ColumnDef 类型做数值/字符串比较
static bool evaluateWhere(const std::string& fieldValue, const std::string& op,
                          const std::string& targetValue, DataType type) {
    std::string upperOp = op;
    for (auto& c : upperOp) c = std::toupper(static_cast<unsigned char>(c));
    if (upperOp == "<>") upperOp = "!=";

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
        } catch (...) { return false; }
        return false;
    }
    if (type == DataType::TYPE_BOOLEAN) {
        bool lb = parseBool(fieldValue);
        bool rb = parseBool(targetValue);
        if (upperOp == "=")  return lb == rb;
        if (upperOp == "!=") return lb != rb;
        return false;
    }
    if (upperOp == "=")  return fieldValue == targetValue;
    if (upperOp == "!=") return fieldValue != targetValue;
    if (upperOp == "<")  return fieldValue <  targetValue;
    if (upperOp == ">")  return fieldValue >  targetValue;
    if (upperOp == "<=") return fieldValue <= targetValue;
    if (upperOp == ">=") return fieldValue >= targetValue;
    return false;
}

// ─── 复合 WHERE 评估：对一条记录的所有条件做 AND/OR 组合 ───
static bool evaluateCompoundWhere(const std::vector<std::string>& row,
                                  const WhereClause& where,
                                  const std::vector<ColumnDef>& fields) {
    if (!where.hasWhere || where.conditions.empty()) return true;

    // 逐条件求值
    std::vector<bool> results;
    for (const auto& cond : where.conditions) {
        int colIndex = -1;
        DataType colType = DataType::TYPE_VARCHAR;
        for (size_t ci = 0; ci < fields.size(); ++ci) {
            if (fields[ci].fieldName == cond.column) {
                colIndex = static_cast<int>(ci);
                colType = fields[ci].type;
                break;
            }
        }
        if (colIndex < 0 || colIndex >= static_cast<int>(row.size())) {
            results.push_back(false);
        } else {
            results.push_back(evaluateWhere(row[colIndex], cond.op, cond.value, colType));
        }
    }

    // 按 logicOps 组合（默认隐含 AND）
    bool finalResult = results[0];
    for (size_t i = 0; i < where.logicOps.size(); ++i) {
        if (where.logicOps[i] == LogicOp::AND) {
            finalResult = finalResult && results[i + 1];
        } else { // OR
            finalResult = finalResult || results[i + 1];
        }
    }
    return finalResult;
}

// ─── 根据 ColumnDef 类型做数值/字符串比较 ───
static bool evaluateSingleCondition(const std::string& fieldValue,
                                     const SingleCondition& cond, DataType type) {
    return evaluateWhere(fieldValue, cond.op, cond.value, type);
}

// ─── SELECT ──────────────────────────────────────────────
ExecuteResult DMLExecutor::selectRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.error = 0;
    res.msg = "Query OK: 1 row simulated in set";
    res.headers = {"system_id", "status"};
    res.rows.push_back({"1001", "RUANKO_STUB_WORKING"});
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1; res.msg = "Error: " + std::string(getErrorMessage(err)); return res;
    }

    bool projectAll = false;
    if (ast->columns.empty() || (ast->columns.size() == 1 && ast->columns[0] == "*")) {
        projectAll = true;
    }

    std::vector<size_t> projectedIndices;
    if (projectAll) {
        for (size_t i = 0; i < fields.size(); ++i) {
            res.headers.push_back(fields[i].fieldName);
            projectedIndices.push_back(i);
        }
    } else {
        for (const auto& colName : ast->columns) {
            std::string upperCol = colName;
            for (auto& c : upperCol) c = std::toupper(c);
            bool found = false;
            for (size_t i = 0; i < fields.size(); ++i) {
                std::string fieldNameStr = fields[i].fieldName;
                for (auto& c : fieldNameStr) c = std::toupper(c);
                if (upperCol == fieldNameStr) {
                    res.headers.push_back(fields[i].fieldName);
                    projectedIndices.push_back(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                res.error = 1;
                res.msg = "Error: Column '" + colName + "' not found.";
                return res;
            }
        }
    }

    std::vector<size_t> projectedIndices;
    if (projectAll) {
        for (size_t i = 0; i < fields.size(); ++i) {
            res.headers.push_back(fields[i].fieldName);
            projectedIndices.push_back(i);
        }
    } else {
        for (const auto& colName : ast->columns) {
            std::string upperCol = colName;
            for (auto& c : upperCol) c = std::toupper(static_cast<unsigned char>(c));
            bool found = false;
            for (size_t i = 0; i < fields.size(); ++i) {
                std::string fieldNameStr = fields[i].fieldName;
                for (auto& c : fieldNameStr) c = std::toupper(static_cast<unsigned char>(c));
                if (upperCol == fieldNameStr) {
                    res.headers.push_back(fields[i].fieldName);
                    projectedIndices.push_back(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                res.error = 1;
                res.msg = "Error: Column '" + colName + "' not found.";
                return res;
            }
        }
    }

    // ── 索引加速（等值查询 + 范围查询）──
    std::vector<uint32_t> matchedOffsets;
    bool usedIndex = false;
    if (ast->where.hasWhere && !ast->where.conditions.empty()) {
        std::vector<std::string> indexes;
        DictManager::ListIndexes(ast->tbl, indexes);

        // ── 复合索引优先匹配 ──
        for (const auto& idxName : indexes) {
            IndexHeader idxHdr;
            if (DictManager::GetIndexHeader(idxName, idxHdr) != ErrorCode::DB_OK) continue;
            uint32_t colCount = IndexManager::GetCompositeColumnCount(idxHdr);
            if (colCount <= 1) continue;  // 单列索引用后面的逻辑

            // 获取复合索引的列名列表
            std::vector<std::string> idxColNames;
            IndexManager::GetCompositeColumnNames(idxHdr, idxColNames);
            uint32_t indices[4];
            IndexManager::GetCompositeColumnIndices(idxHdr, indices);

            // 检查 WHERE 条件是否覆盖了复合索引的左前缀
            // 规则：前 k 列全部等值匹配 → 可做等值/范围查找
            int matchedPrefix = 0;
            bool allEquality = true;
            for (int ci = 0; ci < static_cast<int>(colCount); ++ci) {
                bool foundCol = false;
                for (const auto& cond : ast->where.conditions) {
                    if (cond.column == idxColNames[ci]) {
                        if (cond.op == "=" || cond.op == "==") {
                            matchedPrefix = ci + 1;
                            foundCol = true;
                            break;
                        } else if (ci == static_cast<int>(colCount) - 1 &&
                                   (cond.op == "<" || cond.op == ">" || cond.op == "<=" || cond.op == ">=")) {
                            // 仅最后一列支持范围
                            matchedPrefix = ci + 1;
                            allEquality = false;
                            foundCol = true;
                            break;
                        } else if (cond.op == "<" || cond.op == ">" || cond.op == "<=" || cond.op == ">=") {
                            // 非末尾列做范围 → 不支持（忽略此索引）
                            foundCol = true;
                            matchedPrefix = 0;
                            break;
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
                    default: { // VARCHAR, CHAR, TEXT, DATETIME
                        size_t bufLen = f.length > 512 ? 511 : f.length;
                        char* buf = new char[bufLen + 1]();
                        std::strncpy(buf, recordPtr + f.offset, bufLen);
                        row.push_back(std::string(buf));
                        delete[] buf;
                        break;
                    }
                }
                if (!foundCol || matchedPrefix == 0) break;
            }

            if (matchedPrefix == 0) continue;  // 前缀不匹配

            // 构建复合搜索 key
            uint32_t totalKS = IndexManager::GetCompositeKeySize(idxHdr, fields);
            std::vector<char> keyBuf(totalKS, 0);
            bool buildOk = true;
            for (int ci = 0; ci < matchedPrefix; ++ci) {
                // 找到匹配的条件值
                std::string condVal;
                std::string condOp;
                for (const auto& cond : ast->where.conditions) {
                    if (cond.column == idxColNames[ci]) {
                        condVal = cond.value;
                        condOp = cond.op;
                        break;
                    }
                }
                const ColumnDef& tgtCol = fields[indices[ci]];
                uint32_t colKS = IndexManager::GetColumnKeySize(tgtCol);
                uint32_t keyOff = 0;
                for (int p = 0; p < ci; ++p)
                    keyOff += IndexManager::GetColumnKeySize(fields[indices[p]]);

                DataType ct = static_cast<DataType>(tgtCol.type);
                if (ct == DataType::TYPE_INT || ct == DataType::TYPE_DATETIME || ct == DataType::TYPE_BOOLEAN) {
                    uint32_t v = static_cast<uint32_t>(std::atoi(condVal.c_str()));
                    std::memcpy(keyBuf.data() + keyOff, &v, colKS);
                } else if (ct == DataType::TYPE_FLOAT) {
                    float v = 0.0f; try { v = std::stof(condVal); } catch(...) {}
                    std::memcpy(keyBuf.data() + keyOff, &v, colKS);
                } else if (ct == DataType::TYPE_DOUBLE) {
                    double v = 0.0; try { v = std::stod(condVal); } catch(...) {}
                    std::memcpy(keyBuf.data() + keyOff, &v, colKS);
                } else {
                    size_t len = condVal.size();
                    if (len > colKS) len = colKS;
                    std::memcpy(keyBuf.data() + keyOff, condVal.c_str(), len);
            // ── WHERE 过滤 ──
            if (ast->where.hasWhere) {
                int colIndex = -1;
                DataType colType = DataType::TYPE_VARCHAR;
                std::string upperWhereCol = ast->where.column;
                for (auto& c : upperWhereCol) c = std::toupper(c);
                for (size_t ci = 0; ci < fields.size(); ++ci) {
                    std::string fieldNameStr = fields[ci].fieldName;
                    for (auto& c : fieldNameStr) c = std::toupper(c);
                    if (fieldNameStr == upperWhereCol) {
                        colIndex = static_cast<int>(ci);
                        colType = fields[ci].type;
                        break;
                    }
                }

                if (colIndex >= 0 && colIndex < static_cast<int>(row.size())) {
                    bool matched = evaluateWhere(row[colIndex], ast->where.op, ast->where.value, colType);
                    if (!matched) { recordsRead++; continue; }
                } else {
                    res.rows.clear(); recordsRead = 0;
                    res.msg = "Query OK: Empty set (unknown column '" + ast->where.column + "')";
                    BufferPool::ReleasePage(fd, pid);
                    FileManager::CloseFile(fd);
                    return res;
                }
                // 验证 buildOk 不需要额外操作
            }
            (void)buildOk;

            if (matchedPrefix == static_cast<int>(colCount) && allEquality) {
                // 全列等值 → 精确查找
                IndexManager::Lookup(idxName, keyBuf.data(), totalKS, matchedOffsets);
                std::cerr << "[DML] Composite index eq hit: " << idxName
                          << " (" << matchedOffsets.size() << " rows)\n";
            } else {
                // 前缀匹配（带或不带末列范围）→ 范围查找（>= 前缀key 且 < 前缀key+1）
                // 简化：对于全等值前缀，用等值查找；部分前缀用全表扫描
                // 此处退化为全表扫描，保留索引可用标记
                continue;  // 前缀部分匹配暂不优化，让后面的单列索引逻辑处理
            }
            usedIndex = true;
            break;
        }

        // ── 单列索引匹配（未命中复合索引时）──
        if (!usedIndex) {
        for (const auto& cond : ast->where.conditions) {
            bool isEquality = (cond.op == "=" || cond.op == "==");
            bool isRange = (cond.op == "<" || cond.op == ">" || cond.op == "<=" || cond.op == ">=");
            if (!isEquality && !isRange) continue;
            bool found = false;
            for (const auto& idxName : indexes) {
                IndexHeader idxHdr;
                if (DictManager::GetIndexHeader(idxName, idxHdr) != ErrorCode::DB_OK) continue;
                if (std::string(idxHdr.columnName) == cond.column) {
                    const ColumnDef& col = fields[idxHdr.columnIndex];
                    uint32_t ksz = (idxHdr.keyType == static_cast<uint32_t>(DataType::TYPE_INT) ||
                                    idxHdr.keyType == static_cast<uint32_t>(DataType::TYPE_DATETIME) ||
                                    idxHdr.keyType == static_cast<uint32_t>(DataType::TYPE_BOOLEAN))
                                       ? 4u : col.length;
                    std::vector<char> keyBuf(ksz, 0);
                    DataType t = static_cast<DataType>(col.type);
                    if (t == DataType::TYPE_INT || t == DataType::TYPE_DATETIME || t == DataType::TYPE_BOOLEAN) {
                        uint32_t v = static_cast<uint32_t>(std::atoi(cond.value.c_str()));
                        std::memcpy(keyBuf.data(), &v, sizeof(uint32_t));
                    } else if (t == DataType::TYPE_FLOAT) {
                        float v = 0.0f; try { v = std::stof(cond.value); } catch(...) {}
                        std::memcpy(keyBuf.data(), &v, sizeof(float));
                    } else if (t == DataType::TYPE_DOUBLE) {
                        double v = 0.0; try { v = std::stod(cond.value); } catch(...) {}
                        std::memcpy(keyBuf.data(), &v, sizeof(double));
                    } else {
                        size_t len = cond.value.size();
                        if (len > ksz) len = ksz;
                        std::memcpy(keyBuf.data(), cond.value.c_str(), len);
                        for (size_t z = len; z < ksz; ++z) keyBuf[z] = '\0';
                    }
                    if (isEquality) {
                        IndexManager::Lookup(idxName, keyBuf.data(), ksz, matchedOffsets);
                        std::cerr << "[DML] Index eq hit: " << idxName << " (" << matchedOffsets.size() << " rows)\n";
                    } else {
                        IndexManager::LookupRange(idxName, keyBuf.data(), ksz, idxHdr.keyType, cond.op, matchedOffsets);
                        std::cerr << "[DML] Index range hit: " << idxName << " " << cond.op << " (" << matchedOffsets.size() << " rows)\n";
                    }
                    usedIndex = true;
                    found = true;
                    break;
                }
            }
            if (found) break; // 只用第一个可用索引
        }
    } // end if (!usedIndex)
    } // end if (where.hasWhere && !conditions.empty())

    // 检查是否有 OR：有 OR 时索引缩小范围不可靠，退化为全表扫描
    if (usedIndex && ast->where.conditions.size() > 1) {
        for (auto& op : ast->where.logicOps) {
            if (op == LogicOp::OR) { usedIndex = false; matchedOffsets.clear(); break; }
            // ── 投影处理 ──
            std::vector<std::string> projectedRow;
            for (size_t idx : projectedIndices) {
                projectedRow.push_back(row[idx]);
            }
            res.rows.push_back(projectedRow);
            recordsRead++;
        }
    }

    if (usedIndex) {
        int fd;
        if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "r", fd)) {
            res.msg = "Query OK: Empty set"; return res;
        }
        uint32_t rpp = 4080 / header.recordSize; if (rpp == 0) rpp = 1;
        for (uint32_t off : matchedOffsets) {
            uint32_t idx = off / header.recordSize;
            uint32_t pid = idx / rpp;
            uint32_t pageOff = idx % rpp;
            void* pageData = BufferPool::GetPage(fd, pid);
            if (!pageData) continue;
            char* rp = static_cast<char*>(pageData) + pageOff * header.recordSize;
            std::vector<std::string> fullRow;
            for (const auto& f : fields) fullRow.push_back(readFieldValue(rp, f));
            // 索引只覆盖了第一个等值条件，剩余条件需要逐条验证
            if (!evaluateCompoundWhere(fullRow, ast->where, fields)) {
                BufferPool::ReleasePage(fd, pid);
                continue;
            }
            // 列投影
            std::vector<std::string> row;
            for (auto pi : projectedIndices) row.push_back(fullRow[pi]);
            res.rows.push_back(row);
            BufferPool::ReleasePage(fd, pid);
        }
        FileManager::CloseFile(fd);
    } else {
        int fd;
        if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "r", fd)) {
            res.msg = "Query OK: Empty set"; return res;
        }
        uint32_t recordsPerPage = 4080 / header.recordSize; if (recordsPerPage == 0) recordsPerPage = 1;
        uint32_t totalPages = (header.recordCount + recordsPerPage - 1) / recordsPerPage;
        uint32_t recordsRead = 0;

        for (uint32_t pid = 0; pid < totalPages; ++pid) {
            void* pageData = BufferPool::GetPage(fd, pid);
            if (!pageData) continue;
            for (uint32_t i = 0; i < recordsPerPage && recordsRead < header.recordCount; ++i) {
                char* recordPtr = static_cast<char*>(pageData) + i * header.recordSize;
                std::vector<std::string> fullRow;
                for (const auto& f : fields) {
                    switch (f.type) {
                        case DataType::TYPE_INT: { int32_t val; std::memcpy(&val, recordPtr + f.offset, 4); fullRow.push_back(std::to_string(val)); break; }
                        case DataType::TYPE_FLOAT: { float val; std::memcpy(&val, recordPtr + f.offset, 4); std::ostringstream oss; oss << val; fullRow.push_back(oss.str()); break; }
                        case DataType::TYPE_DOUBLE: { double val; std::memcpy(&val, recordPtr + f.offset, 8); std::ostringstream oss; oss << val; fullRow.push_back(oss.str()); break; }
                        case DataType::TYPE_BOOLEAN: fullRow.push_back(boolToString(*(recordPtr + f.offset))); break;
                        default: { size_t bufLen = f.length > 512 ? 511 : f.length; char* buf = new char[bufLen + 1](); std::strncpy(buf, recordPtr + f.offset, bufLen); fullRow.push_back(std::string(buf)); delete[] buf; break; }
                    }
                }
                if (ast->where.hasWhere) {
                    if (!evaluateCompoundWhere(fullRow, ast->where, fields)) { recordsRead++; continue; }
                }
                // 列投影
                std::vector<std::string> row;
                for (auto pi : projectedIndices) row.push_back(fullRow[pi]);
                res.rows.push_back(row); recordsRead++;
            }
            BufferPool::ReleasePage(fd, pid);
        }
        FileManager::CloseFile(fd);
    }
    res.msg = "Query OK: " + std::to_string(res.rows.size()) + " row(s) in set";
#endif
    return res;
}

// ─── UPDATE ──────────────────────────────────────────────
ExecuteResult DMLExecutor::updateRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.msg = "Query OK: Rows matched: 1  Changed: 1  Warnings: 0";
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) { res.error = 1; res.msg = "Error: Table not found."; return res; }

    int setColIndex = -1;
    std::string upperCol = ast->columns[0];
    for (auto& c : upperCol) c = std::toupper(c);
    for (size_t i = 0; i < fields.size(); ++i) {
        std::string fieldNameStr = fields[i].fieldName;
        for (auto& c : fieldNameStr) c = std::toupper(c);
        if (fieldNameStr == upperCol) { setColIndex = static_cast<int>(i); break; }
    }
    if (setColIndex < 0) { res.error = 1; res.msg = "Error: Unknown column '" + ast->columns[0] + "'."; return res; }
    DataType setColType = fields[setColIndex].type;

    std::string trdFile = DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd";
    int fd;
    if (!FileManager::OpenFile(trdFile, "rw", fd)) { res.error = 1; res.msg = "Error: Cannot open data file."; return res; }

    uint32_t rpp = 4080 / header.recordSize; if (!rpp) rpp = 1;
    uint32_t totalPages = (header.recordCount + rpp - 1) / rpp;
    uint32_t rowsChanged = 0;

    for (uint32_t pid = 0; pid < totalPages; ++pid) {
        void* pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) continue;
        for (uint32_t ri = 0; ri < rpp && (pid * rpp + ri) < header.recordCount; ++ri) {
            char* rp = static_cast<char*>(pageData) + ri * header.recordSize;
            if (ast->where.hasWhere) {
                std::vector<std::string> row;
                for (const auto& f : fields) row.push_back(readFieldValue(rp, f));
                if (!evaluateCompoundWhere(row, ast->where, fields)) continue;
                int whereIdx = -1;
                DataType colType;
                std::string upperWhereCol = ast->where.column;
                for (auto& c : upperWhereCol) c = std::toupper(c);
                for (size_t ci = 0; ci < fields.size(); ++ci) {
                    std::string fieldNameStr = fields[ci].fieldName;
                    for (auto& c : fieldNameStr) c = std::toupper(c);
                    if (fieldNameStr == upperWhereCol) {
                        whereIdx = static_cast<int>(ci); colType = fields[ci].type; break;
                    }
                }
                if (whereIdx >= 0) {
                    std::string fieldValue = readFieldValue(rp, fields[whereIdx]);
                    bool match = evaluateWhere(fieldValue, ast->where.op, ast->where.value, colType);
                    if (!match)
                        continue; // 不匹配，跳过
                } else continue;
            }
            uint32_t recIdx = pid * rpp + ri;
            uint32_t recOffset = recIdx * header.recordSize;
            std::vector<char> oldRecord(header.recordSize, 0);
            std::memcpy(oldRecord.data(), rp, header.recordSize);

            switch (setColType) {
                case DataType::TYPE_INT: { int32_t val = 0; try { val = std::stoi(ast->values[0]); } catch(...) {} std::memcpy(rp + fields[setColIndex].offset, &val, 4); break; }
                case DataType::TYPE_FLOAT: { float val = 0.0f; try { val = std::stof(ast->values[0]); } catch(...) {} std::memcpy(rp + fields[setColIndex].offset, &val, 4); break; }
                case DataType::TYPE_DOUBLE: { double val = 0.0; try { val = std::stod(ast->values[0]); } catch(...) {} std::memcpy(rp + fields[setColIndex].offset, &val, 8); break; }
                case DataType::TYPE_BOOLEAN: { uint8_t bval = parseBool(ast->values[0]) ? 1 : 0; *(rp + fields[setColIndex].offset) = bval; break; }
                default: { std::strncpy(rp + fields[setColIndex].offset, ast->values[0].c_str(), fields[setColIndex].length - 1); rp[fields[setColIndex].offset + fields[setColIndex].length - 1] = '\0'; break; }
            }
            std::vector<char> newRecord(header.recordSize, 0);
            std::memcpy(newRecord.data(), rp, header.recordSize);
            ErrorCode idxErr = IndexManager::UpdateEntry(ast->tbl, recOffset, oldRecord.data(), newRecord.data(), fields);
            if (idxErr != ErrorCode::DB_OK) {
                std::cerr << "[DML] Warning: Index update failed: " << getErrorMessage(idxErr) << "\n";
            }
            rowsChanged++;
        }
        BufferPool::MarkDirty(fd, pid);
        BufferPool::ReleasePage(fd, pid);
    }
    FileManager::CloseFile(fd);
    if (rowsChanged > 0) {
        header.modifyTime = static_cast<uint32_t>(std::time(nullptr));
        FileManager::writeStruct(DictManager::GetCurrentDB() + "/" + ast->tbl + ".tb", header, 0);
    }
    res.msg = "Query OK: Rows matched: " + std::to_string(rowsChanged) + "  Changed: " + std::to_string(rowsChanged) + "  Warnings: 0";
#endif
    return res;
}

// ─── DELETE ──────────────────────────────────────────────
ExecuteResult DMLExecutor::deleteRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    res.msg = "Query OK: 1 row deleted";
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) { res.error = 1; res.msg = "Error: Table not found."; return res; }

    std::string trdFile = DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd";
    int fd;
    if (!FileManager::OpenFile(trdFile, "rw", fd)) { res.error = 1; res.msg = "Error: Cannot open data file."; return res; }

    uint32_t rpp = 4080 / header.recordSize; if (!rpp) rpp = 1;
    uint32_t totalRecs = header.recordCount;
    std::vector<uint32_t> toDelete;

    for (uint32_t idx = 0; idx < totalRecs; ++idx) {
        uint32_t pid = idx / rpp;
        uint32_t offset = (idx % rpp) * header.recordSize;
        void* pg = BufferPool::GetPage(fd, pid);
        if (!pg) continue;
        char* rp = static_cast<char*>(pg) + offset;
        bool matched = true;
        if (ast->where.hasWhere) {
            std::vector<std::string> row;
            for (const auto& f : fields) row.push_back(readFieldValue(rp, f));
            matched = evaluateCompoundWhere(row, ast->where, fields);
        }
        if (matched) {
            toDelete.push_back(idx);
            uint32_t recOffset = idx * header.recordSize;
            ErrorCode idxErr = IndexManager::DeleteEntry(ast->tbl, recOffset, rp, fields);
            if (idxErr != ErrorCode::DB_OK) {
                std::cerr << "[DML] Warning: Index delete failed: " << getErrorMessage(idxErr) << "\n";
            }
            int whereIdx = -1;
            DataType colType;
                std::string upperWhereCol = ast->where.column;
                for (auto& c : upperWhereCol) c = std::toupper(c);
                for (size_t ci = 0; ci < fields.size(); ++ci) {
                    std::string fieldNameStr = fields[ci].fieldName;
                    for (auto& c : fieldNameStr) c = std::toupper(c);
                    if (fieldNameStr == upperWhereCol) {
                        whereIdx = static_cast<int>(ci); colType = fields[ci].type; break;
                    }
                }
            if (whereIdx >= 0) {
                std::string fieldValue = readFieldValue(rp, fields[whereIdx]);
                matched = evaluateWhere(fieldValue, ast->where.op, ast->where.value, colType);
            } else matched = false;
        }
        BufferPool::ReleasePage(fd, pid);
    }

    for (int di = static_cast<int>(toDelete.size()) - 1; di >= 0; --di) {
        uint32_t delIdx = toDelete[di];
        for (uint32_t i = delIdx; i < totalRecs - 1; ++i) {
            uint32_t srcPid = (i + 1) / rpp, srcOff = ((i + 1) % rpp) * header.recordSize;
            uint32_t dstPid = i / rpp, dstOff = (i % rpp) * header.recordSize;
            void* srcPg = BufferPool::GetPage(fd, srcPid);
            char temp[4096]; std::memcpy(temp, static_cast<char*>(srcPg) + srcOff, header.recordSize);
            BufferPool::ReleasePage(fd, srcPid);
            void* dstPg = BufferPool::GetPage(fd, dstPid);
            std::memcpy(static_cast<char*>(dstPg) + dstOff, temp, header.recordSize);
            BufferPool::MarkDirty(fd, dstPid); BufferPool::ReleasePage(fd, dstPid);
        }
        totalRecs--;
    }

    header.recordCount = totalRecs;
    header.modifyTime = static_cast<uint32_t>(std::time(nullptr));
    FileManager::writeStruct(DictManager::GetCurrentDB() + "/" + ast->tbl + ".tb", header, 0);
    FileManager::CloseFile(fd);

    // DELETE 紧缩后，非删除记录的偏移量已变化，需重建所有索引
    if (!toDelete.empty()) {
        ErrorCode rebuildErr = IndexManager::RebuildAllIndexes(ast->tbl);
        if (rebuildErr != ErrorCode::DB_OK) {
            std::cerr << "[DML] Warning: Index rebuild after DELETE failed\n";
        }
    }
    res.msg = "Query OK: " + std::to_string(toDelete.size()) + " row(s) deleted";
#endif
    return res;
}

ExecuteResult DMLExecutor::executeBegin(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Transaction started (Stub).";
    return res;
}

ExecuteResult DMLExecutor::executeCommit(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Transaction committed (Stub).";
    return res;
}

ExecuteResult DMLExecutor::executeRollback(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Transaction rolled back (Stub).";
    return res;
}
