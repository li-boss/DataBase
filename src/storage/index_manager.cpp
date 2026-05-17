// src/storage/index_manager.cpp
#include "../../include/storage/index_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/buffer_pool.h"

#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

// 直接访问 DictManager 的全局数据库路径
extern std::string g_currentDbDir;

// ─── CreateIndex（单列）────────────────────────────────
ErrorCode IndexManager::CreateIndex(const std::string& indexName,
                                       const std::string& tableName,
                                       const std::string& columnName) {
    std::vector<std::string> names = { columnName };
    return CreateIndex(indexName, tableName, names);
}

// ─── CreateIndex（复合索引入口）───────────────────────
ErrorCode IndexManager::CreateIndex(const std::string& indexName,
                                       const std::string& tableName,
                                       const std::vector<std::string>& columnNames) {
    // 1. 获取表结构
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(tableName, hdr, fields);
    if (err != ErrorCode::DB_OK) return err;

    // 2. 解析各列的 columnIndex 和 keyType
    std::vector<uint32_t> colIndices;
    std::vector<uint32_t> keyTypes;
    for (const auto& colName : columnNames) {
        int colIdx = -1;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (std::string(fields[i].fieldName) == colName) {
                colIdx = static_cast<int>(i);
                break;
            }
        }
        if (colIdx < 0) return ErrorCode::DB_ERR_INDEX_NOT_FOUND;
        colIndices.push_back(static_cast<uint32_t>(colIdx));
        keyTypes.push_back(static_cast<uint32_t>(fields[colIdx].type));
    }

    // 3. 调用 DictManager 创建索引元数据 + 空 .idx 文件
    if (columnNames.size() == 1) {
        // 单列走旧接口（向后兼容）
        err = DictManager::CreateIndex(indexName, tableName, columnNames[0],
                                         colIndices[0], keyTypes[0]);
    } else {
        // 复合索引走新接口
        err = DictManager::CreateIndex(indexName, tableName, columnNames,
                                         colIndices, keyTypes, fields);
    }
    if (err != ErrorCode::DB_OK) return err;

    // 4. 获取索引头信息，构建索引数据（扫描全表）
    IndexHeader idxHdr;
    err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    std::cerr << "[IdxMgr] Building index: " << indexName
              << " (" << columnNames.size() << " columns)\n";
    return buildIndexData(hdr, fields, idxHdr);
}

// ─── DropIndex ──────────────────────────────────────────
ErrorCode IndexManager::DropIndex(const std::string& indexName) {
    return DictManager::DropIndex(indexName);
}

// ─── ListIndexes ────────────────────────────────────────
ErrorCode IndexManager::ListIndexes(const std::string& tableName,
                                        std::vector<std::string>& outNames) {
    return DictManager::ListIndexes(tableName, outNames);
}

// ─── Lookup ────────────────────────────────────────────
ErrorCode IndexManager::Lookup(const std::string& indexName,
                              const void* keyData,
                              uint32_t keySize,
                              std::vector<uint32_t>& outOffsets) {
    std::string idxPath = g_currentDbDir + "/" + indexName + ".idx";
    bool found = FileManager::lookupIndexEntry(idxPath, keyData, keySize, outOffsets);
    (void)found;
    return ErrorCode::DB_OK;
}

// ─── LookupRange ────────────────────────────────────────
ErrorCode IndexManager::LookupRange(const std::string& indexName,
                                   const void* keyData,
                                   uint32_t keySize,
                                   uint32_t keyType,
                                   const std::string& op,
                                   std::vector<uint32_t>& outOffsets) {
    std::string idxPath = g_currentDbDir + "/" + indexName + ".idx";
    bool found = FileManager::lookupIndexRange(idxPath, keyData, keySize, keyType, op, outOffsets);
    (void)found;
    return ErrorCode::DB_OK;
}

