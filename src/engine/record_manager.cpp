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
        case StmtType::ALTER_TABLE:
            return DDLExecutor::executeAlterTable(ast);
        case StmtType::SHOW_TABLES:
            return DDLExecutor::showTables();
        case StmtType::CREATE_INDEX:
            return DDLExecutor::executeCreateIndex(ast);
        case StmtType::DROP_INDEX:
            return DDLExecutor::executeDropIndex(ast);
        case StmtType::SHOW_INDEXES:
            return DDLExecutor::executeShowIndexes(ast);
        case StmtType::INSERT:
            return DMLExecutor::insertRecord(ast);
        case StmtType::SELECT:
            return DMLExecutor::selectRecord(ast);
        case StmtType::UPDATE:
            return DMLExecutor::updateRecord(ast);
        case StmtType::DELETE:
            return DMLExecutor::deleteRecord(ast);
        case StmtType::BEGIN_TRANS:
            return DMLExecutor::executeBegin(ast);
        case StmtType::COMMIT_TRANS:
            return DMLExecutor::executeCommit(ast);
        case StmtType::ROLLBACK_TRANS:
            return DMLExecutor::executeRollback(ast);
        case StmtType::CREATE_VIEW:
            return DDLExecutor::executeCreateView(ast);
        default: {
            ExecuteResult res;
            res.error = 1;
            res.msg = "Error: Unknown SQL operation or syntax error.";
            return res;
        }
    }
}

#include <fstream>
#include <sstream>
#include "parser/sql_parser.h"

std::vector<ExecuteResult> RecordManager::ExecuteScript(const std::string& scriptPath) {
    std::vector<ExecuteResult> results;
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        ExecuteResult res;
        res.error = 1;
        res.msg = "Error: Cannot open script file: " + scriptPath;
        results.push_back(res);
        return results;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::stringstream ss(content);
    std::string statement;

    while (std::getline(ss, statement, ';')) {
        std::string trimmed = statement;
        // 去除首尾空白
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

        if (trimmed.empty()) continue;

        if (SqlParser::Validate(trimmed)) {
            auto ast = SqlParser::Parse(trimmed);
            ExecuteResult res = Execute(ast.get());
            results.push_back(res);
        } else {
            ExecuteResult res;
            res.error = 1;
            res.msg = "Error: Invalid SQL statement: " + trimmed;
            results.push_back(res);
        }
    }

    return results;
}
