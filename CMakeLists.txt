# 1. 基础配置
cmake_minimum_required(VERSION 3.14)
project(RuankoDB VERSION 1.0 LANGUAGES CXX)

# 强制使用 C++17 标准 (以便后续可能使用 std::filesystem 和高级对齐特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置输出目录结构 (防止编译产物污染源码树)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 2. 头文件搜索路径
# 将 include 目录暴露给所有目标，代码中可直接 #include "storage/file_manager.h"
include_directories(${CMAKE_SOURCE_DIR}/include)

# 3. 源码收集与核心库构建 (RuankoCore)
# 采用显式指定源码的方式，比 GLOB 更安全，避免幽灵文件问题
set(STORAGE_SRC
    src/storage/file_manager.cpp
    # src/storage/buffer_pool.cpp
    # src/storage/dict_manager.cpp
)

set(ENGINE_SRC
    # src/engine/record_manager.cpp
    # src/engine/ddl_executor.cpp
    # src/engine/dml_executor.cpp
)

set(PARSER_SRC
    # src/parser/sql_parser.cpp
)

set(ACCESS_SRC
    # src/access/cli_handler.cpp
)

# 将所有核心业务逻辑编译为一个静态库，彻底与 main.cpp 解耦
add_library(RuankoCore STATIC
    ${STORAGE_SRC}
    ${ENGINE_SRC}
    ${PARSER_SRC}
    ${ACCESS_SRC}
)

# 4. 主程序构建 (DBMS 终端入口)
add_executable(RuankoDB src/main.cpp)
# 将核心逻辑链接到主程序
target_link_libraries(RuankoDB PRIVATE RuankoCore)


# 5. 测试工程构建 (CTest)
# 开启 CTest 测试支持
enable_testing()

# 第一阶段：存储层与 4 字节对齐 I/O 测试
add_executable(test_file_io tests/test_file_io.cpp)
target_link_libraries(test_file_io PRIVATE RuankoCore)
add_test(NAME Storage_FileIO_Test COMMAND test_file_io)

# 后续测试用例预留...
# add_executable(test_record_rw tests/test_record_rw.cpp)
# target_link_libraries(test_record_rw PRIVATE RuankoCore)
# add_test(NAME Engine_RecordRW_Test COMMAND test_record_rw)