// src/storage/file_manager.cpp
#include "../../include/storage/file_manager.h"
#include "../../include/common/db_types.h"
#include <filesystem>
#include <unordered_map>
#include <mutex>

namespace fs = std::filesystem;

// ─── fd 管理表 ─────────────────────────────────────────
// fd 从 1 开始分配，0 保留为无效值
struct FdEntry {
    std::string filepath;
    bool        writable;
};

static std::unordered_map<int, FdEntry> g_fdTable;
static int g_nextFd = 1;
static std::mutex g_fdMutex;

// ─── OpenFile ────────────────────────────────────────────
bool FileManager::OpenFile(const std::string& path,
                            const std::string& mode,
                            int& outFd) {
    bool writable = (mode == "rw");

    if (writable && !fileExists(path)) {
        // 不存在则创建
        if (!createFile(path)) return false;
    } else if (!fileExists(path)) {
        return false;
    }

    std::lock_guard<std::mutex> lk(g_fdMutex);
    outFd = g_nextFd++;
    g_fdTable[outFd] = { path, writable };
    return true;
}

// ─── CloseFile ───────────────────────────────────────────
bool FileManager::CloseFile(int fd) {
    std::lock_guard<std::mutex> lk(g_fdMutex);
    auto it = g_fdTable.find(fd);
    if (it == g_fdTable.end()) return false;
    g_fdTable.erase(it);
    return true;
}

// ─── GetFilePath ─────────────────────────────────────────
std::string FileManager::GetFilePath(int fd) {
    std::lock_guard<std::mutex> lk(g_fdMutex);
    auto it = g_fdTable.find(fd);
    if (it == g_fdTable.end()) return "";
    return it->second.filepath;
}

// ─── ReadPage ────────────────────────────────────────────
bool FileManager::ReadPage(int fd, uint32_t pageId, char* buf) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_fdMutex);
        auto it = g_fdTable.find(fd);
        if (it == g_fdTable.end()) return false;
        path = it->second.filepath;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;

    ifs.seekg(static_cast<std::streamoff>(pageId) * PAGE_SIZE);
    ifs.read(buf, PAGE_SIZE);
    return ifs.gcount() > 0;
}

// ─── WritePage ───────────────────────────────────────────
bool FileManager::WritePage(int fd, uint32_t pageId, const char* buf) {
    std::string path;
    bool writable;
    {
        std::lock_guard<std::mutex> lk(g_fdMutex);
        auto it = g_fdTable.find(fd);
        if (it == g_fdTable.end()) return false;
        path     = it->second.filepath;
        writable = it->second.writable;
    }
    if (!writable) return false;

    std::ofstream ofs(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!ofs.is_open()) {
        ofs.clear();
        ofs.open(path, std::ios::binary | std::ios::out);
    }
    if (!ofs.is_open()) return false;

    ofs.seekp(static_cast<std::streamoff>(pageId) * PAGE_SIZE);
    ofs.write(buf, PAGE_SIZE);
    return ofs.good();
}

// ─── filepath-based 实现 ─────────────────────────────────

bool FileManager::createFile(const std::string& filepath) {
    if (fileExists(filepath)) return false;
    std::ofstream ofs(filepath, std::ios::binary);
    return ofs.is_open();
}

bool FileManager::truncateFile(const std::string& filepath) {
    std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc);
    return ofs.is_open();
}

bool FileManager::deleteFile(const std::string& filepath) {
    std::error_code ec;
    return fs::remove(filepath, ec);
}

bool FileManager::fileExists(const std::string& filepath) {
    std::error_code ec;
    return fs::exists(filepath, ec);
}

bool FileManager::appendBlock(const std::string& filepath, const void* data, size_t size) {
    std::ofstream ofs(filepath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(data), size);
    return ofs.good();
}

// ─── 索引文件读写（.idx）────────────────────────────

bool FileManager::createIndexFile(const std::string& idxPath, const IndexHeader& hdr) {
    std::ofstream ofs(idxPath, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(IndexHeader));
    return ofs.good();
}

bool FileManager::writeIndexHeader(const std::string& idxPath, const IndexHeader& hdr) {
    return writeStruct(idxPath, hdr, 0);
}

bool FileManager::readIndexHeader(const std::string& idxPath, IndexHeader& outHdr) {
    return readStruct(idxPath, outHdr, 0);
}

bool FileManager::appendIndexEntry(const std::string& idxPath,
                                     const void* keyData,
                                     uint32_t keySize,
                                     uint32_t recordOffset) {
    IndexHeader hdr;
    if (!readIndexHeader(idxPath, hdr)) return false;

    std::ofstream ofs(idxPath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(keyData), keySize);
    ofs.write(reinterpret_cast<const char*>(&recordOffset), sizeof(uint32_t));
    ofs.close();

    hdr.entryCount++;
    return writeIndexHeader(idxPath, hdr);
}

