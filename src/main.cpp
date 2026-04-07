// src/main.cpp
#include <iostream>
#include <string>

int main() {
    std::cout << "=== RuankoDB MVP Version 1.0 ===" << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;
    
    // 这里预留给未来的 REPL (读取-执行-输出) 循环
    std::string input;
    while (true) {
        std::cout << "RuankoDB> ";
        if (!std::getline(std::cin, input) || input == "exit") {
            break;
        }
        // TODO: 交给 Parser 解析并执行
    }
    
    std::cout << "Bye." << std::endl;
    return 0;
}