// src/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include "parser/sql_parser.h"
#include "engine/record_manager.h"
#include "storage/buffer_pool.h"
#include "server/http_server.h"

// ─── atexit 兜底：确保任何退出路径都会刷缓冲池 ───
static void cleanup() {
    BufferPool::shutdown();
}

int main() {
    std::atexit(cleanup);  // 注册清理函数（包括正常 exit / return 0）

    // 启动前初始化缓冲池 (例如 64 页容量)
    BufferPool::init(64);

    std::cout << "=== RuankoDB Booting (HTTP Mode) ===" << std::endl;

    // 启动 HTTP 服务，阻塞线程（Ctrl+C / 关窗口会触发信号处理优雅退出）
    Ruanko::HttpServer::Start(8080);

    // Start() 返回后（正常或信号停止），刷脏页到磁盘
    BufferPool::shutdown();
    std::cout << "Bye." << std::endl;
    return 0;
}
