// src/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include "parser/sql_parser.h"
#include "engine/record_manager.h"
#include "storage/buffer_pool.h"
#include "server/http_server.h"

int main() {
    // 启动前初始化缓冲池 (例如 64 页容量)
    BufferPool::init(64);
    
    std::cout << "=== RuankoDB Booting (HTTP Mode) ===" << std::endl;
    
    // 启动 HTTP 服务，阻塞线程
    Ruanko::HttpServer::Start(8080);
    
    BufferPool::shutdown();
    std::cout << "Bye." << std::endl;
    return 0;
}
