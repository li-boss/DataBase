// src/parser/sql_parser.cpp
#include "parser/sql_parser.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

// -----------------------------------------------------------------------------
// 辅助函数：将字符串全部转为大写，因为 SQL 关键字（如 SELECT, CREATE）不区分大小写
// -----------------------------------------------------------------------------
static std::string toUpperCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

// -----------------------------------------------------------------------------
// 辅助函数：去除字符串两端的空白字符以及不需要的符号（如分号、单引号、括号）
// -----------------------------------------------------------------------------
static std::string trimToken(const std::string& str) {
    std::string s = str;
    // 去除两端空格
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    
    // 如果字符串包含分号结尾，去除分号
    if (!s.empty() && s.back() == ';') {
        s.pop_back();
    }
    
    // 去除末尾的逗号或括号
    if (!s.empty() && (s.back() == ',' || s.back() == ')')) {
        s.pop_back();
    }
    
    // 去除开头的括号
    if (!s.empty() && s.front() == '(') {
        s.erase(0, 1);
    }
    
    // 去除单引号包裹（例如字符串 'Alice' 变成 Alice）
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

// -----------------------------------------------------------------------------
// 核心解析器：将原始 SQL 字符串映射为 ASTNode 对象
// -----------------------------------------------------------------------------
std::unique_ptr<ASTNode> SqlParser::Parse(const std::string& sql) {
    // 1. 创建我们要返回的语法树节点 (ASTNode)
    auto node = std::make_unique<ASTNode>();
    
    // 2. 字符串流对象：以空格为界限一个单词一个单词地“吃”进去
    std::stringstream ss(sql);
    std::string token;
    
    // 读出第一个词，这往往决定了这是一条什么 SQL
    ss >> token; 
    std::string keyword = toUpperCase(trimToken(token));

    // =========================================================
    // 3. 核心解析逻辑：依据首个关键字进行分支判断
    // =========================================================
    
    if (keyword == "CREATE") {
        ss >> token; // 下一个词是 "TABLE" 或 "DATABASE"
        std::string subKeyword = toUpperCase(trimToken(token));
        
        if (subKeyword == "TABLE") {
            // 解析 CREATE TABLE <表名> (<列定义>)
            node->type = StmtType::CREATE_TABLE;
            ss >> token;
            node->tbl = trimToken(token); // 提取表名
            
            // 提取括号内的列定义，例如 (id INT, name VARCHAR)
            // 用 istreambuf_iterator 读取剩余全部内容（支持多行 SQL）
            std::string remainder((std::istreambuf_iterator<char>(ss)), {});
            
            size_t start = remainder.find('(');
            size_t end = remainder.rfind(')');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                // 取出括号内所有内容
                std::string fieldsStr = remainder.substr(start + 1, end - start - 1);
                std::stringstream fieldStream(fieldsStr);
                std::string fieldDef;
                
                // 按逗号切分每个字段的定义，例如 "id INT" 和 "name VARCHAR"
                while (std::getline(fieldStream, fieldDef, ',')) {
                    fieldDef = trimToken(fieldDef);
                    if (!fieldDef.empty()) {
                        node->columns.push_back(fieldDef); // 保存至 AST
                    }
                }
            }
        } 
        else if (subKeyword == "DATABASE") {
            // 解析 CREATE DATABASE <数据库名>
            node->type = StmtType::CREATE_DB;
            ss >> token;
            node->db = trimToken(token);
        }
        else if (subKeyword == "INDEX") {
            // 解析 CREATE INDEX <索引名> ON <表名> (<列名>)
            node->type = StmtType::CREATE_INDEX;
            ss >> token;
            node->columns.push_back(trimToken(token)); // 索引名
            
            ss >> token;
            if (toUpperCase(trimToken(token)) == "ON") {
                ss >> token;
                std::string tblAndCol = trimToken(token);
                size_t paren = tblAndCol.find('(');
                if (paren != std::string::npos) {
                    node->tbl = tblAndCol.substr(0, paren);
                    std::string col = tblAndCol.substr(paren + 1);
                    if (col.back() == ')') col = col.substr(0, col.size() - 1);
                    node->columns.push_back(col); // 列名
                } else {
                    node->tbl = tblAndCol;
                    ss >> token;
                    std::string col = trimToken(token);
                    if (col.front() == '(') col = col.substr(1);
                    if (col.back() == ')') col = col.substr(0, col.size() - 1);
                    node->columns.push_back(col); // 列名
                }
            }
        }
        else if (subKeyword == "VIEW") {
            // 解析 CREATE VIEW <视图名> AS <SELECT 语句>
            node->type = StmtType::CREATE_VIEW;
            ss >> token;
            node->columns.push_back(trimToken(token)); // 视图名
            
            ss >> token;
            if (toUpperCase(trimToken(token)) == "AS") {
                // 读取剩余的所有内容作为 SELECT 语句
                std::string selectStr((std::istreambuf_iterator<char>(ss)), {});
                // 去除首尾空白
                selectStr.erase(0, selectStr.find_first_not_of(" \t\n\r"));
                selectStr.erase(selectStr.find_last_not_of(" \t\n\r") + 1);
                node->values.push_back(selectStr); // 存入 values[0]
            }
        }
    } 
    else if (keyword == "DROP") {
        ss >> token;
        std::string subKeyword = toUpperCase(trimToken(token));
        
        if (subKeyword == "TABLE") {
            // 解析 DROP TABLE <表名>
            node->type = StmtType::DROP_TABLE;
            ss >> token;
            node->tbl = trimToken(token);
        } 
        else if (subKeyword == "DATABASE") {
            // 解析 DROP DATABASE <库名>
            node->type = StmtType::DROP_DB;
            ss >> token;
            node->db = trimToken(token);
        }
        else if (subKeyword == "INDEX") {
            // 解析 DROP INDEX <索引名> ON <表名>
            node->type = StmtType::DROP_INDEX;
            ss >> token;
            node->columns.push_back(trimToken(token)); // 索引名
            
            ss >> token;
            if (toUpperCase(trimToken(token)) == "ON") {
                ss >> token;
                node->tbl = trimToken(token); // 表名
            }
        }
    }
    else if (keyword == "USE") {
        // 解析 USE <数据库名>
        node->type = StmtType::USE_DB;
        ss >> token; 
        node->db = trimToken(token);
    }
    else if (keyword == "SHOW") {
        // 解析 SHOW TABLES
        ss >> token;
        std::string sub = toUpperCase(trimToken(token));
        if (sub == "TABLES") {
            node->type = StmtType::SHOW_TABLES;
        } else if (sub == "INDEXES") {
            // 解析 SHOW INDEXES FROM <表名>
            node->type = StmtType::SHOW_INDEXES;
            ss >> token; // 预期 FROM
            if (toUpperCase(trimToken(token)) == "FROM") {
                ss >> token;
                node->tbl = trimToken(token);
            }
        }
    }
    else if (keyword == "INSERT") {
        // 解析 INSERT INTO <表名> [(<列名>...)] VALUES (<值1>, <值2>)
        ss >> token;
        if (toUpperCase(trimToken(token)) == "INTO") {
            node->type = StmtType::INSERT;
            ss >> token;
            node->tbl = trimToken(token); // 提取表名

            // 跳过可选的列名列表 (col1, col2, ...)
            ss >> token;
            if (token.front() == '(') {
                // 跳到对应 ')'（简单括号匹配）
                int depth = 1;
                while (depth > 0 && ss >> token) {
                    if (token.back() == ')') --depth;
                    if (token.find('(') != std::string::npos) ++depth;
                }
                ss >> token; // 读取 VALUES 关键字
            }
            // 此时 token 应为 VALUES
            if (toUpperCase(trimToken(token)) == "VALUES") {
                // 用 istreambuf_iterator 读取剩余全部内容（支持多行）
                std::string remainder((std::istreambuf_iterator<char>(ss)), {});

                // 寻找括号内的数据
                size_t start = remainder.find('(');
                size_t end = remainder.rfind(')');
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string valsStr = remainder.substr(start + 1, end - start - 1);
                    std::stringstream valStream(valsStr);
                    std::string valDef;

                    // 按逗号分割每一个插入的具体值
                    while (std::getline(valStream, valDef, ',')) {
                        node->values.push_back(trimToken(valDef)); // 保存纯数值/字符串
                    }
                }
            }
        }
    }
    else if (keyword == "SELECT") {
        // 解析 SELECT col1, col2 FROM <表名> WHERE <条件>
        node->type = StmtType::SELECT;
        
        std::string columnsStr = "";
        bool foundFrom = false;
        while (ss >> token) {
            std::string upperToken = toUpperCase(trimToken(token));
            if (upperToken == "FROM") {
                foundFrom = true;
                break;
            }
            columnsStr += token + " ";
        }
        
        if (!foundFrom) {
            node->type = StmtType::UNKNOWN;
            return node;
        }

        // 按逗号切分列名
        std::stringstream colStream(columnsStr);
        std::string col;
        while (std::getline(colStream, col, ',')) {
            std::string trimmedCol = trimToken(col);
            if (!trimmedCol.empty()) {
                node->columns.push_back(trimmedCol);
            }
        }
        
        // 提取表名
        ss >> token;
        node->tbl = trimToken(token);

        // 处理可能存在的 WHERE 语句 
        if (ss >> token && toUpperCase(trimToken(token)) == "WHERE") {
            node->where.hasWhere = true;
            ss >> token; node->where.column = trimToken(token); // 例: id
            ss >> token; node->where.op = trimToken(token);     // 例: =
            ss >> token; node->where.value = trimToken(token);  // 例: 5
        }
    }
    else if (keyword == "ALTER") {
        // 解析 ALTER TABLE <表名> ADD/DROP/MODIFY COLUMN <列名> [类型] [约束]
        ss >> token;
        if (toUpperCase(trimToken(token)) == "TABLE") {
            node->type = StmtType::ALTER_TABLE;
            ss >> token;
            node->tbl = trimToken(token); // 表名

            // 读取动作: ADD / DROP / MODIFY
            std::string actionStr;
            if (ss >> token) actionStr = toUpperCase(trimToken(token));

            if (actionStr == "ADD") {
                node->alterAction = AlterAction::ADD_COLUMN;
                // 读取 COLUMN 关键字(可选)
                if (ss >> token && toUpperCase(trimToken(token)) != "COLUMN") {
                    // 不是COLUMN，回退——这是列名
                    node->alterColumnName = trimToken(token);
                    if (ss >> token) node->alterColumnType = trimToken(token); // 类型
                } else {
                    ss >> token; node->alterColumnName = trimToken(token); // 列名
                    if (ss >> token) node->alterColumnType = trimToken(token); // 类型
                }
                // 读取约束
                while (ss >> token) {
                    std::string kw = toUpperCase(trimToken(token));
                    if ((kw == "PRIMARY" || kw == "NOT")) {
                        std::string next;
                        if (kw == "PRIMARY" && ss >> next) { /* skip KEY */ }
                        else if (kw == "NOT" && ss >> next) { /* skip NULL */ }
                        if (kw == "PRIMARY" || kw == "NOT") {
                            if (kw == "PRIMARY") node->alterPrimaryKey = true;
                            if (kw == "NOT") node->alterNotNull = true;
                        }
                        break;
                    }
                    break;
                }
            } else if (actionStr == "DROP") {
                node->alterAction = AlterAction::DROP_COLUMN;
                if (ss >> token && toUpperCase(trimToken(token)) == "COLUMN")
                    ss >> token;
                node->alterColumnName = trimToken(token);
            } else if (actionStr == "MODIFY") {
                node->alterAction = AlterAction::MODIFY_COLUMN;
                if (ss >> token && toUpperCase(trimToken(token)) == "COLUMN")
                    ss >> token;
                node->alterColumnName = trimToken(token);
                if (ss >> token) node->alterColumnType = trimToken(token); // 类型

                // 读取约束（与 ADD 相同逻辑）
                while (ss >> token) {
                    std::string kw = toUpperCase(trimToken(token));
                    std::string next;
                    if ((kw == "PRIMARY" || kw == "NOT")) {
                        if (kw == "PRIMARY" && ss >> next) { /* skip KEY */ }
                        else if (kw == "NOT" && ss >> next) { /* skip NULL */ }
                        if (kw == "PRIMARY") node->alterPrimaryKey = true;
                        if (kw == "NOT") node->alterNotNull = true;
                        break;
                    }
                    break;
                }
            }
        }
    }
    else if (keyword == "UPDATE") {
        // 解析 UPDATE <表名> SET <列名> = <值> [WHERE <列> <操作符> <值>]
        node->type = StmtType::UPDATE;
        ss >> token;
        node->tbl = trimToken(token); // 表名

        ss >> token; // 预期 SET
        if (toUpperCase(trimToken(token)) == "SET") {
            // 读 SET 列名
            ss >> token;
            node->columns.push_back(trimToken(token)); // set column
            // 显式消费 '=' 号（ss >> token 会读到 '='，必须丢弃）
            std::string eqToken;
            if (ss >> eqToken && trimToken(eqToken) != "=") {
                // 如果读到的不是 '='，说明 SQL 格式异常；暂不处理
            }
            // 读 SET 值（支持带引号的字符串）
            ss >> token;
            std::string val = trimToken(token);
            if ((val.front() == '\'' && val.back() == '\'') ||
                (val.front() == '"' && val.back() == '"')) {
                val = val.substr(1, val.size() - 2); // 去引号
            }
            node->values.push_back(val);

            // 可选 WHERE 子句（与 SELECT 相同逻辑）
            if (ss >> token && toUpperCase(trimToken(token)) == "WHERE") {
                node->where.hasWhere = true;
                ss >> token; node->where.column = trimToken(token);
                ss >> token; node->where.op = trimToken(token);
                ss >> token; node->where.value = trimToken(token);
                // 去引号
                std::string& wv = node->where.value;
                if ((wv.front() == '\'' && wv.back() == '\'') || (wv.front() == '"' && wv.back() == '"'))
                    wv = wv.substr(1, wv.size() - 2);
            }
        }
    }
    else if (keyword == "DELETE") {
        // 解析 DELETE FROM <表名> [WHERE <列> <操作符> <值>]
        ss >> token;
        if (toUpperCase(trimToken(token)) == "FROM") {
            node->type = StmtType::DELETE;
            ss >> token;
            node->tbl = trimToken(token);

            // 可选 WHERE 子句
            if (ss >> token && toUpperCase(trimToken(token)) == "WHERE") {
                node->where.hasWhere = true;
                ss >> token; node->where.column = trimToken(token);
                ss >> token; node->where.op = trimToken(token);
                ss >> token; node->where.value = trimToken(token);
                // 去引号
                std::string& wv = node->where.value;
                if ((wv.front() == '\'' && wv.back() == '\'') || (wv.front() == '"' && wv.back() == '"'))
                    wv = wv.substr(1, wv.size() - 2);
            }
        }
    }
    else if (keyword == "BEGIN") {
        node->type = StmtType::BEGIN_TRANS;
    }
    else if (keyword == "COMMIT") {
        node->type = StmtType::COMMIT_TRANS;
    }
    else if (keyword == "ROLLBACK") {
        node->type = StmtType::ROLLBACK_TRANS;
    }
    else {
        // 如果都不匹配，判定为未知语法
        node->type = StmtType::UNKNOWN;
    }

    // 4. 将填充满数据的树枝（Node）交出去
    return node;
}

// -----------------------------------------------------------------------------
// 预检逻辑，只有基本的长度才能做 Parse
// -----------------------------------------------------------------------------
bool SqlParser::Validate(const std::string& sql) {
    return !sql.empty() && sql.length() > 3;
}
