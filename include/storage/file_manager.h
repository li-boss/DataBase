// include/storage/file_manager.h
#pragma once
#include <string>
#include <fstream>
#include <iostream>

class FileManager {
public:
    static bool createFile(const std::string& filepath);
    static bool deleteFile(const std::string& filepath);
    static bool fileExists(const std::string& filepath);

    // 在指定偏移量写入结构体，且不破坏文件其余部分
    template<typename T>
    static bool writeStruct(const std::string& filepath, const T& data, std::streampos offset = 0) {
        // 使用 in | out 模式打开已存在的文件可以防止文件被清空（truncate）
        std::ofstream ofs(filepath, std::ios::binary | std::ios::in | std::ios::out);
        if (!ofs.is_open()) {
            // 若文件不存在，则回退为仅创建并输出模式
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
    static bool readStruct(const std::string& filepath, T& data, std::streampos offset = 0) {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs.is_open()) return false;
        
        ifs.seekg(offset);
        ifs.read(reinterpret_cast<char*>(&data), sizeof(T));
        // gcount 返回实际读取的字节数，用于校验文件是否损坏或未写满
        return ifs.gcount() == sizeof(T); 
    }

    // 用于向 .trd 数据文件末尾追加不定长字节块
    static bool appendBlock(const std::string& filepath, const void* data, size_t size);
};