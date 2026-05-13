// src/engine/record_manager.cpp
#include "engine/record_manager.h"
#include "engine/ddl_executor.h"
#include "engine/dml_executor.h"
#include "storage/index_manager.h"
#include "storage/dict_manager.h"
#include "common/db_errors.h"

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
        case StmtType::CREATE_INDEX: {
            ExecuteResult r;
            ErrorCode ec = IndexManager::CreateIndex(ast->db, ast->tbl,
                ast->columns.empty() ? "" : ast->columns[0]);
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Index '" + ast->db + "' created on " + ast->tbl + ".";
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
            }
            return r;
        }
        case StmtType::DROP_INDEX: {
            ExecuteResult r;
            ErrorCode ec = IndexManager::DropIndex(ast->db);
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Index '" + ast->db + "' dropped.";
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
            }
            return r;
        }
        case StmtType::SHOW_INDEXES: {
            ExecuteResult r;
            std::vector<std::string> idxNames;
            ErrorCode ec = IndexManager::ListIndexes(ast->tbl, idxNames);
            if (ec != ErrorCode::DB_OK) {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
                return r;
            }
            r.headers = {"index_name", "table_name", "column_name"};
            for (const auto& idxName : idxNames) {
                IndexHeader hdr;
                if (DictManager::GetIndexHeader(idxName, hdr) == ErrorCode::DB_OK) {
                    r.rows.push_back({std::string(hdr.indexName),
                                       std::string(hdr.tableName),
                                       std::string(hdr.columnName)});
                }
            }
            r.msg = "Query OK: " + std::to_string(idxNames.size()) + " index(es) on " + ast->tbl;
            return r;
        }
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
