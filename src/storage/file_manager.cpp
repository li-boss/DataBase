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
