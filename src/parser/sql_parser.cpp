// src/parser/sql_parser.cpp
#include "parser/sql_parser.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

// 辅助函数：将字符串全部转为大写，因为 SQL 关键字（如 SELECT, create）是不区分大小写的
static std::string toUpperCase(const std::string& str) {
    std::string result = str;
    // 使用 std::transform 配合 std::toupper 进行字符级转换
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::unique_ptr<ASTNode> SqlParser::Parse(const std::string& sql) {
    // 1. 创建我们要返回的语法树节点 (ASTNode)
    // 语法点：使用 std::make_unique 创建智能指针，它会自动管理在这块内存，避免内存泄漏
    auto node = std::make_unique<ASTNode>();
    
    // 2. 字符串流对象：它可以像 cin 一样，以空格为界限一个单词一个单词地“吃”进去
    std::stringstream ss(sql);
    std::string token;
    
    // 读出第一个词，这往往决定了这是一条什么 SQL
    ss >> token; 
    std::string keyword = toUpperCase(token);

    // 3. 核心解析逻辑：依据首个关键字分支
    if (keyword == "CREATE") {
        ss >> token; // 吃掉下一个词，预期是 "TABLE"
        if (toUpperCase(token) == "TABLE") {
            node->type = StmtType::CREATE_TABLE;
            ss >> node->tbl; // TABLE 后面的自然是咱们的表名了，赋值给 tbl 字段
            
            // 此处省略了括号 `(id INT)` 的拆解，真正工业级可以写循环，作为演示框架到此为止
        }
    } 
    else if (keyword == "INSERT") {
        ss >> token; 
        if (toUpperCase(token) == "INTO") {
            node->type = StmtType::INSERT;
            ss >> node->tbl; // 解析被插入的具体表
            // 未来这里会继续读 VALUES 后的数据...
        }
    }
    else if (keyword == "SELECT") {
        node->type = StmtType::SELECT;
        ss >> token; // 往往是 "*" 或者 "id,name"
        node->columns.push_back(token); // 把它作为列存起来
        
        ss >> token; // 预期的 "FROM"
        if (toUpperCase(token) == "FROM") {
            ss >> node->tbl; // 下一个是表名
        }

        // 处理可能存在的 WHERE 语句 (你刚才同意了先搞简单的单条件模式！)
        if (ss >> token && toUpperCase(token) == "WHERE") {
            node->where.hasWhere = true;
            ss >> node->where.column; // 如 'id'
            ss >> node->where.op;     // 如 '='
            ss >> node->where.value;  // 如 '5'
        }
    }
    else {
        // 如果都不是，这是一个无法识别的语句
        node->type = StmtType::UNKNOWN;
    }

    // 4. 将填充满数据的树枝（Node）交出去
    return node;
}

bool SqlParser::Validate(const std::string& sql) {
    // 预检逻辑，只有有基本长度的句子才值得做 Parse 
    return !sql.empty() && sql.length() > 5;
}