// ─── InsertEntry（表级：INSERT 后调用）──────────────────
ErrorCode IndexManager::InsertEntry(const std::string& tableName,
                                       uint32_t recordOffset,
                                       const void* recordData,
                                       const std::vector<ColumnDef>& fields) {
    std::vector<std::string> indexes;
    ErrorCode err = DictManager::ListIndexes(tableName, indexes);
    if (err != ErrorCode::DB_OK) return err;

    for (const auto& idxName : indexes) {
        IndexHeader idxHdr;
        err = DictManager::GetIndexHeader(idxName, idxHdr);
        if (err != ErrorCode::DB_OK) continue;

        uint32_t colCount = GetCompositeColumnCount(idxHdr);
        uint32_t ksz;
        std::vector<char> keyBuf;
        const char* keyData;

        if (colCount <= 1) {
            const ColumnDef& col = fields[idxHdr.columnIndex];
            ksz = GetColumnKeySize(col);
            keyData = static_cast<const char*>(recordData) + col.offset;
        } else {
            ksz = GetCompositeKeySize(idxHdr, fields);
            keyBuf.resize(ksz);
            BuildCompositeKey(idxHdr, fields, recordData, keyBuf.data());
            keyData = keyBuf.data();
        }

        std::string idxPath = g_currentDbDir + "/" + idxName + ".idx";
        if (!FileManager::appendIndexEntry(idxPath, keyData, ksz, recordOffset)) {
            return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
        }

        // BPlusTree 已自行维护 IndexHeader（entryCount/rootPageId/nextPageId）
        // 从磁盘刷新 DictManager 缓存
        IndexHeader updatedHdr;
        if (FileManager::readIndexHeader(idxPath, updatedHdr)) {
            DictManager::UpdateIndexCache(updatedHdr);
        }
    }
    return ErrorCode::DB_OK;
}

// ─── DeleteEntry（表级：DELETE 后调用）──────────────────
ErrorCode IndexManager::DeleteEntry(const std::string& tableName,
                                        uint32_t recordOffset,
                                        const void* recordData,
                                        const std::vector<ColumnDef>& fields) {
    std::vector<std::string> indexes;
    ErrorCode err = DictManager::ListIndexes(tableName, indexes);
    if (err != ErrorCode::DB_OK) return err;

    for (const auto& idxName : indexes) {
        IndexHeader idxHdr;
        err = DictManager::GetIndexHeader(idxName, idxHdr);
        if (err != ErrorCode::DB_OK) continue;

        uint32_t colCount = GetCompositeColumnCount(idxHdr);
        uint32_t ksz;
        std::vector<char> keyBuf;
        const char* keyData;

        if (colCount <= 1) {
            const ColumnDef& col = fields[idxHdr.columnIndex];
            ksz = GetColumnKeySize(col);
            keyData = static_cast<const char*>(recordData) + col.offset;
        } else {
            ksz = GetCompositeKeySize(idxHdr, fields);
            keyBuf.resize(ksz);
            BuildCompositeKey(idxHdr, fields, recordData, keyBuf.data());
            keyData = keyBuf.data();
        }

        std::string idxPath = g_currentDbDir + "/" + idxName + ".idx";
        FileManager::removeIndexEntry(idxPath, keyData, ksz, recordOffset);

        // BPlusTree 已自行维护 IndexHeader
        IndexHeader updatedHdr;
        if (FileManager::readIndexHeader(idxPath, updatedHdr)) {
            DictManager::UpdateIndexCache(updatedHdr);
        }
    }
    return ErrorCode::DB_OK;
}

// ─── UpdateEntry（表级：UPDATE 后调用，P2）────────
ErrorCode IndexManager::UpdateEntry(const std::string& tableName,
                                       uint32_t recordOffset,
                                       const void* oldData,
                                       const void* newData,
                                       const std::vector<ColumnDef>& fields) {
    ErrorCode err = DeleteEntry(tableName, recordOffset, oldData, fields);
    if (err != ErrorCode::DB_OK) return err;
    return InsertEntry(tableName, recordOffset, newData, fields);
}

