#pragma once
#include <string>
#include <vector>

// 17 种 SQL 语句操作类型
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
    BEGIN_TX,        // BEGIN
    COMMIT_TX,       // COMMIT
    ROLLBACK_TX,     // ROLLBACK
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CREATE_VIEW,
    UNKNOWN
};

// ALTER TABLE 操作类型
enum class AlterAction { ADD_COLUMN, DROP_COLUMN, MODIFY_COLUMN };

// 逻辑运算符
enum class LogicOp { AND, OR };

// 单个过滤条件
struct SingleCondition {
    std::string column;
    std::string op;      // =, !=, <, >, <=, >=
    std::string value;
};

// 复合条件过滤表达（支持 AND / OR）
struct WhereClause {
    bool hasWhere = false;
    std::vector<SingleCondition> conditions;  // 条件列表
    std::vector<LogicOp> logicOps;            // conditions[i] 与 conditions[i+1] 的逻辑运算符

    // 向后兼容：返回第一个条件的引用（无 WHERE 时返回空引用）
    const std::string& column() const {
        static const std::string emptyStr;
        return conditions.empty() ? emptyStr : conditions[0].column;
    }
    const std::string& op() const {
        static const std::string emptyStr;
        return conditions.empty() ? emptyStr : conditions[0].op;
    }
    const std::string& value() const {
        static const std::string emptyStr;
        return conditions.empty() ? emptyStr : conditions[0].value;
    }
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
