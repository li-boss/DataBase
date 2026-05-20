// include/storage/buffer_pool.h
#pragma once

#include <string>
#include <cstdint>

#include "../common/db_errors.h"

// 页头布局（写在 PAGE_DATA 前面）
struct PageHeader {
    uint32_t pageId;   // 页编号
    uint32_t dirty;    // 脏页标记（0=干净, 1=脏）
};

// 单个缓存页槽位
struct PageSlot {
    char      data[4080];  // 实际页面数据（4096 - sizeof(PageHeader)）
    PageHeader header;      // 页头
    bool      valid;       // 是否已加载有效数据
    uint32_t  pinCount;    // 被 pin（固定）的次数，0 可淘汰

    PageSlot() : valid(false), pinCount(0) {}
};

/**
 * @file buffer_pool.h
 * @brief BufferPool 全局单例：LRU 页面缓存
 *
 * 同时支持两套调用方式：
 *   1. fd-based（方案规定）：GetPage(fd, pid) / MarkDirty(fd, pid) 等
 *   2. filepath-based（内部兼容）：getPage(filepath, pid) 等
 *
 * fd 通过 FileManager::OpenFile 获取。
 */
class BufferPool {
public:
    // ─── 生命周期 ────────────────────────────────────────

    /**
     * @brief 初始化缓冲池
     * @param maxPages 最大缓存页数（不超过 MAX_PAGES）
     */
    static ErrorCode init(uint32_t maxPages = 16);

    /**
     * @brief 关闭缓冲池：将所有脏页写回磁盘，释放内存
     */
    static void shutdown();

    // ─── fd-based 接口（方案规定）────────────────────────

    /**
     * @brief 通过 fd 获取指定页（方案规定签名）
     * @param fd     由 FileManager::OpenFile 返回的文件描述符
     * @param pageId 页号（从 0 开始）
     * @return 指向页数据的 void 指针；nullptr 表示失败
     */
    static void* GetPage(int fd, uint32_t pageId);

    /**
     * @brief 通过 fd 标记脏页（方案规定签名）
     */
    static ErrorCode MarkDirty(int fd, uint32_t pageId);

    /**
     * @brief 通过 fd 将指定页写回磁盘（方案规定签名）
     */
    static ErrorCode FlushPage(int fd, uint32_t pageId);

    /**
     * @brief 通过 fd 解除页面固定（方案规定命名：ReleasePage）
     */
    static ErrorCode ReleasePage(int fd, uint32_t pageId);

    // ─── filepath-based 接口（保持兼容）─────────────────

    static ErrorCode getPage(const std::string& filepath, uint32_t pageId,
                              const char*& outData);

    static ErrorCode getPageWritable(const std::string& filepath, uint32_t pageId,
                                      char*& outData);

    static ErrorCode markDirty(const std::string& filepath, uint32_t pageId);

    static ErrorCode flushPage(const std::string& filepath, uint32_t pageId);

    static void flushAll();

    /**
     * @brief 驱逐指定文件的所有缓存页（用于 ROLLBACK / DROP 后强制重新读盘）
     */
    static void InvalidateFile(const std::string& filepath);

    static ErrorCode unpin(const std::string& filepath, uint32_t pageId);

    // ─── 工具 ────────────────────────────────────────────

    static uint32_t usedPages();
    static uint32_t evictionCount();
    static uint32_t hitCount();
    static uint32_t missCount();
};
