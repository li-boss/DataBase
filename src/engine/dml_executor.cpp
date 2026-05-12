// src/engine/dml_executor.cpp
#include "engine/dml_executor.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/index_manager.h"
#include "../../include/common/db_structs.h"
#include "../../include/common/db_errors.h"

#include <filesystem>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

extern std::string g_currentDbDir;

// ─── 辅助：将字符串值按字段类型序列化为二进制 ──────────
// outBuf 需预先分配 recordSize 字节并清零
static void serializeValue(char* outBuf,
                           const ColumnDef& col,
                           const std::string& value) {
    DataType t = static_cast<DataType>(col.type);
    if (t == DataType::TYPE_INT || t == DataType::TYPE_DATETIME || t == DataType::TYPE_BOOLEAN) {
        // 整数/时间/布尔 统一存为 uint32_t
        uint32_t v = static_cast<uint32_t>(std::atoi(value.c_str()));
        std::memcpy(outBuf + col.offset, &v, sizeof(uint32_t));
    } else {
        // TYPE_CHAR / TYPE_VARCHAR 存为定长字符串
        size_t len = value.size();
        if (len > col.length) len = col.length;
        std::memcpy(outBuf + col.offset, value.c_str(), len);
    }
}

// ─── INSERT ──────────────────────────────────────────────
ExecuteResult DMLExecutor::insertRecord(const ASTNode* ast) {
    ExecuteResult res;

    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, hdr, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = static_cast<int>(err);
        res.msg = "[DML] Failed to load table: " + std::string(getErrorMessage(err));
        return res;
    }

    // 序列化记录
    std::vector<char> recordBuf(hdr.recordSize, 0);
    size_t valIdx = 0;
    for (uint32_t i = 0; i < hdr.fieldCount && valIdx < ast->values.size(); ++i) {
        serializeValue(recordBuf.data(), fields[i], ast->values[valIdx]);
        ++valIdx;
    }

    // 追加到 .trd 文件
    std::string dbDir = g_currentDbDir;
    std::string trdPath = dbDir + "/" + ast->tbl + ".trd";
    uint32_t recordOffset = hdr.recordCount * hdr.recordSize;

    if (!FileManager::appendBlock(trdPath, recordBuf.data(), hdr.recordSize)) {
        res.error = static_cast<int>(ErrorCode::DB_ERR_RECORD_INSERT_FAILED);
        res.msg = "[DML] Failed to write .trd file";
        return res;
    }

    // 更新 .tb 中的 recordCount
    DictManager::updateRecordCount(ast->tbl, hdr.recordCount + 1);

    // 同步索引
    std::vector<std::string> indexes;
    DictManager::ListIndexes(ast->tbl, indexes);
    for (const auto& idxName : indexes) {
        IndexHeader idxHdr;
        if (DictManager::GetIndexHeader(idxName, idxHdr) != ErrorCode::DB_OK) continue;
        const ColumnDef& col = fields[idxHdr.columnIndex];
        const char* keyData = recordBuf.data() + col.offset;
        IndexManager::InsertEntry(idxName, keyData, recordOffset);
    }

    res.error = 0;
    res.msg = "Query OK, 1 row inserted (offset=" + std::to_string(recordOffset) + ")";
    return res;
}

// ─── SELECT ──────────────────────────────────────────────
ExecuteResult DMLExecutor::selectRecord(const ASTNode* ast) {
    ExecuteResult res;

    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, hdr, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = static_cast<int>(err);
        res.msg = "[DML] Failed to load table: " + std::string(getErrorMessage(err));
        return res;
    }

    // 设置结果头
    res.headers.clear();
    for (const auto& f : fields) {
        res.headers.push_back(std::string(f.fieldName));
    }

    std::string dbDir = g_currentDbDir;
    std::string trdPath = dbDir + "/" + ast->tbl + ".trd";
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) {
        res.error = static_cast<int>(ErrorCode::DB_ERR_FILE_OPEN_FAILED);
        res.msg = "[DML] Cannot open .trd file";
        return res;
    }

    // 尝试用索引加速
    bool usedIndex = false;
    std::vector<uint32_t> matchedOffsets;
    if (ast->where.hasWhere) {
        std::vector<std::string> indexes;
        DictManager::ListIndexes(ast->tbl, indexes);
        for (const auto& idxName : indexes) {
            IndexHeader idxHdr;
            if (DictManager::GetIndexHeader(idxName, idxHdr) != ErrorCode::DB_OK) continue;
            if (std::string(idxHdr.columnName) == ast->where.column) {
                // 序列化查找键
                const ColumnDef& col = fields[idxHdr.columnIndex];
                std::vector<char> keyBuf(idxHdr.keySize, 0);
                serializeValue(keyBuf.data(), col, ast->where.value);
                IndexManager::Lookup(idxName, keyBuf.data(), matchedOffsets);
                usedIndex = true;
                std::cerr << "[DML] Index hit: " << idxName
                          << " (" << matchedOffsets.size() << " rows)\n";
                break;
            }
        }
    }

    // 读取记录
    res.rows.clear();
    if (usedIndex && !matchedOffsets.empty()) {
        for (uint32_t off : matchedOffsets) {
            ifs.seekg(off);
            std::vector<char> rec(hdr.recordSize);
            ifs.read(rec.data(), hdr.recordSize);
            if (!ifs) break;
            std::vector<std::string> row;
            for (const auto& f : fields) {
                DataType ft = static_cast<DataType>(f.type);
            if (ft == DataType::TYPE_INT || ft == DataType::TYPE_DATETIME || ft == DataType::TYPE_BOOLEAN) {
                    uint32_t v;
                    std::memcpy(&v, rec.data() + f.offset, sizeof(uint32_t));
                    row.push_back(std::to_string(v));
                } else {
                    row.push_back(std::string(rec.data() + f.offset, f.length));
                }
            }
            res.rows.push_back(row);
        }
    } else {
        // 全表扫描
        for (uint32_t i = 0; i < hdr.recordCount; ++i) {
            std::vector<char> rec(hdr.recordSize);
            ifs.read(rec.data(), hdr.recordSize);
            if (!ifs) break;

            bool match = true;
            if (ast->where.hasWhere) {
                match = false;
                for (const auto& f : fields) {
                    if (std::string(f.fieldName) == ast->where.column) {
                        DataType ft = static_cast<DataType>(f.type);
                        if (ft == DataType::TYPE_INT || ft == DataType::TYPE_DATETIME || ft == DataType::TYPE_BOOLEAN) {
                            uint32_t v;
                            std::memcpy(&v, rec.data() + f.offset, sizeof(uint32_t));
                            uint32_t target = static_cast<uint32_t>(std::atoi(ast->where.value.c_str()));
                            match = (v == target);
                        }
                        break;
                    }
                }
            }
            if (!match) continue;

            std::vector<std::string> row;
            for (const auto& f : fields) {
                DataType ft = static_cast<DataType>(f.type);
            if (ft == DataType::TYPE_INT || ft == DataType::TYPE_DATETIME || ft == DataType::TYPE_BOOLEAN) {
                    uint32_t v;
                    std::memcpy(&v, rec.data() + f.offset, sizeof(uint32_t));
                    row.push_back(std::to_string(v));
                } else {
                    row.push_back(std::string(rec.data() + f.offset, f.length));
                }
            }
            res.rows.push_back(row);
        }
    }

    res.error = 0;
    res.msg = "Query OK, " + std::to_string(res.rows.size()) + " rows in set";
    return res;
}

