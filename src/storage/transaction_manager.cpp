// src/storage/transaction_manager.cpp
#include "../../include/storage/transaction_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/log_manager.h"
#include "../../include/storage/buffer_pool.h"
#include "../../include/common/db_structs.h"
#include "../../include/common/db_errors.h"

#include <iostream>
#include <cstring>
#include <fstream>
#include <ctime>

// 静态成员初始化
bool TransactionManager::s_active = false;
std::string TransactionManager::s_tableName;
std::vector<char> TransactionManager::s_snapshotData;
uint32_t TransactionManager::s_snapshotRecordCount = 0;

// ─── begin ───────────────────────────────────────────
ErrorCode TransactionManager::begin(const std::string& tableName) {
    if (s_active) {
        LogManager::error(LogManager::OpType::BEGIN_TX,
            "Transaction already active on table '" + s_tableName + "'");
        return ErrorCode::DB_ERR_TX_ACTIVE;
    }

    std::string dbDir = DictManager::GetCurrentDB();
    if (dbDir.empty()) {
        return ErrorCode::DB_ERR_DB_NOT_FOUND;
    }

    // 加载表结构
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(tableName, hdr, fields);
    if (err != ErrorCode::DB_OK) return err;

    // 刷盘：确保 INSERT/UPDATE 写入 BufferPool 的数据已落盘，快照才不会读到空页
    BufferPool::flushAll();

    // 读取 .trd 文件快照
    std::string trdFile = dbDir + "/" + tableName + ".trd";
    int fd;
    if (!FileManager::OpenFile(trdFile, "r", fd)) {
        // 表为空或文件不存在时，用空快照
        s_snapshotData.clear();
        s_snapshotRecordCount = 0;
    } else {
        // 计算文件大小
        uint32_t totalBytes = hdr.recordCount * hdr.recordSize;
        if (totalBytes > 0) {
            s_snapshotData.resize(totalBytes, 0);
            // 逐页读取
            uint32_t rpp = 4080 / hdr.recordSize;
            if (rpp == 0) rpp = 1;
            uint32_t totalPages = (hdr.recordCount + rpp - 1) / rpp;
            for (uint32_t pid = 0; pid < totalPages; ++pid) {
                char pageBuf[4096] = {0};
                bool ok = FileManager::ReadPage(fd, pid, pageBuf);
                if (!ok) continue;
                uint32_t startRec = pid * rpp;
                uint32_t endRec = (startRec + rpp > hdr.recordCount) ? hdr.recordCount : startRec + rpp;
                uint32_t byteStart = startRec * hdr.recordSize;
                std::memcpy(s_snapshotData.data() + byteStart, pageBuf,
                            (endRec - startRec) * hdr.recordSize);
            }
        } else {
            s_snapshotData.clear();
        }
        FileManager::CloseFile(fd);
    }

    s_snapshotRecordCount = hdr.recordCount;
    s_tableName = tableName;
    s_active = true;

    LogManager::log(LogManager::OpType::BEGIN_TX,
        "transaction started on table '" + tableName + "' (snapshot: "
        + std::to_string(s_snapshotRecordCount) + " records)");

    return ErrorCode::DB_OK;
}

// ─── commit ──────────────────────────────────────────
ErrorCode TransactionManager::commit() {
    if (!s_active) {
        LogManager::error(LogManager::OpType::COMMIT_TX, "No active transaction");
        return ErrorCode::DB_ERR_TX_NOT_ACTIVE;
    }

    LogManager::log(LogManager::OpType::COMMIT_TX,
        "transaction committed on table '" + s_tableName + "'");

    // 释放快照
    s_snapshotData.clear();
    s_snapshotData.shrink_to_fit();
    s_active = false;
    s_tableName.clear();

    return ErrorCode::DB_OK;
}

// ─── rollback ────────────────────────────────────────
ErrorCode TransactionManager::rollback() {
    if (!s_active) {
        LogManager::error(LogManager::OpType::ROLLBACK_TX, "No active transaction");
        return ErrorCode::DB_ERR_TX_NOT_ACTIVE;
    }

    std::string dbDir = DictManager::GetCurrentDB();
    std::string trdFile = dbDir + "/" + s_tableName + ".trd";

    // 恢复 .trd 文件
    int fd;
    if (FileManager::OpenFile(trdFile, "rw", fd)) {
        if (s_snapshotData.empty()) {
            // 快照为空 → 清空数据文件
            // 写入一个空页然后截断
            char emptyPage[4096] = {0};
            FileManager::WritePage(fd, 0, emptyPage);
        } else {
            // 写回快照数据
            TableHeader hdr;
            std::vector<ColumnDef> fields;
            DictManager::loadTable(s_tableName, hdr, fields);

            uint32_t rpp = 4080 / hdr.recordSize;
            if (rpp == 0) rpp = 1;

            for (size_t i = 0; i < s_snapshotData.size(); i += hdr.recordSize) {
                uint32_t recIdx = static_cast<uint32_t>(i / hdr.recordSize);
                uint32_t pid = recIdx / rpp;
                uint32_t pageOff = (recIdx % rpp) * hdr.recordSize;
                char pageBuf[4096] = {0};
                FileManager::ReadPage(fd, pid, pageBuf);
                std::memcpy(pageBuf + pageOff, s_snapshotData.data() + i, hdr.recordSize);
                FileManager::WritePage(fd, pid, pageBuf);
            }
        }
        FileManager::CloseFile(fd);

        // 驱逐 BufferPool 中该表的所有缓存页，确保后续查询读到恢复后的磁盘数据
        BufferPool::InvalidateFile(trdFile);
    }

    // 恢复 recordCount
    DictManager::updateRecordCount(s_tableName, s_snapshotRecordCount);

    // 恢复 modifyTime
    TableHeader hdr;
    std::vector<ColumnDef> fields;
    if (DictManager::loadTable(s_tableName, hdr, fields) == ErrorCode::DB_OK) {
        hdr.modifyTime = static_cast<uint32_t>(time(nullptr));
        FileManager::writeStruct(dbDir + "/" + s_tableName + ".tb", hdr, 0);
    }

    LogManager::log(LogManager::OpType::ROLLBACK_TX,
        "transaction rolled back on table '" + s_tableName
        + "' (restored " + std::to_string(s_snapshotRecordCount) + " records)");

    // 释放快照
    s_snapshotData.clear();
    s_snapshotData.shrink_to_fit();
    s_active = false;
    s_tableName.clear();

    return ErrorCode::DB_OK;
}

// ─── isActive / getTableName ─────────────────────────
bool TransactionManager::isActive() {
    return s_active;
}

const std::string& TransactionManager::getTableName() {
    return s_tableName;
}