// ─── buildIndexData（内部辅助：为单个索引扫描全表插入条目）──
ErrorCode IndexManager::buildIndexData(const struct TableHeader& hdr,
                                        const std::vector<ColumnDef>& fields,
                                        const struct IndexHeader& idxHdr) {
    // ── 刷盘：确保 BufferPool 中脏页写回磁盘，避免 ifstream 读到旧数据 ──
    BufferPool::flushAll();

    std::string trdPath = g_currentDbDir + "/" + std::string(idxHdr.tableName) + ".trd";
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[IdxMgr] Cannot open .trd file: " << trdPath << "\n";
        return ErrorCode::DB_ERR_FILE_OPEN_FAILED;
    }

    const ColumnDef& col = fields[idxHdr.columnIndex];
    uint32_t colCount = GetCompositeColumnCount(idxHdr);
    uint32_t ks;
    if (colCount <= 1) {
        ks = GetColumnKeySize(col);
    } else {
        ks = GetCompositeKeySize(idxHdr, fields);
    }
    std::vector<char> keyBuf(ks);
    std::vector<char> recBuf(hdr.recordSize);  // 复合索引需要整条记录
    uint32_t recordOffset = 0;

    for (uint32_t i = 0; i < hdr.recordCount; ++i) {
        if (colCount <= 1) {
            // 单列索引：只读目标列
            ifs.seekg(recordOffset + col.offset);
            ifs.read(keyBuf.data(), ks);
        } else {
            // 复合索引：读整条记录，再提取复合 key
            ifs.seekg(recordOffset);
            ifs.read(recBuf.data(), hdr.recordSize);
            BuildCompositeKey(idxHdr, fields, recBuf.data(), keyBuf.data());
        }
        if (!ifs.good()) break;

        std::string idxPath = g_currentDbDir + "/" + std::string(idxHdr.indexName) + ".idx";
        if (!FileManager::appendIndexEntry(idxPath, keyBuf.data(), ks, recordOffset)) {
            return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
        }

        recordOffset += hdr.recordSize;
    }

    // BPlusTree 已自行维护 entryCount；从磁盘刷新缓存
    std::string idxPath = g_currentDbDir + "/" + std::string(idxHdr.indexName) + ".idx";
    IndexHeader idxHdrToWrite;
    if (FileManager::readIndexHeader(idxPath, idxHdrToWrite)) {
        DictManager::UpdateIndexCache(idxHdrToWrite);
        std::cerr << "[IdxMgr] Index built: " << idxHdr.indexName
                  << " with " << idxHdrToWrite.entryCount << " entries\n";
    } else {
        std::cerr << "[IdxMgr] Index built: " << idxHdr.indexName << "\n";
    }
    return ErrorCode::DB_OK;
}

// ─── 复合索引工具函数 ──────────────────────────────────

uint32_t IndexManager::GetColumnKeySize(const ColumnDef& col) {
    uint32_t t = static_cast<uint32_t>(col.type);
    if (t == static_cast<uint32_t>(DataType::TYPE_INT) ||
        t == static_cast<uint32_t>(DataType::TYPE_DATETIME) ||
        t == static_cast<uint32_t>(DataType::TYPE_BOOLEAN))
        return 4u;
    if (t == static_cast<uint32_t>(DataType::TYPE_FLOAT))
        return 4u;
    if (t == static_cast<uint32_t>(DataType::TYPE_DOUBLE))
        return 8u;
    // CHAR / VARCHAR / TEXT / DATETIME-string: 使用字段定义长度
    return col.length;
}

uint32_t IndexManager::GetCompositeKeySize(const IndexHeader& idxHdr,
                                              const std::vector<ColumnDef>& fields) {
    uint32_t colCount = GetCompositeColumnCount(idxHdr);
    if (colCount <= 1) {
        // 单列索引：直接读 reserved[2]（B+ Tree 缓存的 keySize）
        return idxHdr.reserved[2];
    }
    // 复合索引：累加各列 key 长度
    uint32_t indices[4];
    GetCompositeColumnIndices(idxHdr, indices);
    uint32_t total = 0;
    for (uint32_t i = 0; i < colCount; ++i) {
        total += GetColumnKeySize(fields[indices[i]]);
    }
    return total;
}

