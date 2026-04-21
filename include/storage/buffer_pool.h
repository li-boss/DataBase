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

// BufferPool 全局单例：LRU 页面缓存
class BufferPool {
public:
    // ─── 生命周期 ────────────────────────────────────────

    /**
     * @brief 初始化缓冲池
     * @param maxPages 最大缓存页数（不超过 MAX_PAGES）
     * @return 成功返回 DB_OK
     */
    static ErrorCode init(uint32_t maxPages = 16);

    /**
     * @brief 关闭缓冲池：将所有脏页写回磁盘，释放内存
     */
    static void shutdown();

    // ─── 核心操作 ────────────────────────────────────────

    /**
     * @brief 获取指定文件+页号的页面数据到缓存
     *
     * 如果页已在池中：提升 LRU 优先级（move-to-front）
     * 如果页不在池中：从磁盘加载，必要时驱逐最老的干净页
     *
     * @param filepath    数据文件路径（如 "Users.trd"）
     * @param pageId      页编号（从 0 开始）
     * @param outData     输出：指向页面数据的指针（不可修改）
     * @return DB_OK 成功；DB_ERR_PAGE_NOT_FOUND 页不存在但已预分配槽位
     */
    static ErrorCode getPage(const std::string& filepath, uint32_t pageId,
                              const char*& outData);

    /**
     * @brief 获取可写页面（返回非 const 指针，调用后自动标记脏页）
     */
    static ErrorCode getPageWritable(const std::string& filepath, uint32_t pageId,
                                      char*& outData);

    /**
     * @brief 标记指定页为脏（内容已被修改，需写回磁盘）
     */
    static ErrorCode markDirty(const std::string& filepath, uint32_t pageId);

    /**
     * @brief 将指定页写回磁盘（如果干净则无操作）
     */
    static ErrorCode flushPage(const std::string& filepath, uint32_t pageId);

    /**
     * @brief 将所有脏页写回磁盘（shutdown 前调用）
     */
    static void flushAll();

    /**
     * @brief 解除页面固定（pinCount--），归零时页面可被驱逐
     */
    static ErrorCode unpin(const std::string& filepath, uint32_t pageId);

    // ─── 工具 ────────────────────────────────────────────

    /** @brief 当前池中有效页数 */
    static uint32_t usedPages();

    /** @brief 驱逐统计：累计驱逐页数 */
    static uint32_t evictionCount();

    /** @brief 命中统计：缓存命中次数 */
    static uint32_t hitCount();

    /** @brief 未命中统计：缓存未命中次数 */
    static uint32_t missCount();
};
