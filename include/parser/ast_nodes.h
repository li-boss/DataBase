#pragma once
#include <string>
#include <vector>

// 11 种 SQL 语句操作类型
enum class StmtType {
    CREATE_DB,
    DROP_DB,
    USE_DB,
    CREATE_TABLE,
    DROP_TABLE,
    SHOW_TABLES,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    UNKNOWN
};

// 简单的单条件过滤表达，比如 id = 5
struct WhereClause {
    bool hasWhere = false;
    std::string column;
    std::string op;
    std::string value;
};

// 抽象语法树节点 (AST)，所有有效 SQL 最终都会被转成这个结构体交给 Engine 处理
struct ASTNode {
    StmtType type = StmtType::UNKNOWN;
    std::string db;
    std::string tbl;
    std::vector<std::string> columns;
    std::vector<std::string> values;
    WhereClause where;
};