// ─── DELETE ──────────────────────────────────────────────
ExecuteResult DMLExecutor::deleteRecord(const ASTNode* ast) {
    ExecuteResult res;

    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, hdr, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = static_cast<int>(err);
        res.msg = "[DML] Failed to load table: " + std::string(getErrorMessage(err));
        return res;
    }

    std::string dbDir = g_currentDbDir;
    std::string trdPath = dbDir + "/" + ast->tbl + ".trd";
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) {
        res.error = static_cast<int>(ErrorCode::DB_ERR_FILE_OPEN_FAILED);
        res.msg = "[DML] Cannot open .trd file";
        return res;
    }

    // 第一版：重建 .trd 文件
    std::vector<std::vector<char>> kept;
    uint32_t deletedCount = 0;

    for (uint32_t i = 0; i < hdr.recordCount; ++i) {
        std::vector<char> rec(hdr.recordSize);
        uint32_t recordOffset = i * hdr.recordSize;
        ifs.seekg(recordOffset);
        ifs.read(rec.data(), hdr.recordSize);
        if (!ifs) break;

        bool shouldDelete = false;
        if (!ast->where.hasWhere) {
            shouldDelete = true;
        } else {
            for (const auto& f : fields) {
                if (std::string(f.fieldName) == ast->where.column) {
                    DataType ft = static_cast<DataType>(f.type);
                    if (ft == DataType::TYPE_INT || ft == DataType::TYPE_DATETIME || ft == DataType::TYPE_BOOLEAN) {
                        uint32_t v;
                        std::memcpy(&v, rec.data() + f.offset, sizeof(uint32_t));
                        uint32_t target = static_cast<uint32_t>(std::atoi(ast->where.value.c_str()));
                        shouldDelete = (v == target);
                    }
                    break;
                }
            }
        }

        if (shouldDelete) {
            // 同步删除索引条目
            std::vector<std::string> indexes;
            DictManager::ListIndexes(ast->tbl, indexes);
            for (const auto& idxName : indexes) {
                IndexHeader idxHdr;
                if (DictManager::GetIndexHeader(idxName, idxHdr) != ErrorCode::DB_OK) continue;
                const ColumnDef& col = fields[idxHdr.columnIndex];
                const char* keyData = rec.data() + col.offset;
                IndexManager::DeleteEntry(idxName, keyData, recordOffset);
            }
            ++deletedCount;
        } else {
            kept.push_back(std::move(rec));
        }
    }

    // 重写 .trd 文件
    if (deletedCount > 0) {
        std::ofstream ofs(trdPath, std::ios::binary);
        if (!ofs.is_open()) {
            res.error = static_cast<int>(ErrorCode::DB_ERR_FILE_WRITE_FAILED);
            res.msg = "[DML] Failed to rewrite .trd file";
            return res;
        }
        for (const auto& rec : kept) {
            ofs.write(rec.data(), rec.size());
        }
        fs::resize_file(trdPath, kept.size() * hdr.recordSize);
        DictManager::updateRecordCount(ast->tbl, hdr.recordCount - deletedCount);
    }

    res.error = 0;
    res.msg = "Query OK, " + std::to_string(deletedCount) + " rows deleted";
    return res;
}

// ─── UPDATE ──────────────────────────────────────────────
ExecuteResult DMLExecutor::updateRecord(const ASTNode* ast) {
    ExecuteResult res;
    res.error = 0;
    res.msg = "[DML] UPDATE not yet implemented (P2)";
    return res;
}
