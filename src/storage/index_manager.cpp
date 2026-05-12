// src/storage/index_manager.cpp
#include "../../include/storage/index_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"

#include <iostream>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// ─── CreateIndex ────────────────────────────────────────
ErrorCode IndexManager::CreateIndex(const std::string& indexName,
                                       const std::string& tableName,
                                       const std::string& columnName,
                                       uint32_t columnIndex,
                                       uint32_t keyType,
                                       uint32_t keySize) {
    // 1. 创建索引元数据（DictManager 会创建 .idx 文件并写入 IndexHeader）
    ErrorCode err = DictManager::CreateIndex(indexName, tableName,
                                               columnName, columnIndex,
                                               keyType, keySize);
    if (err != ErrorCode::DB_OK) return err;

    // 2. 获取表结构和索引头信息
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    err = DictManager::loadTable(tableName, hdr, fields);
    if (err != ErrorCode::DB_OK) return err;

    IndexHeader idxHdr;
    err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    // 3. 构建索引数据（扫描全表）
    return buildIndexData(hdr, fields, idxHdr);
}

// ─── DropIndex ──────────────────────────────────────────
ErrorCode IndexManager::DropIndex(const std::string& indexName) {
    return DictManager::DropIndex(indexName);
}

// ─── InsertEntry ────────────────────────────────────────
ErrorCode IndexManager::InsertEntry(const std::string& indexName,
                                       const void* keyData,
                                       uint32_t recordOffset) {
    // 获取索引头信息（含 keySize）
    IndexHeader idxHdr;
    ErrorCode err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    // 追加索引条目
    std::string idxPath = DictManager::GetCurrentDB() + "/" + indexName + ".idx";
    if (!FileManager::appendIndexEntry(idxPath, keyData, idxHdr.keySize, recordOffset)) {
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }

    // 更新 entryCount
    idxHdr.entryCount++;
    if (!FileManager::writeIndexHeader(idxPath, idxHdr)) {
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }

    return ErrorCode::DB_OK;
}

// ─── DeleteEntry ────────────────────────────────────────
ErrorCode IndexManager::DeleteEntry(const std::string& indexName,
                                       const void* keyData,
                                       uint32_t recordOffset) {
    // 第一版：删除所有匹配 keyData 的条目（假设唯一索引）
    IndexHeader idxHdr;
    ErrorCode err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    std::string idxPath = DictManager::GetCurrentDB() + "/" + indexName + ".idx";
    if (!FileManager::removeIndexEntry(idxPath, keyData, idxHdr.keySize)) {
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }

    // 更新 entryCount（通过读取更新后的 IndexHeader）
    if (!FileManager::readIndexHeader(idxPath, idxHdr)) {
        return ErrorCode::DB_ERR_FILE_READ_FAILED;
    }
    // entryCount 已经被 removeIndexEntry 正确设置

    return ErrorCode::DB_OK;
}

// ─── Lookup ────────────────────────────────────────────
ErrorCode IndexManager::Lookup(const std::string& indexName,
                                 const void* keyData,
                                 std::vector<uint32_t>& outOffsets) {
    IndexHeader idxHdr;
    ErrorCode err = DictManager::GetIndexHeader(indexName, idxHdr);
    if (err != ErrorCode::DB_OK) return err;

    std::string idxPath = DictManager::GetCurrentDB() + "/" + indexName + ".idx";
    if (!FileManager::lookupIndexEntry(idxPath, keyData, idxHdr.keySize, outOffsets)) {
        // 没找到不算错误，返回 OK 但 outOffsets 为空
        return ErrorCode::DB_OK;
    }

    return ErrorCode::DB_OK;
}

// ─── buildIndexData（内部辅助）──────────────────────────
ErrorCode IndexManager::buildIndexData(const struct TableHeader& hdr,
                                          const std::vector<ColumnDef>& fields,
                                          const struct IndexHeader& idxHdr) {
    // 1. 打开 .trd 文件
    std::string trdPath = DictManager::GetCurrentDB() + "/" + idxHdr.tableName + ".trd";
    std::ifstream ifs(trdPath, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[IdxMgr] Cannot open .trd file: " << trdPath << "\n";
        return ErrorCode::DB_ERR_FILE_OPEN_FAILED;
    }

    // 2. 获取索引字段的 offset 和 length
    const ColumnDef& col = fields[idxHdr.columnIndex];
    uint32_t colOffset = col.offset;   // 字段在记录中的偏移量
    uint32_t colLength = col.length;   // 字段长度（字节数）

    // 3. 逐条读取记录，提取索引字段值，插入索引
    std::vector<char> keyBuf(idxHdr.keySize);
    uint32_t recordOffset = 0;

    for (uint32_t i = 0; i < hdr.recordCount; ++i) {
        // 定位到记录的起始位置
        ifs.seekg(recordOffset);

        // 读取索引字段的值
        ifs.seekg(recordOffset + colOffset);  // 先定位到字段偏移
        ifs.read(keyBuf.data(), colLength);

        if (!ifs) {
            std::cerr << "[IdxMgr] Failed to read record #" << i << " from " << trdPath << "\n";
            break;
        }

        // 插入索引条目
        ErrorCode err = InsertEntry(idxHdr.indexName, keyBuf.data(), recordOffset);
        if (err != ErrorCode::DB_OK) {
            std::cerr << "[IdxMgr] Failed to insert index entry for record #" << i << "\n";
            return err;
        }

        // 移动到下一条记录
        recordOffset += hdr.recordSize;
    }

    std::cerr << "[IdxMgr] Index built: " << idxHdr.indexName
              << " with " << hdr.recordCount << " entries\n";
    return ErrorCode::DB_OK;
}
