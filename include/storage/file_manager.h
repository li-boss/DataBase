// include/storage/file_manager.h
#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <vector>                   // for std::vector
#include "../common/db_structs.h"   // for IndexHeader

/**
 * @file file_manager.h
 * @brief RuankoDB 文件管理器
 *
 * 提供两套接口：
 *   1. fd-based（方案规定）：OpenFile / CloseFile / ReadPage / WritePage
 *      - fd 为整型文件句柄，内部维护 fd→filepath 映射
 *      - 页号从 0 开始，页大小由 db_types.h 的 PAGE_SIZE 决定
 *   2. filepath-based（内部使用）：createFile / deleteFile / fileExists
 *      以及模板方法 writeStruct / readStruct / appendBlock
 *
 * ─── 索引文件（.idx）格式设计 ─────────────────────
 * 文件名：<db_dir>/<index_name>.idx
 *
 * 磁盘布局（字节序：little-endian）：
 * ┌──────────────────────────────────────────┐
 * │  IndexHeader（固定 436 字节）           │
 * │  sizeof(IndexHeader) = 128*3 + 4*5 + 4*8 = 436 │
 * ├──────────────────────────────────────────┤
 * │  IndexEntry 数组（变长，entryCount 条）│
 * │  每条长度 = keySize + 4 字节            │
 * │  布局：[ keyData: keySize 字节 ]        │
 * │        [ recordOffset: uint32_t ]        │
 * └──────────────────────────────────────────┘
 *
 * IndexHeader 字段说明：
 *   indexName[MAX_NAME_LEN] : 索引名（如 idx_students_age）
 *   tableName[MAX_NAME_LEN] : 所属表名
 *   columnName[MAX_NAME_LEN]: 索引字段名
 *   columnIndex             : 字段在表中的序号
 *   keyType                : 索引键类型（复用 DataType 枚举）
 *   entryCount             : 当前索引条目数
 *   createTime             : 创建时间（Unix 时间戳）
 *   reserved[8]            : 保留字段，对齐用（keySize 不存于头部，按 keyType 现场计算）
 *
 * Key 存储规则（第一版）：
 *   TYPE_INT    (1): 定长 4 字节，直接存 uint32_t
 *   TYPE_CHAR   (2): 定长，长度 = ColumnDef.length
 *   TYPE_VARCHAR(3): 暂不实现（或长度前缀 + 数据）
 *   TYPE_FLOAT  (6): 定长 4 字节
 *   TYPE_DOUBLE (7): 定长 8 字节
 *
 * 第一版约束：
 *   - 仅支持单字段索引
 *   - 仅支持等值查询（WHERE col = value）
 *   - 查找使用线性扫描（条目少时足够演示）
 *   - 索引文件不使用 BufferPool，直接 fstream 读写
 *   - 后续可升级为 B+ 树结构
 *
 * 第二版（B+ Tree）：
 *   - 已升级为 B+ Tree 索引结构（内部委托给 BPlusTree）
 *   - 支持范围查询（<, >, <=, >=），通过叶子链表 O(log N + K)
 *   - IndexHeader.reserved[0..2] 存储 B+ Tree 元数据（rootPageId/nextPageId/keySize）
 */

class FileManager {
public:

    // ─── fd-based 接口（方案规定）────────────────────────

    /**
     * @brief 打开文件，返回整型文件描述符
     * @param path  文件路径
     * @param mode  "r" = 只读，"rw" = 读写（不存在则创建）
     * @param outFd 输出：文件描述符（>= 0 为有效）
     * @return true 成功
     */
    static bool OpenFile(const std::string& path,
                         const std::string& mode,
                         int& outFd);

    /**
     * @brief 关闭文件句柄，释放资源
     * @param fd 文件描述符
     * @return true 成功
     */
    static bool CloseFile(int fd);

    /**
     * @brief 读取指定页到缓冲区（页大小由 PAGE_SIZE 决定）
     * @param fd    文件描述符
     * @param pageId 页号（从 0 开始）
     * @param buf   输出缓冲区（调用方保证至少 PAGE_SIZE 字节）
     * @return true 成功；false 页不存在或 IO 错误
     */
    static bool ReadPage(int fd, uint32_t pageId, char* buf);

    /**
     * @brief 将缓冲区内容写入指定页（不足则扩展文件）
     * @param fd    文件描述符
     * @param pageId 页号
     * @param buf   数据缓冲区（PAGE_SIZE 字节）
     * @return true 成功
     */
    static bool WritePage(int fd, uint32_t pageId, const char* buf);

    /**
     * @brief 通过 fd 获取对应的文件路径（BufferPool 需要）
     * @param fd 文件描述符
     * @return 文件路径；若无效返回空字符串
     */
    static std::string GetFilePath(int fd);

    // ─── filepath-based 接口（内部使用）──────────────────

    static bool createFile(const std::string& filepath);
    static bool deleteFile(const std::string& filepath);
    static bool fileExists(const std::string& filepath);

