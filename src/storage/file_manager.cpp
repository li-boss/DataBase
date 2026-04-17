// src/storage/file_manager.cpp
#include "../../include/storage/file_manager.h"
#include <filesystem>

namespace fs = std::filesystem;

bool FileManager::createFile(const std::string& filepath) {
    if (fileExists(filepath)) return false;
    // std::ios::binary 是必须的，防止 Windows 系统自动将 \n 转换为 \r\n 破坏二进制对齐
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
    // std::ios::app 保证每次写入都在文件末尾，常用于 INSERT 操作
    std::ofstream ofs(filepath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return false;
    
    ofs.write(reinterpret_cast<const char*>(data), size);
    return ofs.good();
}