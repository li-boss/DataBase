// src/storage/buffer_pool.cpp
#include "../../include/storage/buffer_pool.h"
#include "../../include/storage/file_manager.h"
#include "../../include/common/db_types.h"
#include <cstring>
#include <fstream>
#include <iostream>

namespace BPNamespace {

// 单个缓存槽
struct PoolSlot {
    std::string filepath;     // 所属文件
    uint32_t    pageId;       // 页号
    char        data[PAGE_SIZE - sizeof(PageHeader)];
    bool        valid;        // 是否已加载
    bool        dirty;        // 是否脏页
    uint32_t    pinCount;     // 固定计数（>0 不可驱逐）

    PoolSlot() : pageId(0), valid(false), dirty(false), pinCount(0) {}
};

// 全局池
static PoolSlot*  g_pool      = nullptr;
static uint32_t   g_capacity  = 0;
static uint32_t   g_count    = 0;
static uint32_t   g_evictions = 0;
static uint32_t   g_hits      = 0;
static uint32_t   g_misses    = 0;
static bool        g_init      = false;

// ─── 辅助：查找槽位 ─────────────────────────────────────

static int bp_find(const std::string& fp, uint32_t pid) {
    for (uint32_t i = 0; i < g_capacity; ++i) {
        if (g_pool[i].valid &&
            g_pool[i].pageId == pid &&
            g_pool[i].filepath == fp)
            return static_cast<int>(i);
    }
    return -1;
}

// ─── 辅助：LRU 移到最新（末尾）───────────────────────

static void bp_move_to_newest(int idx) {
    if (idx < 0 || idx >= static_cast<int>(g_count)) return;
    PoolSlot tmp = g_pool[idx];
    for (int i = idx; i < static_cast<int>(g_count) - 1; ++i)
        g_pool[i] = g_pool[i + 1];
    g_pool[g_count - 1] = tmp;
}

// ─── 辅助：驱逐一个可驱逐的槽 ────────────────────────

static bool bp_evict_one() {
    if (g_count == 0) return false;

    for (int i = static_cast<int>(g_count) - 1; i >= 0; --i) {
        if (!g_pool[i].valid || g_pool[i].pinCount > 0) continue;

        if (g_pool[i].dirty) {
            std::ofstream ofs(g_pool[i].filepath,
                              std::ios::binary | std::ios::in | std::ios::out);
            if (ofs.is_open()) {
                ofs.seekp(static_cast<std::streamoff>(g_pool[i].pageId) * PAGE_SIZE);
                ofs.write(g_pool[i].data, PAGE_SIZE - sizeof(PageHeader));
                ofs.flush();
            }
        }

        g_pool[i].valid = false;
        g_pool[i].dirty = false;
        g_pool[i].pinCount = 0;
        g_evictions++;

        for (int j = i; j < static_cast<int>(g_count) - 1; ++j)
            g_pool[j] = g_pool[j + 1];
        g_count--;
        return true;
    }
    return false;
}

// ─── 辅助：从磁盘加载页 ──────────────────────────────

static bool bp_load_page(const std::string& fp, uint32_t pid, char* outBuf) {
    std::ifstream ifs(fp, std::ios::binary);
    if (!ifs.is_open()) return false;
    ifs.seekg(static_cast<std::streamoff>(pid) * PAGE_SIZE);
    ifs.read(outBuf, PAGE_SIZE - sizeof(PageHeader));
    // 读到 0 字节且已到文件末尾 = 页不存在
    return ifs.gcount() > 0;
}

// ─── 辅助：插入槽 ───────────────────────────────────

static bool bp_insert(const std::string& fp, uint32_t pid, const char* src) {
    if (g_count >= g_capacity) {
        if (!bp_evict_one()) return false;
    }
    g_pool[g_count].filepath = fp;
    g_pool[g_count].pageId   = pid;
    g_pool[g_count].valid    = true;
    g_pool[g_count].dirty    = false;
    g_pool[g_count].pinCount = 0;   // 默认不 pin，调用方按需 unpin
    if (src)
        std::memcpy(g_pool[g_count].data, src, PAGE_SIZE - sizeof(PageHeader));
    g_count++;
    return true;
}

}  // namespace BPNamespace

// ─── 公共接口（使用 BPNamespace:: 访问内部状态）────────

ErrorCode BufferPool::init(uint32_t maxPages) {
    using namespace BPNamespace;
    if (g_init) shutdown();
    g_capacity = (maxPages > MAX_PAGES) ? MAX_PAGES : maxPages;
    g_pool     = new BPNamespace::PoolSlot[g_capacity];
    g_count    = 0;
    g_evictions = 0;
    g_hits = g_misses = 0;
    g_init = true;
    return ErrorCode::DB_OK;
}

void BufferPool::shutdown() {
    using namespace BPNamespace;
    if (!g_init || !g_pool) return;
    flushAll();
    delete[] g_pool;
    g_pool = nullptr;
    g_capacity = g_count = 0;
    g_init = false;
}