    // 在指定偏移量写入结构体，且不破坏文件其余部分
    template<typename T>
    static bool writeStruct(const std::string& filepath, const T& data,
                             std::streampos offset = 0) {
        std::ofstream ofs(filepath, std::ios::binary | std::ios::in | std::ios::out);
        if (!ofs.is_open()) {
            ofs.clear();
            ofs.open(filepath, std::ios::binary | std::ios::out);
        }
        if (!ofs.is_open()) return false;
        ofs.seekp(offset);
        ofs.write(reinterpret_cast<const char*>(&data), sizeof(T));
        return ofs.good();
    }

    // 从指定偏移量读取结构体
    template<typename T>
    static bool readStruct(const std::string& filepath, T& data,
                            std::streampos offset = 0) {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs.is_open()) return false;
        ifs.seekg(offset);
        ifs.read(reinterpret_cast<char*>(&data), sizeof(T));
        return ifs.gcount() == static_cast<std::streamsize>(sizeof(T));
    }

    // 向文件末尾追加不定长字节块（用于 .trd 数据文件追加记录）
    static bool appendBlock(const std::string& filepath, const void* data, size_t size);

    // ─── 索引文件读写（.idx）────────────────────────────
    // 索引文件布局：IndexHeader（436字节）+ IndexEntry 数组（变长）
    // IndexEntry = [keyData: keySize 字节][recordOffset: uint32_t]

    /**
     * @brief 创建索引文件并写入 IndexHeader
     * @param idxPath   索引文件路径（.idx）
     * @param hdr       IndexHeader 内容
     * @return true 成功
     */
    static bool createIndexFile(const std::string& idxPath, const IndexHeader& hdr);

    /**
     * @brief 重写索引文件的 IndexHeader（用于更新 entryCount 等字段）
     * @param idxPath  索引文件路径
     * @param hdr      IndexHeader 内容
     * @return true 成功
     */
    static bool writeIndexHeader(const std::string& idxPath, const IndexHeader& hdr);

    /**
     * @brief 读取索引文件的 IndexHeader
     * @param idxPath  索引文件路径
     * @param outHdr   输出：IndexHeader
     * @return true 成功
     */
    static bool readIndexHeader(const std::string& idxPath, IndexHeader& outHdr);

    /**
     * @brief 向索引文件追加一条 IndexEntry
     * @param idxPath       索引文件路径
     * @param keyData       键数据指针（长度 = keySize）
     * @param keySize       键字节数
     * @param recordOffset  对应的记录偏移量
     * @return true 成功
     */
    static bool appendIndexEntry(const std::string& idxPath,
                                 const void* keyData,
                                 uint32_t keySize,
                                 uint32_t recordOffset);

    /**
     * @brief 查找索引条目（线性扫描，第一版）
     * @param idxPath       索引文件路径
     * @param keyData       查找键数据指针
     * @param keySize       键字节数
     * @param outOffsets    输出：匹配的 recordOffset 列表（支持重复键）
     * @return true 找到至少一条
     */
    static bool lookupIndexEntry(const std::string& idxPath,
                                 const void* keyData,
                                 uint32_t keySize,
                                 std::vector<uint32_t>& outOffsets);

    /**
     * @brief 删除索引条目（重建文件方式）
     *        遍历所有条目，跳过 key + recordOffset 精确匹配的条目，重写 .idx 文件
     * @param idxPath       索引文件路径
     * @param keyData       要删除的键数据指针
     * @param keySize       键字节数
     * @param recordOffset  要删除的记录偏移量
     * @return true 成功（即使没找到也算成功）
     */
    static bool removeIndexEntry(const std::string& idxPath,
                                 const void* keyData,
                                 uint32_t keySize,
                                 uint32_t recordOffset);

    /**
     * @brief 范围查找索引条目（线性扫描，支持 <, >, <=, >=）
     * @param idxPath       索引文件路径
     * @param keyData       查找键数据指针
     * @param keySize       键字节数
     * @param keyType       键类型（DataType 枚举值，用于数值比较）
     * @param op            比较运算符（<, >, <=, >=）
     * @param outOffsets    输出：匹配的 recordOffset 列表
     * @return true 找到至少一条
     */
    static bool lookupIndexRange(const std::string& idxPath,
                                 const void* keyData,
                                 uint32_t keySize,
                                 uint32_t keyType,
                                 const std::string& op,
                                 std::vector<uint32_t>& outOffsets);

    /**
     * @brief 复合索引前缀范围查找
     *        用于复合索引末列范围查询（如 WHERE a=1 AND b>5 ON INDEX(a,b)）
     *        内部委托给 BPlusTree::searchPrefixRange
     */
    static bool lookupIndexPrefixRange(const std::string& idxPath,
                                       const void* prefixKey,
                                       uint32_t prefixSize,
                                       const void* fullKey,
                                       uint32_t keySize,
                                       const std::string& op,
                                       std::vector<uint32_t>& outOffsets);
};
