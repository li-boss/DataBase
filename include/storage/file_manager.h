// include/storage/file_manager.h
#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>

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
};