bool FileManager::lookupIndexEntry(const std::string& idxPath,
                                    const void* keyData,
                                    uint32_t keySize,
                                    std::vector<uint32_t>& outOffsets) {
    outOffsets.clear();
    IndexHeader hdr;
    if (!readIndexHeader(idxPath, hdr)) return false;

    std::ifstream ifs(idxPath, std::ios::binary);
    if (!ifs.is_open()) return false;
    ifs.seekg(sizeof(IndexHeader));

    std::vector<char> buf(keySize);
    uint32_t recOff = 0;

    for (uint32_t i = 0; i < hdr.entryCount; ++i) {
        ifs.read(buf.data(), keySize);
        ifs.read(reinterpret_cast<char*>(&recOff), sizeof(uint32_t));
        if (!ifs) break;

        if (std::memcmp(buf.data(), keyData, keySize) == 0) {
            outOffsets.push_back(recOff);
        }
    }
    return !outOffsets.empty();
}

bool FileManager::lookupIndexRange(const std::string& idxPath,
                                   const void* keyData,
                                   uint32_t keySize,
                                   uint32_t keyType,
                                   const std::string& op,
                                   std::vector<uint32_t>& outOffsets) {
    outOffsets.clear();
    IndexHeader hdr;
    if (!readIndexHeader(idxPath, hdr)) return false;

    std::ifstream ifs(idxPath, std::ios::binary);
    if (!ifs.is_open()) return false;
    ifs.seekg(sizeof(IndexHeader));

    std::vector<char> buf(keySize);
    uint32_t recOff = 0;

    for (uint32_t i = 0; i < hdr.entryCount; ++i) {
        ifs.read(buf.data(), keySize);
        ifs.read(reinterpret_cast<char*>(&recOff), sizeof(uint32_t));
        if (!ifs) break;

        bool match = false;
        if (keyType == static_cast<uint32_t>(DataType::TYPE_INT) ||
            keyType == static_cast<uint32_t>(DataType::TYPE_DATETIME) ||
            keyType == static_cast<uint32_t>(DataType::TYPE_BOOLEAN)) {
            int32_t lv; std::memcpy(&lv, buf.data(), 4);
            int32_t rv; std::memcpy(&rv, keyData, 4);
            if (op == "<")  match = (lv < rv);
            else if (op == ">")  match = (lv > rv);
            else if (op == "<=") match = (lv <= rv);
            else if (op == ">=") match = (lv >= rv);
        } else if (keyType == static_cast<uint32_t>(DataType::TYPE_FLOAT)) {
            float lv; std::memcpy(&lv, buf.data(), 4);
            float rv; std::memcpy(&rv, keyData, 4);
            if (op == "<")  match = (lv < rv);
            else if (op == ">")  match = (lv > rv);
            else if (op == "<=") match = (lv <= rv);
            else if (op == ">=") match = (lv >= rv);
        } else if (keyType == static_cast<uint32_t>(DataType::TYPE_DOUBLE)) {
            double lv; std::memcpy(&lv, buf.data(), 8);
            double rv; std::memcpy(&rv, keyData, 8);
            if (op == "<")  match = (lv < rv);
            else if (op == ">")  match = (lv > rv);
            else if (op == "<=") match = (lv <= rv);
            else if (op == ">=") match = (lv >= rv);
        } else {
            std::string lv(buf.data(), keySize);
            std::string rv(reinterpret_cast<const char*>(keyData), keySize);
            if (op == "<")  match = (lv < rv);
            else if (op == ">")  match = (lv > rv);
            else if (op == "<=") match = (lv <= rv);
            else if (op == ">=") match = (lv >= rv);
        }

        if (match) {
            outOffsets.push_back(recOff);
        }
    }
    return !outOffsets.empty();
}

bool FileManager::removeIndexEntry(const std::string& idxPath,
                                    const void* keyData,
                                    uint32_t keySize,
                                    uint32_t recordOffset) {
    IndexHeader hdr;
    if (!readIndexHeader(idxPath, hdr)) return false;

    std::string tmpPath = idxPath + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary);
    if (!ofs.is_open()) return false;

    IndexHeader newHdr = hdr;
    newHdr.entryCount = 0;
    ofs.write(reinterpret_cast<const char*>(&newHdr), sizeof(IndexHeader));

    std::ifstream ifs(idxPath, std::ios::binary);
    if (!ifs.is_open()) { std::error_code ec; fs::remove(tmpPath, ec); return false; }
    ifs.seekg(sizeof(IndexHeader));

    const size_t entrySize = keySize + sizeof(uint32_t);
    std::vector<char> buf(entrySize);
    uint32_t newCount = 0;

    for (uint32_t i = 0; i < hdr.entryCount; ++i) {
        ifs.read(buf.data(), entrySize);
        if (!ifs) break;

        uint32_t entryRecOff;
        std::memcpy(&entryRecOff, buf.data() + keySize, sizeof(uint32_t));

        if (std::memcmp(buf.data(), keyData, keySize) == 0 && entryRecOff == recordOffset) {
            // Deleted
        } else {
            ofs.write(buf.data(), entrySize);
            ++newCount;
        }
    }
    ifs.close();
    ofs.close();

    newHdr.entryCount = newCount;
    std::fstream tmpFs(tmpPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!tmpFs.is_open()) { std::error_code ec; fs::remove(tmpPath, ec); return false; }
    tmpFs.write(reinterpret_cast<const char*>(&newHdr), sizeof(IndexHeader));
    tmpFs.close();

    std::error_code ec;
    fs::remove(idxPath, ec);
    fs::rename(tmpPath, idxPath, ec);
    return !ec;
}
