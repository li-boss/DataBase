// src/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include "parser/sql_parser.h"
#include "engine/record_manager.h"
#include "storage/buffer_pool.h"

int main() {
    // 启动前初始化缓冲池 (例如 64 页容量)
    BufferPool::init(64);
    
    std::cout << "=== RuankoDB Booting ===" << std::endl;
    std::cout << "Welcome to RuankoDB CLI interact interface." << std::endl;
    std::cout << "Type 'exit' or 'quit' to quit." << std::endl;
    std::cout << "Try: CREATE TABLE test  |  SELECT * FROM test" << std::endl;

    while (true) {
        std::cout << "\nRuankoDB> ";
        std::string sql;
        std::getline(std::cin, sql);

        if (sql == "exit" || sql == "quit") {
            break;
        }

        if (!SqlParser::Validate(sql)) {
            std::cout << "Invalid or empty SQL statement." << std::endl;
            continue;
        }

        // 核心流水线 1：交给 Dev-A-Parser 解析为 AST
        auto ast = SqlParser::Parse(sql);
        if (ast->type == StmtType::UNKNOWN) {
            std::cout << "Sorry, could not parse or unmatched SQL syntax." << std::endl;
            continue;
        }

        // 核心流水线 2：交给 Dev-A-Engine 的枢纽 RecordManager 去执行
        ExecuteResult result = RecordManager::Execute(ast.get());
        
        // --- 以下为 Access 展现层假逻辑（原属 Dev-C，写在此处用于测试闭环） ---
        if (result.error != 0) {
            std::cerr << result.msg << std::endl;
        } else {
            // 如果查出来了数据，打印二维表结构
            if (!result.headers.empty()) {
                for (const auto& h : result.headers) {
                    std::cout << h << "\t| ";
                }
                std::cout << "\n----------------------------" << std::endl;
                
                for (const auto& row : result.rows) {
                    for (const auto& col : row) {
                        std::cout << col << "\t| ";
                    }
                    std::cout << std::endl;
                }
            }
            std::cout << result.msg << std::endl;
        }
    }
    
    BufferPool::shutdown();
    std::cout << "Bye." << std::endl;
    return 0;
}
