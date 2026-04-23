// src/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include "parser/sql_parser.h"
#include "engine/record_manager.h"

int main() {
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

        // --- 展现层 ---
        if (result.error != 0) {
            std::cerr << result.msg << std::endl;
        } else {
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

    std::cout << "Bye." << std::endl;
    return 0;
}
