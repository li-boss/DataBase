// src/storage/index_manager.cpp
#include "../../include/storage/index_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"

#include <iostream>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// 直接访问 DictManager 的全局数据库路径
extern std::string g_currentDbDir;

// ─── CreateIndex ────────────────────────────────────────
ErrorCode IndexManager::CreateIndex(const std::string& indexName,
                                       const std::string& tableName,
                                       const std::string& columnName) {
    // 1. 获取表结构，确定 columnIndex / keyType / keySize
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(tableName, hdr, fields);
    if (err != ErrorCode::DB_OK) return err;

    int columnIndex = -1;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (std::string(fields[i].fieldName) == columnName) {
            columnIndex = static_cast<int>(i);
            break;
        }
    }
    if (columnIndex < 0) return ErrorCode::DB_ERR_INDEX_NOT_FOUND;

    uint32_t keyType = static_cast<uint32_t>(fields[columnIndex].type);
    uint32_t keySize =
        (keyType == static_cast<uint32_t>(DataType::TYPE_INT) ||
         keyType == static_cast<uint32_t>(DataType::TYPE_DATETIME) ||
         keyType == static_cast<uint32_t>(DataType::TYPE_BOOLEAN))
            ? 4u : fields[columnIndex].length;

    // 2. 调用 DictManager 创建索引元数据 + 空 .idx 文件
    err = DictManager::CreateIndex(indexName, tableName, columnName,
                                     static_cast<uint32_t>(columnIndex),
                                     keyType, keySize);
    if (err != ErrorCode::DB_OK) return err;

    // 3. 获取索引头信息，构建索引数据（扫描全表）
    IndexHeader idxHdr;
    err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    std::cerr << "[IdxMgr] Building index: " << indexName << "\n";
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

        const ColumnDef& col = fields[idxHdr.columnIndex];
        const char* keyData = static_cast<const char*>(recordData) + col.offset;

        std::string idxPath = g_currentDbDir + "/" + idxName + ".idx";
        if (!FileManager::appendIndexEntry(idxPath, keyData, idxHdr.keySize, recordOffset)) {
            return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
        }

        // 更新 entryCount
        idxHdr.entryCount++;
        FileManager::writeIndexHeader(idxPath, idxHdr);
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

        const ColumnDef& col = fields[idxHdr.columnIndex];
        const char* keyData = static_cast<const char*>(recordData) + col.offset;

        std::string idxPath = g_currentDbDir + "/" + idxName + ".idx";
        if (!FileManager::removeIndexEntry(idxPath, keyData, idxHdr.keySize)) {
            return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
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
    std::string trdPath = g_currentDbDir + "/" + std::string(idxHdr.tableName) + ".trd";
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[IdxMgr] Cannot open .trd file: " << trdPath << "\n";
        return ErrorCode::DB_ERR_FILE_OPEN_FAILED;
    }

    const ColumnDef& col = fields[idxHdr.columnIndex];
    std::vector<char> keyBuf(idxHdr.keySize);
    uint32_t recordOffset = 0;

    for (uint32_t i = 0; i < hdr.recordCount; ++i) {
        ifs.seekg(recordOffset + col.offset);
        ifs.read(keyBuf.data(), idxHdr.keySize);
        if (!ifs.good()) break;

        std::string idxPath = g_currentDbDir + "/" + std::string(idxHdr.indexName) + ".idx";
        if (!FileManager::appendIndexEntry(idxPath, keyBuf.data(), idxHdr.keySize, recordOffset)) {
            return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
        }

        recordOffset += hdr.recordSize;
    }

    // 更新 IndexHeader.entryCount
    IndexHeader idxHdrToWrite;
    DictManager::GetIndexHeader(std::string(idxHdr.indexName), idxHdrToWrite);
    idxHdrToWrite.entryCount = hdr.recordCount;
    std::string idxPath = g_currentDbDir + "/" + std::string(idxHdr.indexName) + ".idx";
    FileManager::writeIndexHeader(idxPath, idxHdrToWrite);

    std::cerr << "[IdxMgr] Index built: " << idxHdr.indexName
              << " with " << hdr.recordCount << " entries\n";
    return ErrorCode::DB_OK;
}
