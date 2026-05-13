#pragma once
#include <string>
#include <vector>

// 14 种 SQL 语句操作类型
enum class StmtType {
    CREATE_DB,
    DROP_DB,
    USE_DB,
    CREATE_TABLE,
    DROP_TABLE,
    SHOW_TABLES,
    ALTER_TABLE,     // ALTER TABLE ... ADD/DROP/MODIFY COLUMN ...
    CREATE_INDEX,    // CREATE INDEX <idx> ON <tbl>(<col>)
    DROP_INDEX,      // DROP INDEX <idx>
    SHOW_INDEXES,    // SHOW INDEXES <tbl> 或 SHOW INDEX FROM <tbl>
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    UNKNOWN
};

// ALTER TABLE 操作类型
enum class AlterAction { ADD_COLUMN, DROP_COLUMN, MODIFY_COLUMN };

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
    // ALTER TABLE 专用
    AlterAction alterAction;
    std::string alterColumnName;   // 目标列名
    std::string alterColumnType;  // 新类型 (ADD/MODIFY 时使用)
    bool alterNotNull = false;     // 是否带 NOT NULL 约束
    bool alterPrimaryKey = false;  // 是否为主键
};
