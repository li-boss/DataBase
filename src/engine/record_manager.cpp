// src/engine/record_manager.cpp
#include "engine/record_manager.h"
#include "engine/ddl_executor.h"
#include "engine/dml_executor.h"
#include "storage/index_manager.h"
#include "storage/dict_manager.h"
#include "storage/log_manager.h"
#include "storage/transaction_manager.h"
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
        case StmtType::CREATE_DB: {
            auto r = DDLExecutor::createDatabase(ast);
            LogManager::log(LogManager::OpType::CREATE_DB,
                "database=" + ast->db + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::DROP_DB: {
            auto r = DDLExecutor::dropDatabase(ast);
            LogManager::log(LogManager::OpType::DROP_DB,
                "database=" + ast->db + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::USE_DB: {
            auto r = DDLExecutor::useDatabase(ast);
            LogManager::log(LogManager::OpType::USE_DB,
                "database=" + ast->db + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::CREATE_TABLE: {
            auto r = DDLExecutor::executeCreateTable(ast);
            LogManager::log(LogManager::OpType::CREATE_TABLE,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::DROP_TABLE: {
            auto r = DDLExecutor::executeDropTable(ast);
            LogManager::log(LogManager::OpType::DROP_TABLE,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::ALTER_TABLE: {
            auto r = DDLExecutor::executeAlterTable(ast);
            LogManager::log(LogManager::OpType::ALTER_TABLE,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::SHOW_TABLES:
            return DDLExecutor::showTables();
        case StmtType::CREATE_INDEX: {
            ExecuteResult r;
            std::string idxName = ast->db;
            std::string tblName = ast->tbl;
            std::string colName = ast->columns.empty() ? "" : ast->columns[0];
            ErrorCode ec = IndexManager::CreateIndex(idxName, tblName, colName);
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Index '" + idxName + "' created on " + tblName + "(" + colName + ").";
                LogManager::log(LogManager::OpType::CREATE_INDEX, r.msg);
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
                LogManager::error(LogManager::OpType::CREATE_INDEX, r.msg);
            }
            return r;
        }
        case StmtType::DROP_INDEX: {
            ExecuteResult r;
            std::string idxName = ast->db;
            ErrorCode ec = IndexManager::DropIndex(idxName);
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Index '" + idxName + "' dropped.";
                LogManager::log(LogManager::OpType::DROP_INDEX, r.msg);
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
                LogManager::error(LogManager::OpType::DROP_INDEX, r.msg);
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
        case StmtType::INSERT: {
            auto r = DMLExecutor::insertRecord(ast);
            LogManager::log(LogManager::OpType::INSERT,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::BEGIN_TX: {
            ExecuteResult r;
            // 如果 BEGIN 后面跟了表名，用那个表名；否则用当前活跃的表
            std::string tbl = ast->tbl;
            if (tbl.empty()) {
                // 尝试从事务上下文推断，或返回提示
                r.msg = "Error: BEGIN TRANSACTION <table_name> — please specify a table.";
                r.error = 1;
                LogManager::error(LogManager::OpType::BEGIN_TX, "No table specified");
                return r;
            }
            ErrorCode ec = TransactionManager::begin(tbl);
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Transaction started on '" + tbl + "'.";
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
            }
            return r;
        }
        case StmtType::COMMIT_TX: {
            ExecuteResult r;
            ErrorCode ec = TransactionManager::commit();
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Transaction committed.";
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
            }
            return r;
        }
        case StmtType::ROLLBACK_TX: {
            ExecuteResult r;
            ErrorCode ec = TransactionManager::rollback();
            if (ec == ErrorCode::DB_OK) {
                r.msg = "Query OK: Transaction rolled back.";
            } else {
                r.error = static_cast<int>(ec);
                r.msg = "Error: " + std::string(getErrorMessage(ec));
            }
            return r;
        }
        case StmtType::SELECT:
            return DMLExecutor::selectRecord(ast);
        case StmtType::UPDATE: {
            auto r = DMLExecutor::updateRecord(ast);
            LogManager::log(LogManager::OpType::UPDATE,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        case StmtType::DELETE: {
            auto r = DMLExecutor::deleteRecord(ast);
            LogManager::log(LogManager::OpType::DELETE,
                "table=" + ast->tbl + (r.error ? " FAILED" : " OK"),
                r.error ? LogManager::Level::ERROR : LogManager::Level::INFO);
            return r;
        }
        default: {
            ExecuteResult res;
            res.error = 1;
            res.msg = "Error: Unknown SQL operation or syntax error.";
            return res;
        }
    }
}
