// src/storage/dict_manager.cpp
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"

#include <iostream>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

// ─── 内部全局：当前数据库路径前缀 ────────────────────────
static std::string g_currentDbDir = ".";   // 默认当前目录

// ─── 辅助：生成 .tb / .tdf 文件路径 ──────────────────────
static std::string tbPath(const std::string& tableName) {
    return g_currentDbDir + "/" + tableName + ".tb";
}
static std::string tdfPath(const std::string& tableName) {
    return g_currentDbDir + "/" + tableName + ".tdf";
}

// ─── CreateDatabase ──────────────────────────────────────
ErrorCode DictManager::CreateDatabase(const std::string& dbName) {
    std::error_code ec;
    fs::create_directories(dbName, ec);
    if (ec) {
        std::cerr << "[DictMgr] CreateDatabase failed: " << ec.message() << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    // 写入系统元文件 ruanko.db（SysDBRecord）
    const std::string metaPath = dbName + "/ruanko.db";
    if (!FileManager::fileExists(metaPath)) {
        SysDBRecord rec{};
        std::strncpy(rec.dbName, dbName.c_str(), MAX_NAME_LEN - 1);
        rec.createTime = static_cast<uint32_t>(std::time(nullptr));
        rec.tableCount = 0;
        FileManager::createFile(metaPath);
        FileManager::writeStruct(metaPath, rec, 0);
    }
    return ErrorCode::DB_OK;
}

// ─── DropDatabase ────────────────────────────────────────
ErrorCode DictManager::DropDatabase(const std::string& dbName) {
    std::error_code ec;
    fs::remove_all(dbName, ec);
    if (ec) {
        std::cerr << "[DictMgr] DropDatabase failed: " << ec.message() << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    // 若当前 db 就是被删除的，复位到当前目录
    if (g_currentDbDir == dbName) {
        g_currentDbDir = ".";
    }
    return ErrorCode::DB_OK;
}

// ─── UseDatabase ─────────────────────────────────────────
ErrorCode DictManager::UseDatabase(const std::string& dbName) {
    if (!fs::exists(dbName)) {
        return ErrorCode::DB_ERR_DB_NOT_FOUND;
    }
    g_currentDbDir = dbName;
    return ErrorCode::DB_OK;
}

// ─── GetCurrentDB ─────────────────────────────────────────
std::string DictManager::GetCurrentDB() {
    return g_currentDbDir;
}

// ─── ShowTables ──────────────────────────────────────────
ErrorCode DictManager::ShowTables(std::vector<std::string>& outTables) {
    outTables.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(g_currentDbDir, ec)) {
        if (entry.path().extension() == ".tb") {
            outTables.push_back(entry.path().stem().string());
        }
    }
    if (ec) {
        return ErrorCode::DB_ERR_FILE_READ_FAILED;
    }
    return ErrorCode::DB_OK;
}

// ─── GetTableHeader（方案规定签名）──────────────────────
ErrorCode DictManager::GetTableHeader(const std::string& tableName,
                                       TableHeader& header) {
    return loadTableHeader(tableName, header);
}

// ─── loadTable ───────────────────────────────────────────
ErrorCode DictManager::loadTable(const std::string& tableName,
                                  TableHeader& header,
                                  std::vector<ColumnDef>& fields) {
    // 1. 加载表头
    ErrorCode err = loadTableHeader(tableName, header);
    if (err != ErrorCode::DB_OK) {
        return err;
    }

    // 2. 读取字段定义数组
    const std::string tdf = tdfPath(tableName);
    if (!FileManager::fileExists(tdf)) {
        std::cerr << "[DictMgr] Error: .tdf file missing: " << tdf << "\n";
        return ErrorCode::DB_ERR_FILE_NOT_FOUND;
    }

    fields.clear();
    fields.resize(header.fieldCount);

    for (uint32_t i = 0; i < header.fieldCount; ++i) {
        bool ok = FileManager::readStruct(tdf, fields[i],
                                          static_cast<std::streampos>(sizeof(ColumnDef) * i));
        if (!ok) {
            std::cerr << "[DictMgr] Error: Failed to read field #" << i
                      << " from " << tdf << "\n";
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
        std::cerr << "[DictMgr] Error: Corrupted .tb file: " << tb << "\n";
        return ErrorCode::DB_ERR_FILE_CORRUPTED;
    }

    if (header.fieldCount > 256) {
        std::cerr << "[DictMgr] Warning: Unusual fieldCount=" << header.fieldCount
                  << ", possible corruption\n";
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
    ErrorCode err = loadTableHeader(tableName, hdr);
    if (err != ErrorCode::DB_OK) return err;

    hdr.recordCount = recordCount;
    hdr.modifyTime   = static_cast<uint32_t>(std::time(nullptr));

    bool ok = FileManager::writeStruct(tbPath(tableName), hdr, 0);
    return ok ? ErrorCode::DB_OK : ErrorCode::DB_ERR_FILE_WRITE_FAILED;
}

// ─── touchModifyTime ─────────────────────────────────────
ErrorCode DictManager::touchModifyTime(const std::string& tableName) {
    TableHeader hdr;
    ErrorCode err = loadTableHeader(tableName, hdr);
    if (err != ErrorCode::DB_OK) return err;

    hdr.modifyTime = static_cast<uint32_t>(std::time(nullptr));
    bool ok = FileManager::writeStruct(tbPath(tableName), hdr, 0);
    return ok ? ErrorCode::DB_OK : ErrorCode::DB_ERR_FILE_WRITE_FAILED;
}