ErrorCode BufferPool::getPage(const std::string& fp, uint32_t pid,
                               const char*& outData) {
    using namespace BPNamespace;
    if (!g_init) return ErrorCode::DB_INVALID_PARAM;

    int idx = bp_find(fp, pid);
    if (idx >= 0) {
        g_hits++;
        bp_move_to_newest(idx);
        outData = g_pool[g_count - 1].data;
        return ErrorCode::DB_OK;
    }

    g_misses++;
    char buf[PAGE_SIZE - sizeof(PageHeader)];
    if (!bp_load_page(fp, pid, buf)) {
        outData = nullptr;
        return ErrorCode::DB_ERR_PAGE_NOT_FOUND;
    }
    if (!bp_insert(fp, pid, buf)) {
        outData = nullptr;
        return ErrorCode::DB_ERR_BUFFER_FULL;
    }
    outData = g_pool[g_count - 1].data;
    return ErrorCode::DB_OK;
}

ErrorCode BufferPool::getPageWritable(const std::string& fp,
                                       uint32_t pid, char*& outData) {
    using namespace BPNamespace;
    const char* ro = nullptr;
    ErrorCode err = getPage(fp, pid, ro);
    if (err != ErrorCode::DB_OK) { outData = nullptr; return err; }

    int idx = bp_find(fp, pid);
    if (idx < 0) { outData = nullptr; return ErrorCode::DB_ERR_PAGE_NOT_FOUND; }

    g_pool[idx].dirty = true;
    if (g_pool[idx].pinCount > 0) g_pool[idx].pinCount--;
    outData = g_pool[idx].data;
    return ErrorCode::DB_OK;
}

ErrorCode BufferPool::markDirty(const std::string& fp, uint32_t pid) {
    using namespace BPNamespace;
    if (!g_init) return ErrorCode::DB_INVALID_PARAM;
    int idx = bp_find(fp, pid);
    if (idx < 0) return ErrorCode::DB_ERR_PAGE_NOT_FOUND;
    g_pool[idx].dirty = true;
    return ErrorCode::DB_OK;
}

ErrorCode BufferPool::flushPage(const std::string& fp, uint32_t pid) {
    using namespace BPNamespace;
    if (!g_init) return ErrorCode::DB_INVALID_PARAM;
    int idx = bp_find(fp, pid);
    if (idx < 0) return ErrorCode::DB_ERR_PAGE_NOT_FOUND;
    if (!g_pool[idx].dirty) return ErrorCode::DB_OK;

    std::ofstream ofs(fp, std::ios::binary | std::ios::in | std::ios::out);
    if (!ofs.is_open()) {
        ofs.clear();
        ofs.open(fp, std::ios::binary | std::ios::out);
    }
    if (!ofs.is_open()) return ErrorCode::DB_ERR_FILE_OPEN_FAILED;

    ofs.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE);
    ofs.write(g_pool[idx].data, PAGE_SIZE - sizeof(PageHeader));
    if (!ofs.good()) return ErrorCode::DB_ERR_FILE_WRITE_FAILED;

    g_pool[idx].dirty = false;
    return ErrorCode::DB_OK;
}

void BufferPool::flushAll() {
    using namespace BPNamespace;
    if (!g_init) return;
    for (uint32_t i = 0; i < g_count; ++i) {
        if (g_pool[i].dirty)
            flushPage(g_pool[i].filepath, g_pool[i].pageId);
    }
}

ErrorCode BufferPool::unpin(const std::string& fp, uint32_t pid) {
    using namespace BPNamespace;
    if (!g_init) return ErrorCode::DB_INVALID_PARAM;
    int idx = bp_find(fp, pid);
    if (idx < 0) return ErrorCode::DB_ERR_PAGE_NOT_FOUND;
    if (g_pool[idx].pinCount > 0) g_pool[idx].pinCount--;
    return ErrorCode::DB_OK;
}

uint32_t BufferPool::usedPages()     { return BPNamespace::g_count; }
uint32_t BufferPool::evictionCount() { return BPNamespace::g_evictions; }
uint32_t BufferPool::hitCount()      { return BPNamespace::g_hits; }
uint32_t BufferPool::missCount()     { return BPNamespace::g_misses; }

// ─── fd-based 接口（方案规定签名）────────────────────────

void* BufferPool::GetPage(int fd, uint32_t pageId) {
    std::string fp = FileManager::GetFilePath(fd);
    if (fp.empty()) {
        std::cerr << "[BufferPool] GetPage: invalid fd=" << fd << "\n";
        return nullptr;
    }
    const char* data = nullptr;
    ErrorCode err = getPage(fp, pageId, data);
    if (err != ErrorCode::DB_OK) return nullptr;
    return const_cast<char*>(data);
}

ErrorCode BufferPool::MarkDirty(int fd, uint32_t pageId) {
    std::string fp = FileManager::GetFilePath(fd);
    if (fp.empty()) return ErrorCode::DB_INVALID_PARAM;
    return markDirty(fp, pageId);
}

ErrorCode BufferPool::FlushPage(int fd, uint32_t pageId) {
    std::string fp = FileManager::GetFilePath(fd);
    if (fp.empty()) return ErrorCode::DB_INVALID_PARAM;
    return flushPage(fp, pageId);
}

ErrorCode BufferPool::ReleasePage(int fd, uint32_t pageId) {
    std::string fp = FileManager::GetFilePath(fd);
    if (fp.empty()) return ErrorCode::DB_INVALID_PARAM;
    return unpin(fp, pageId);
}
