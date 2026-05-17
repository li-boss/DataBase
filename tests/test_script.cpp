#include <iostream>
#include <fstream>
#include "engine/record_manager.h"
#include "storage/buffer_pool.h"

int main() {
    BufferPool::init(64);
    
    std::cout << "=== Running SQL Script Test ===" << std::endl;
    
    // 创建测试脚本文件
    std::ofstream file("test.sql");
    file << "CREATE DATABASE test_db;\n";
    file << "USE test_db;\n";
    file << "CREATE TABLE users (id INT, name VARCHAR);\n";
    file << "INSERT INTO users VALUES (1, 'alice');\n";
    file << "INSERT INTO users VALUES (2, 'bob');\n";
    file << "SELECT * FROM users;\n";
    file << "SELECT name FROM users WHERE id = 1;\n";
    file << "UPDATE users SET name = 'charlie' WHERE id = 2;\n";
    file << "SELECT * FROM users;\n";
    file << "DELETE FROM users WHERE id = 1;\n";
    file << "SELECT * FROM users;\n";
    file << "CREATE INDEX idx_id ON users(id);\n";
    file << "SHOW INDEXES FROM users;\n";
    file << "BEGIN;\n";
    file << "COMMIT;\n";
    file << "CREATE VIEW v_users AS SELECT * FROM users;\n";
    file.close();
    
    // 执行脚本
    auto results = RecordManager::ExecuteScript("test.sql");
    
    // 输出结果
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "Statement " << i + 1 << ": " << results[i].msg << std::endl;
        if (!results[i].rows.empty()) {
            std::cout << "Data:" << std::endl;
            for (const auto& row : results[i].rows) {
                for (const auto& col : row) {
                    std::cout << col << "\t";
                }
                std::cout << std::endl;
            }
        }
    }
    std::cout << "----------------------------------------" << std::endl;
    
    BufferPool::shutdown();
    return 0;
}
