# 1. 基础配置
cmake_minimum_required(VERSION 3.14)
project(RuankoDB VERSION 1.0 LANGUAGES CXX)

# 强制使用 C++17 标准 (以便后续可能使用 std::filesystem 和高级对齐特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# === 新增以下 MSVC 专属配置 ===
if(MSVC)
    # 强制编译器以 UTF-8 读取源文件和输出
    add_compile_options(/utf-8)
    # 禁用 C 库函数不安全警告 (strncpy)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
# ===============================

# 设置输出目录结构 (防止编译产物污染源码树)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 2. 头文件搜索路径
# 将 include 目录暴露给所有目标，代码中可直接 #include "storage/file_manager.h"
include_directories(${CMAKE_SOURCE_DIR}/include)

# [用户约定的隔离常量]
# D8 前默认开启 STUB，用于剥离 Dev-B 的依赖：
option(USE_STORAGE_STUB "Use Stub instead of true Storage module" OFF)
if(USE_STORAGE_STUB)
    add_compile_definitions(USE_STORAGE_STUB=1)
endif()

# 3. 源码收集与核心库构建 (RuankoCore)
set(STORAGE_SRC
    src/storage/file_manager.cpp
    src/storage/bplus_tree.cpp
    src/storage/buffer_pool.cpp
    src/storage/dict_manager.cpp
    src/storage/index_manager.cpp
    src/storage/log_manager.cpp
    src/storage/transaction_manager.cpp
)

set(ENGINE_SRC
     src/engine/record_manager.cpp
     src/engine/ddl_executor.cpp
     src/engine/dml_executor.cpp
)

set(PARSER_SRC
     src/parser/sql_parser.cpp
)

set(ACCESS_SRC
    # src/access/cli_handler.cpp
)

set(SERVER_SRC
    src/server/http_server.cpp
)

# 将所有核心业务逻辑编译为一个静态库，彻底与 main.cpp 解耦
add_library(RuankoCore STATIC
    ${STORAGE_SRC}
    ${ENGINE_SRC}
    ${PARSER_SRC}
    ${ACCESS_SRC}
    ${SERVER_SRC}
)

# 4. 主程序构建 (DBMS 终端入口)
add_executable(RuankoDB_v2 src/main.cpp)
# 将核心逻辑链接到主程序
if(WIN32)
    target_link_libraries(RuankoDB_v2 PRIVATE RuankoCore ws2_32 crypt32)
else()
    target_link_libraries(RuankoDB_v2 PRIVATE RuankoCore)
endif()


# 5. 测试工程构建 (CTest)
# 开启 CTest 测试支持
enable_testing()

# 第一阶段：存储层与 4 字节对齐 I/O 测试
add_executable(test_file_io tests/test_file_io.cpp)
target_link_libraries(test_file_io PRIVATE RuankoCore)
add_test(NAME Storage_FileIO_Test COMMAND test_file_io)

# 第二阶段：数据字典管理器测试（覆盖 DDL → DictManager 全链路）
add_executable(test_dict_manager tests/test_dict_manager.cpp)
target_link_libraries(test_dict_manager PRIVATE RuankoCore)
add_test(NAME Storage_DictMgr_Test COMMAND test_dict_manager)

# 第三阶段：记录读写 + BufferPool 测试
add_executable(test_record_rw tests/test_record_rw.cpp)
target_link_libraries(test_record_rw PRIVATE RuankoCore)
add_test(NAME Storage_RecordRW_Test COMMAND test_record_rw)

# 第五阶段：DML + 索引联动集成测试
add_executable(test_dml_index tests/test_dml_index.cpp)
target_link_libraries(test_dml_index PRIVATE RuankoCore)
add_test(NAME DML_Index_Integration_Test COMMAND test_dml_index)
# 第四阶段：脚本批量执行与全链路测试
add_executable(test_script tests/test_script.cpp)
target_link_libraries(test_script PRIVATE RuankoCore)
add_test(NAME Engine_Script_Test COMMAND test_script)
