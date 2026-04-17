// src/storage/dict_manager.cpp
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"

#include <iostream>
#include <ctime>

// ─── 辅助：生成 .tb / .tdf 文件路径 ──────────────────────
static std::string tbPath(const std::string& tableName) {
    return tableName + ".tb";
}
static std::string tdfPath(const std::string& tableName) {
    return tableName + ".tdf";
}

// ─── loadTable ───────────────────────────────────────────
ErrorCode DictManager::loadTable(const std::string& tableName,
                                  TableHeader& header,
                                  std::vector<FieldDefinition>& fields) {
    // 1. 加载表头
    ErrorCode err = loadTableHeader(tableName, header);
    if (err != ErrorCode::DB_OK) {
        return err;
    }

    // 2. 读取字段定义数组
    const std::string tdf = tdfPath(tableName);
    if (!FileManager::fileExists(tdf)) {
        std::cerr << "[DictMgr] Error: .tdf file missing: " << tdf << std::endl;
        return ErrorCode::DB_ERR_FILE_NOT_FOUND;
    }

    fields.clear();
    fields.resize(header.fieldCount);

    for (uint32_t i = 0; i < header.fieldCount; ++i) {
        bool ok = FileManager::readStruct(tdf, fields[i], sizeof(FieldDefinition) * i);
        if (!ok) {
            std::cerr << "[DictMgr] Error: Failed to read field #" << i
                      << " from " << tdf << std::endl;
            return ErrorCode::DB_ERR_FILE_READ_FAILED;
        }
    }

    return ErrorCode::DB_OK;
}

// ─── loadTableHeader ─────────────────────────────────────
ErrorCode DictManager::loadTableHeader(const std::string& tableName,
                                        TableHeader& header) {
    const std::string tb = tbPath(tableName);

    if (!FileManager::fileExists(tb)) {
        return ErrorCode::DB_ERR_TABLE_NOT_FOUND;
    }

    bool ok = FileManager::readStruct(tb, header, 0);
    if (!ok) {
        std::cerr << "[DictMgr] Error: Corrupted .tb file: " << tb << std::endl;
        return ErrorCode::DB_ERR_FILE_CORRUPTED;
    }

    // 简单校验：recordCount 和 fieldCount 不能超出合理范围
    if (header.fieldCount > 256) {
        std::cerr << "[DictMgr] Warning: Unusual fieldCount=" << header.fieldCount
                  << ", possible corruption" << std::endl;
        return ErrorCode::DB_ERR_FILE_CORRUPTED;
    }

    return ErrorCode::DB_OK;
}

// ─── tableExists ─────────────────────────────────────────
bool DictManager::tableExists(const std::string& tableName) {
    return FileManager::fileExists(tbPath(tableName));
}

// ─── updateRecordCount ──────────────────────────────────
ErrorCode DictManager::updateRecordCount(const std::string& tableName,
                                          uint32_t recordCount) {
    TableHeader hdr;

    // 先读出完整表头，只修改 recordCount 字段后回写
    ErrorCode err = loadTableHeader(tableName, hdr);
    if (err != ErrorCode::DB_OK) {
        return err;  // 表不存在或损坏
    }

    hdr.recordCount = recordCount;
    hdr.modifyTime   = static_cast<uint32_t>(std::time(nullptr));

    bool ok = FileManager::writeStruct(tbPath(tableName), hdr, 0);
    if (!ok) {
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }

    return ErrorCode::DB_OK;
}

// ─── touchModifyTime ─────────────────────────────────────
ErrorCode DictManager::touchModifyTime(const std::string& tableName) {
    TableHeader hdr;

    ErrorCode err = loadTableHeader(tableName, hdr);
    if (err != ErrorCode::DB_OK) {
        return err;
    }

    hdr.modifyTime = static_cast<uint32_t>(std::time(nullptr));
    bool ok = FileManager::writeStruct(tbPath(tableName), hdr, 0);

    return ok ? ErrorCode::DB_OK : ErrorCode::DB_ERR_FILE_WRITE_FAILED;
}
