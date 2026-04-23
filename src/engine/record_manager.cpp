// src/engine/record_manager.cpp
#include "engine/record_manager.h"
#include "engine/ddl_executor.h"

ExecuteResult RecordManager::Execute(const ASTNode* ast) {
    if (!ast) {
        ExecuteResult res;
        res.error = 1;
        res.msg = "Error: Invalid or null SQL AST.";
        return res;
    }

    // 根据 AST 的指令类别，无缝分发给 DDL/DML 执行器
    switch (ast->type) {
        case StmtType::CREATE_DB:
            return DDLExecutor::createDatabase(ast);
        case StmtType::DROP_DB:
            return DDLExecutor::dropDatabase(ast);
        case StmtType::USE_DB:
            return DDLExecutor::useDatabase(ast);
        case StmtType::CREATE_TABLE:
            return DDLExecutor::executeCreateTable(ast);
        case StmtType::DROP_TABLE:
            return DDLExecutor::executeDropTable(ast);
        case StmtType::SHOW_TABLES:
            return DDLExecutor::showTables();
        case StmtType::INSERT:
            return DMLExecutor::insertRecord(ast);
        case StmtType::SELECT:
            return DMLExecutor::selectRecord(ast);
        case StmtType::UPDATE:
            return DMLExecutor::updateRecord(ast);
        case StmtType::DELETE:
            return DMLExecutor::deleteRecord(ast);
        default: {
            ExecuteResult res;
            res.error = 1;
            res.msg = "Error: Unknown SQL operation or syntax error.";
            return res;
        }
    }
}
