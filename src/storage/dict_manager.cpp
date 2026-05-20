// src/storage/dict_manager.cpp
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"

#include <iostream>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>

namespace fs = std::filesystem;

// ─── 内部全局：当前数据库路径前缀 ────────────────────────
std::string g_currentDbDir = ".";   // 全局变量，供其他模块通过 extern 访问

// ─── 索引元数据内存缓存（UseDatabase 时预加载）────────────
static std::map<std::string, IndexHeader> g_indexCache;  // key: indexName

// 前向声明
static void forEachIndex(std::function<void(const IndexHeader&)> visitor);

static fs::path databaseRootPath() {
    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    if (ec) {
        return ".";
    }

    while (!dir.empty()) {
        if (fs::exists(dir / "CMakeLists.txt") &&
            fs::is_directory(dir / "src") &&
            fs::is_directory(dir / "include")) {
            return dir;
        }

        fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    return fs::current_path(ec);
}

static fs::path databasePathFor(const std::string& dbName) {
    fs::path requested(dbName);
    if (requested.is_absolute()) {
        return requested;
    }
    return databaseRootPath() / requested;
}

static fs::path resolveDatabasePath(const std::string& dbName) {
    fs::path requested(dbName);
    if (requested.is_absolute()) {
        if (fs::exists(requested) && fs::is_directory(requested)) {
            return requested;
        }
        return {};
    }

    fs::path rooted = databasePathFor(dbName);
    if (fs::exists(rooted) && fs::is_directory(rooted)) {
        return rooted;
    }

    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    if (ec) {
        return {};
    }

    while (!dir.empty()) {
        fs::path candidate = dir / requested;
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }

        fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    return {};
}

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
    fs::path dbPath = databasePathFor(dbName);
    fs::create_directories(dbPath, ec);
    if (ec) {
        std::cerr << "[DictMgr] CreateDatabase failed: " << ec.message() << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    // 写入系统元文件 ruanko.db（SysDBRecord）
    const std::string metaPath = (dbPath / "ruanko.db").string();
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
    fs::path dbPath = databasePathFor(dbName);
    fs::remove_all(dbPath, ec);
    if (ec) {
        std::cerr << "[DictMgr] DropDatabase failed: " << ec.message() << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    // 若当前 db 就是被删除的，复位到当前目录
    if (g_currentDbDir == dbPath.string()) {
        g_currentDbDir = ".";
    }
    return ErrorCode::DB_OK;
}

// ─── UseDatabase ─────────────────────────────────────────
ErrorCode DictManager::UseDatabase(const std::string& dbName) {
    fs::path dbPath = resolveDatabasePath(dbName);
    if (dbPath.empty()) {
        return ErrorCode::DB_ERR_DB_NOT_FOUND;
    }
    g_currentDbDir = dbPath.string();

    // 预加载该库所有索引元数据到内存缓存
    g_indexCache.clear();
    forEachIndex([](const IndexHeader& h) {
        g_indexCache[std::string(h.indexName)] = h;
    });

    std::cerr << "[DictMgr] Loaded " << g_indexCache.size()
              << " indexes from database: " << g_currentDbDir << "\n";
    return ErrorCode::DB_OK;
}

// ─── GetCurrentDB ─────────────────────────────────────────
std::string DictManager::GetCurrentDB() {
    return g_currentDbDir;
}

std::string DictManager::GetDatabaseRoot() {
    return databaseRootPath().string();
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

// ─── 索引元数据管理（Task 3）────────────────────────────

// 辅助：返回 _index_meta.tdf 完整路径
std::string DictManager::indexMetaPath() {
    return g_currentDbDir + "/_index_meta.tdf";
}

// 辅助：遍历 _index_meta.tdf，对每个条目调用 visitor
static void forEachIndex(std::function<void(const IndexHeader&)> visitor) {
    const std::string metaPath = DictManager::indexMetaPath();
    if (!FileManager::fileExists(metaPath)) return;
    std::ifstream ifs(metaPath, std::ios::binary);
    if (!ifs.is_open()) return;
    IndexHeader hdr;
    while (ifs.read(reinterpret_cast<char*>(&hdr), sizeof(IndexHeader))) {
        if (hdr.indexName[0] == '\0') continue;  // 跳过已删除条目
        visitor(hdr);
    }
}

// CreateIndex：写入 _index_meta.tdf + 创建空的 .idx 文件
ErrorCode DictManager::CreateIndex(const std::string& indexName,
                                      const std::string& tableName,
                                      const std::string& columnName,
                                      uint32_t columnIndex,
                                      uint32_t keyType) {
    // 1. 检查索引是否已存在
    bool exists = false;
    forEachIndex([&](const IndexHeader& h) {
        if (std::string(h.indexName) == indexName) exists = true;
    });
    if (exists) return ErrorCode::DB_ERR_INDEX_EXISTS;

    // 2. 检查表是否存在
    if (!FileManager::fileExists(tbPath(tableName))) {
        return ErrorCode::DB_ERR_TABLE_NOT_FOUND;
    }

    // 3. 构造 IndexHeader
    IndexHeader hdr{};
    std::strncpy(hdr.indexName, indexName.c_str(), MAX_NAME_LEN - 1);
    std::strncpy(hdr.tableName, tableName.c_str(), MAX_NAME_LEN - 1);
    std::strncpy(hdr.columnName, columnName.c_str(), MAX_NAME_LEN - 1);
    hdr.columnIndex = columnIndex;
    hdr.keyType     = keyType;
    hdr.entryCount  = 0;
    hdr.createTime  = static_cast<uint32_t>(std::time(nullptr));
    hdr.reserved[1] = 1;  // nextPageId 起始值，避免与 rootPageId=0（空树哨兵）冲突

    // 4. 追加到 _index_meta.tdf
    const std::string metaPath = indexMetaPath();
    if (!FileManager::fileExists(metaPath)) {
        FileManager::createFile(metaPath);
    }
    std::ofstream ofs(metaPath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(IndexHeader));

    // 5. 创建空的 .idx 文件（仅含 IndexHeader）
    const std::string idxPath = g_currentDbDir + "/" + indexName + ".idx";
    std::ofstream idxFile(idxPath, std::ios::binary);
    if (!idxFile.is_open()) {
        std::cerr << "[DictMgr] Warning: failed to create .idx file: " << idxPath << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    idxFile.write(reinterpret_cast<const char*>(&hdr), sizeof(IndexHeader));
    idxFile.close();

    std::cerr << "[DictMgr] Index created: " << indexName
              << " on " << tableName << "(" << columnName << ")\n";

    // 更新内存缓存
    g_indexCache[indexName] = hdr;

    return ErrorCode::DB_OK;
}

// CreateIndex 多列重载（复合索引）
ErrorCode DictManager::CreateIndex(const std::string& indexName,
                                      const std::string& tableName,
                                      const std::vector<std::string>& columnNames,
                                      const std::vector<uint32_t>& columnIndices,
                                      const std::vector<uint32_t>& keyTypes,
                                      const std::vector<ColumnDef>& fields) {
    if (columnNames.empty() || columnNames.size() > 4)
        return ErrorCode::DB_INVALID_PARAM;

    // 1. 检查索引是否已存在
    bool exists = false;
    forEachIndex([&](const IndexHeader& h) {
        if (std::string(h.indexName) == indexName) exists = true;
    });
    if (exists) return ErrorCode::DB_ERR_INDEX_EXISTS;

    // 2. 检查表是否存在
    if (!FileManager::fileExists(tbPath(tableName)))
        return ErrorCode::DB_ERR_TABLE_NOT_FOUND;

    // 3. 构造 IndexHeader
    IndexHeader hdr{};
    std::strncpy(hdr.indexName, indexName.c_str(), MAX_NAME_LEN - 1);
    std::strncpy(hdr.tableName, tableName.c_str(), MAX_NAME_LEN - 1);

    std::string joinedNames;
    for (size_t i = 0; i < columnNames.size(); ++i) {
        if (i > 0) joinedNames += '|';
        joinedNames += columnNames[i];
    }
    std::strncpy(hdr.columnName, joinedNames.c_str(), MAX_NAME_LEN - 1);

    hdr.columnIndex = columnIndices[0];
    hdr.keyType     = keyTypes[0];
    hdr.entryCount  = 0;
    hdr.createTime  = static_cast<uint32_t>(std::time(nullptr));
    hdr.reserved[1] = 1;  // nextPageId 起始值，避免与 rootPageId=0（空树哨兵）冲突

    hdr.reserved[3] = static_cast<uint32_t>(columnNames.size());
    for (size_t i = 1; i < columnNames.size() && i <= 3; ++i) {
        hdr.reserved[3 + i] = columnIndices[i];
    }

    // 4. 追加到 _index_meta.tdf
    const std::string metaPath = indexMetaPath();
    if (!FileManager::fileExists(metaPath))
        FileManager::createFile(metaPath);
    std::ofstream ofs(metaPath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(IndexHeader));

    // 5. 创建空的 .idx 文件
    const std::string idxPath = g_currentDbDir + "/" + indexName + ".idx";
    std::ofstream idxFile(idxPath, std::ios::binary);
    if (!idxFile.is_open()) {
        std::cerr << "[DictMgr] Warning: failed to create .idx file: " << idxPath << "\n";
        return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    }
    idxFile.write(reinterpret_cast<const char*>(&hdr), sizeof(IndexHeader));
    idxFile.close();

    std::cerr << "[DictMgr] Composite index created: " << indexName
              << " on " << tableName << "(" << joinedNames << "), "
              << columnNames.size() << " columns\n";

    g_indexCache[indexName] = hdr;

    return ErrorCode::DB_OK;
}

// DropIndex：从 _index_meta.tdf 移除 + 删除 .idx 文件
ErrorCode DictManager::DropIndex(const std::string& indexName) {
    const std::string metaPath = indexMetaPath();

    // 1. 先读取所有条目，同时查找要删除的索引并保存 tableName
    std::vector<IndexHeader> kept;
    std::string tableName;
    bool found = false;
    {
        std::ifstream ifs(metaPath, std::ios::binary);
        if (!ifs.is_open()) return ErrorCode::DB_ERR_INDEX_NOT_FOUND;
        IndexHeader h;
        while (ifs.read(reinterpret_cast<char*>(&h), sizeof(IndexHeader))) {
            if (std::string(h.indexName) == indexName) {
                tableName = h.tableName;
                found = true;
                // 不加入 kept，即删除
            } else {
                kept.push_back(h);
            }
        }
    }
    if (!found) return ErrorCode::DB_ERR_INDEX_NOT_FOUND;

    // 2. 重写 _index_meta.tdf（不含被删除的索引）
    FileManager::deleteFile(metaPath);
    FileManager::createFile(metaPath);
    std::ofstream ofs(metaPath, std::ios::binary);
    if (!ofs.is_open()) return ErrorCode::DB_ERR_FILE_WRITE_FAILED;
    for (const auto& h : kept) {
        ofs.write(reinterpret_cast<const char*>(&h), sizeof(IndexHeader));
    }

    // 3. 删除对应的 .idx 文件
    std::string idxPath = g_currentDbDir + "/" + indexName + ".idx";
    if (FileManager::fileExists(idxPath)) {
        FileManager::deleteFile(idxPath);
    }

    std::cerr << "[DictMgr] Index dropped: " << indexName << "\n";

    // 从内存缓存中移除
    g_indexCache.erase(indexName);

    return ErrorCode::DB_OK;
}

// ListIndexes：列出指定表上所有索引名
ErrorCode DictManager::ListIndexes(const std::string& tableName,
                                      std::vector<std::string>& outIndexNames) {
    outIndexNames.clear();
    // 优先读缓存（UseDatabase 预加载）
    for (const auto& kv : g_indexCache) {
        if (std::string(kv.second.tableName) == tableName) {
            outIndexNames.emplace_back(kv.first);
        }
    }
    return ErrorCode::DB_OK;
}

// LoadTableIndexes：加载指定表的所有索引元数据
ErrorCode DictManager::LoadTableIndexes(const std::string& tableName,
                                           std::vector<IndexHeader>& outHeaders) {
    outHeaders.clear();
    forEachIndex([&](const IndexHeader& h) {
        if (std::string(h.tableName) == tableName) {
            outHeaders.push_back(h);
        }
    });
    return ErrorCode::DB_OK;
}

// GetIndexHeader：根据索引名查找 IndexHeader
ErrorCode DictManager::GetIndexHeader(const std::string& indexName,
                                         IndexHeader& outHeader) {
    auto it = g_indexCache.find(indexName);
    if (it != g_indexCache.end()) {
        outHeader = it->second;
        return ErrorCode::DB_OK;
    }
    return ErrorCode::DB_ERR_INDEX_NOT_FOUND;
}

// UpdateIndexCache：同步内存缓存中的索引元数据
void DictManager::UpdateIndexCache(const IndexHeader& hdr) {
    g_indexCache[std::string(hdr.indexName)] = hdr;
}
