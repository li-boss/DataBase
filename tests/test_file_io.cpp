// tests/test_file_io.cpp
#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>
#include "../include/common/db_structs.h"
#include "../include/storage/file_manager.h"

namespace fs = std::filesystem;

void test_struct_alignment() {
    std::cout << "[TEST] Checking Struct Alignment..." << std::endl;
    // MAX_NAME_LEN (128) + 5 * uint32_t (20) = 148 字节
    // 148 是 4 的倍数，不需要额外填充
    assert(sizeof(TableHeader) == 148);
    
    // MAX_NAME_LEN (128) + DataType(4) + 4 * uint32_t (16) = 148 字节
    assert(sizeof(ColumnDef) == 148);
    
    std::cout << "  -> Alignment PASS: TableHeader Size = " << sizeof(TableHeader) << " bytes." << std::endl;
}

void test_table_header_io() {
    std::cout << "[TEST] Checking TableHeader File I/O..." << std::endl;
    const std::string test_file = "test_struct.tb";
    
    // 1. 清理可能残留的测试文件
    FileManager::deleteFile(test_file);
    
    // 2. 构造测试数据
    TableHeader write_hdr;
    std::memset(&write_hdr, 0, sizeof(TableHeader)); // 确保内存干净
    std::strncpy(write_hdr.tableName, "UserTable", MAX_NAME_LEN - 1);
    write_hdr.recordCount = 100;
    write_hdr.fieldCount = 5;
    write_hdr.recordSize = 256;
    write_hdr.createTime = 1680000000;
    
    // 3. 写入文件
    bool write_ok = FileManager::writeStruct(test_file, write_hdr, 0);
    assert(write_ok == true);
    
    // 4. 验证物理文件大小是否精准匹配 148 字节
    assert(fs::file_size(test_file) == sizeof(TableHeader));
    
    // 5. 读出并校验数据内容
    TableHeader read_hdr;
    bool read_ok = FileManager::readStruct(test_file, read_hdr, 0);
    assert(read_ok == true);
    
    assert(std::strcmp(read_hdr.tableName, "UserTable") == 0);
    assert(read_hdr.recordCount == 100);
    assert(read_hdr.fieldCount == 5);
    assert(read_hdr.recordSize == 256);
    assert(read_hdr.createTime == 1680000000);
    
    std::cout << "  -> File I/O PASS: Data integrity and file size verified." << std::endl;
    
    // 6. 清理现场
    FileManager::deleteFile(test_file);
}

int main() {
    std::cout << "=== Starting Storage Layer Tests ===" << std::endl;
    test_struct_alignment();
    test_table_header_io();
    std::cout << "=== All Tests Passed Successfully ===" << std::endl;
    return 0;
}