void IndexManager::BuildCompositeKey(const IndexHeader& idxHdr,
                                       const std::vector<ColumnDef>& fields,
                                       const void* recordData,
                                       char* outBuf) {
    uint32_t colCount = GetCompositeColumnCount(idxHdr);
    uint32_t indices[4];
    GetCompositeColumnIndices(idxHdr, indices);
    const char* rec = static_cast<const char*>(recordData);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < colCount; ++i) {
        const ColumnDef& col = fields[indices[i]];
        uint32_t ksz = GetColumnKeySize(col);
        std::memcpy(outBuf + pos, rec + col.offset, ksz);
        pos += ksz;
    }
}

uint32_t IndexManager::GetCompositeColumnCount(const IndexHeader& idxHdr) {
    uint32_t cnt = idxHdr.reserved[3];
    return (cnt > 0) ? cnt : 1u;  // 旧索引 reserved[3]==0，默认1列
}

void IndexManager::GetCompositeColumnIndices(const IndexHeader& idxHdr,
                                               uint32_t* outIndices) {
    outIndices[0] = idxHdr.columnIndex;
    outIndices[1] = idxHdr.reserved[4];
    outIndices[2] = idxHdr.reserved[5];
    outIndices[3] = idxHdr.reserved[6];
}

void IndexManager::GetCompositeColumnNames(const IndexHeader& idxHdr,
                                              std::vector<std::string>& outNames) {
    outNames.clear();
    std::string full(idxHdr.columnName, MAX_NAME_LEN);
    // 截断到第一个 '\0'
    size_t nullPos = full.find('\0');
    if (nullPos != std::string::npos) full = full.substr(0, nullPos);
    // 按 '|' 拆分
    std::stringstream ss(full);
    std::string one;
    while (std::getline(ss, one, '|')) {
        if (!one.empty()) outNames.push_back(one);
    }
    // 如果无 '|'，则是单列索引，full 就是列名
    if (outNames.empty()) outNames.push_back(full);
}

// ─── RebuildAllIndexes（DELETE 紧缩后调用）───────────
ErrorCode IndexManager::RebuildAllIndexes(const std::string& tableName) {
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(tableName, hdr, fields);
    if (err != ErrorCode::DB_OK) return err;

    std::vector<std::string> indexes;
    err = DictManager::ListIndexes(tableName, indexes);
    if (err != ErrorCode::DB_OK) return err;

    for (const auto& idxName : indexes) {
        IndexHeader idxHdr;
        err = DictManager::GetIndexHeader(idxName, idxHdr);
        if (err != ErrorCode::DB_OK) continue;

        idxHdr.entryCount = 0;
        // 重置 B+ Tree 元数据，让 BPlusTree 构造时视为空树重建
        idxHdr.reserved[0] = 0;  // rootPageId = 0
        idxHdr.reserved[1] = 1;  // nextPageId = 1
        idxHdr.reserved[2] = 0;  // keySize（BPlusTree 构造时回填）
        std::string idxPath = g_currentDbDir + "/" + idxName + ".idx";
        // 截断 .idx 文件为仅含 IndexHeader（清除旧 B+ Tree 节点页）
        {
            std::ofstream truncateOfs(idxPath, std::ios::binary | std::ios::trunc);
            if (truncateOfs.is_open()) {
                truncateOfs.write(reinterpret_cast<const char*>(&idxHdr), sizeof(IndexHeader));
            }
        }

        err = buildIndexData(hdr, fields, idxHdr);
        if (err != ErrorCode::DB_OK) {
            std::cerr << "[IdxMgr] Warning: rebuild '" << idxName << "' failed\n";
        }
    }

    std::cerr << "[IdxMgr] Rebuilt " << indexes.size() << " indexes on: " << tableName << "\n";
    return ErrorCode::DB_OK;
}
