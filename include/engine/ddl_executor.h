// include/engine/ddl_executor.h
#pragma once
#include <string>
#include <vector>
#include "common/db_structs.h"
#include "engine/dml_executor.h" // For ExecuteResult

class DDLExecutor {
public:
    // 原有的核心建表底层实现
    static bool createTable(const std::string& tableName, const std::vector<ColumnDef>& fields);
    static bool dropTable(const std::string& tableName);

    // 新增：接收 AST 树的顶层统一 DDL 接口
    static ExecuteResult executeCreateTable(const ASTNode* ast);
    static ExecuteResult executeDropTable(const ASTNode* ast);
    
    // 依据任务清单新增的库级别接口
    static ExecuteResult createDatabase(const ASTNode* ast);
    static ExecuteResult dropDatabase(const ASTNode* ast);
    static ExecuteResult useDatabase(const ASTNode* ast);
    static ExecuteResult showTables();
};
