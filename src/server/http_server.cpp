// src/server/http_server.cpp
#include "server/http_server.h"
#include "third_party/httplib.h"

// 避免 windows.h 的 DELETE 宏与 AST 的 DELETE 枚举冲突
#ifdef DELETE
#undef DELETE
#endif

#include "third_party/nlohmann/json.hpp"
#include "parser/sql_parser.h"
#include "engine/record_manager.h"
#include "storage/dict_manager.h"
#include "storage/file_manager.h"
#include "storage/buffer_pool.h"
#include <iostream>
#include <filesystem>
#include <sstream>

using json = nlohmann::json;

namespace Ruanko {

static json ExecSql(const std::string& sql) {
    json res;
    auto ast = SqlParser::Parse(sql);
    if (ast->type == StmtType::UNKNOWN) {
        res["ok"] = false;
        res["error"] = "Could not parse or unmatched SQL syntax.";
        return res;
    }
    
    ExecuteResult execRes = RecordManager::Execute(ast.get());
    if (execRes.error != 0) {
        res["ok"] = false;
        res["error"] = execRes.msg;
    } else {
        res["ok"] = true;
        res["msg"] = execRes.msg;
        res["headers"] = execRes.headers;
        res["rows"] = execRes.rows;
    }
    return res;
}

void HttpServer::Start(int port) {
    httplib::Server svr;

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_content("", "text/plain");
    });

    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // 1. 列出所有数据库（仅识别包含 ruanko.db 元文件的目录）
    svr.Get("/api/databases", [](const httplib::Request&, httplib::Response& res) {
        json j;
        j["ok"] = true;
        std::vector<std::string> dbs;
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_directory()) {
                std::string metaFile = entry.path().string() + "/ruanko.db";
                if (std::filesystem::exists(metaFile)) {
                    dbs.push_back(entry.path().filename().string());
                }
            }
        }
        j["databases"] = dbs;
        res.set_content(j.dump(), "application/json");
    });

    // 2. 创建数据库
    svr.Post("/api/database", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        std::string name = body.value("name", "");
        json j = ExecSql("CREATE DATABASE " + name + ";");
        res.set_content(j.dump(), "application/json");
    });

    // 3. 切换使用数据库
    svr.Post(R"(/api/use/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        json j = ExecSql("USE " + name + ";");
        res.set_content(j.dump(), "application/json");
    });

    // 4. 列出当前数据库内所有表
    svr.Get("/api/tables", [](const httplib::Request&, httplib::Response& res) {
        json j = ExecSql("SHOW TABLES;");
        if (j["ok"] == true) {
            std::vector<std::string> tableList;
            for (const auto& row : j["rows"]) {
                if (!row.empty()) tableList.push_back(row[0].get<std::string>());
            }
            j["tables"] = tableList;
        }
        res.set_content(j.dump(), "application/json");
    });

    // 5. 创建表
    svr.Post("/api/table", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        std::string name = body.value("name", "");
        std::string sql = "CREATE TABLE " + name + " (";
        auto cols = body["columns"];
        for (size_t i = 0; i < cols.size(); ++i) {
            sql += cols[i].value("name", "") + " " + cols[i].value("type", "");
            if (cols[i].value("primaryKey", false)) {
                sql += " PRIMARY KEY";
            }
            if (cols[i].value("notNull", false)) {
                sql += " NOT NULL";
            }
            if (i < cols.size() - 1) sql += ", ";
        }
        sql += ");";
        json j = ExecSql(sql);
        res.set_content(j.dump(), "application/json");
    });

    // 6. 删除表
    svr.Delete(R"(/api/table/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        json j = ExecSql("DROP TABLE " + name + ";");
        res.set_content(j.dump(), "application/json");
    });

    // 6.2 修改表结构 (ALTER TABLE)
    svr.Put("/api/alter-table", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        std::string table   = body.value("table", "");
        std::string action  = body.value("action", "");  // "add" | "drop" | "modify"
        std::string colName  = body.value("column", "");
        std::string colType  = body.value("type", "");
        bool notNull  = body.value("notNull", false);
        bool pk       = body.value("primaryKey", false);

        std::string upperAct = action;
        for (auto& c : upperAct) c = static_cast<unsigned char>(std::toupper(c));

        std::string sql;
        if (upperAct == "ADD") {
            sql = "ALTER TABLE " + table + " ADD COLUMN " + colName + " " + colType;
            if (pk)       sql += " PRIMARY KEY";
            if (notNull)   sql += " NOT NULL";
        } else if (upperAct == "DROP") {
            sql = "ALTER TABLE " + table + " DROP COLUMN " + colName;
        } else if (upperAct == "MODIFY") {
            sql = "ALTER TABLE " + table + " MODIFY COLUMN " + colName + " " + colType;
        }
        sql += ";";
        json j = ExecSql(sql);
        res.set_content(j.dump(), "application/json");
    });

    // 6.5 获取表结构
    svr.Get(R"(/api/schema/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string table = req.matches[1];
        TableHeader header;
        std::vector<ColumnDef> fields;
        ErrorCode err = DictManager::loadTable(table, header, fields);
        json j;
        if (err == ErrorCode::DB_OK) {
            j["ok"] = true;
            json cols = json::array();
            for (const auto& f : fields) {
                json col;
                col["name"] = f.fieldName;
                switch (f.type) {
                    case DataType::TYPE_INT:    col["type"] = "INT"; break;
                    case DataType::TYPE_CHAR:   col["type"] = "CHAR"; break;
                    case DataType::TYPE_VARCHAR:col["type"] = "VARCHAR"; break;
                    case DataType::TYPE_BOOLEAN:col["type"] = "BOOL"; break;
                    case DataType::TYPE_FLOAT:  col["type"] = "FLOAT"; break;
                    case DataType::TYPE_DOUBLE: col["type"] = "DOUBLE"; break;
                    case DataType::TYPE_TEXT:   col["type"] = "TEXT"; break;
                    case DataType::TYPE_DATETIME:col["type"] = "DATETIME"; break;
                    default: col["type"] = "VARCHAR"; break;
                }
                col["primaryKey"] = (f.isPrimaryKey != 0);
                col["notNull"] = ((f.constraints & 1u) != 0 || f.isPrimaryKey != 0);
                cols.push_back(col);
            }
            j["columns"] = cols;
        } else {
            j["ok"] = false;
            j["error"] = "Table not found";
        }
        res.set_content(j.dump(), "application/json");
    });

    // 7. 获取表的所有数据
    svr.Get(R"(/api/data/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string table = req.matches[1];
        json j = ExecSql("SELECT * FROM " + table + ";");
        res.set_content(j.dump(), "application/json");
    });

    // 8. 插入数据（带约束校验）
    svr.Post(R"(/api/data/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string table = req.matches[1];
        auto body = json::parse(req.body);
        auto row = body["row"];

        // 加载表结构做校验
        TableHeader header;
        std::vector<ColumnDef> fields;
        ErrorCode err = DictManager::loadTable(table, header, fields);

        if (err != ErrorCode::DB_OK || row.size() != fields.size()) {
            json j; j["ok"] = false;
            j["error"] = "Table not found or column count mismatch.";
            res.set_content(j.dump(), "application/json");
            return;
        }

        // NOT NULL 校验
        for (size_t i = 0; i < fields.size(); ++i) {
            bool isNotNull = ((fields[i].constraints & 1u) != 0 || fields[i].isPrimaryKey != 0);
            std::string val = row[i].is_string() ? row[i].get<std::string>() : row[i].dump();
            if (isNotNull && val.empty()) {
                json j; j["ok"] = false;
                j["error"] = "Column '" + std::string(fields[i].fieldName) + "' cannot be NULL.";
                res.set_content(j.dump(), "application/json");
                return;
            }
        }

        // 主键唯一性检查（扫描已有记录）
        int pkIdx = -1;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].isPrimaryKey != 0) { pkIdx = static_cast<int>(i); break; }
        }
        if (pkIdx >= 0 && header.recordCount > 0) {
            std::string pkVal = row[pkIdx].get<std::string>();
            int fd;
            if (FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + table + ".trd", "r", fd)) {
                uint32_t rpp = 4080 / header.recordSize; if (!rpp) rpp = 1;
                uint32_t totalPgs = (header.recordCount + rpp - 1) / rpp;
                bool dupFound = false;

                for (uint32_t pid = 0; pid < totalPgs && !dupFound; ++pid) {
                    void* pg = BufferPool::GetPage(fd, pid); if (!pg) continue;
                    uint32_t startR = pid * rpp;
                    uint32_t endR = std::min(startR + rpp, header.recordCount);
                    const auto& f = fields[pkIdx];

                    for (uint32_t ri = startR; ri < endR; ++ri) {
                        char* rp = static_cast<char*>(pg) + (ri % rpp) * header.recordSize;
                        std::string existingPk;
                        if (f.type == DataType::TYPE_INT) {
                            int32_t v; std::memcpy(&v, rp + f.offset, 4); existingPk = std::to_string(v);
                        } else if (f.type == DataType::TYPE_FLOAT) {
                            float v; std::memcpy(&v, rp + f.offset, 4);
                            std::ostringstream o; o << v; existingPk = o.str();
                        } else if (f.type == DataType::TYPE_DOUBLE) {
                            double v; std::memcpy(&v, rp + f.offset, 8);
                            std::ostringstream o; o << v; existingPk = o.str();
                        } else {
                            char b[512] = {};
                            std::strncpy(b, rp + f.offset, f.length > 511 ? 511 : f.length);
                            existingPk = b;
                        }
                        if (existingPk == pkVal) dupFound = true;
                    }
                    BufferPool::ReleasePage(fd, pid);
                }
                FileManager::CloseFile(fd);

                if (dupFound) {
                    json j; j["ok"] = false;
                    j["error"] = "Duplicate primary key '" + pkVal + "'.";
                    res.set_content(j.dump(), "application/json");
                    return;
                }
            }
        }

        // 通过 SQL 引擎执行插入（走 DMLExecutor，已含完整类型处理）
        std::string sql = "INSERT INTO " + table + " VALUES (";
        for (size_t i = 0; i < row.size(); ++i) {
            sql += "'" + row[i].get<std::string>() + "'";
            if (i < row.size() - 1) sql += ", ";
        }
        sql += ");";
        json j = ExecSql(sql);
        res.set_content(j.dump(), "application/json");
    });

    // 8.1 更新行
    svr.Put(R"(/api/data/(.*)/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string table = req.matches[1];
        int rowIndex = -1;
        try { rowIndex = std::stoi(req.matches[2].str()); } catch(...) {}

        auto body = json::parse(req.body);
        auto row = body["row"];

        json j;
        TableHeader header;
        std::vector<ColumnDef> fields;
        if (DictManager::loadTable(table, header, fields) != ErrorCode::DB_OK || rowIndex < 0 || rowIndex >= header.recordCount) {
            j["ok"] = false; j["error"] = "Invalid row index or table.";
            res.set_content(j.dump(), "application/json");
            return;
        }

        int fd;
        if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + table + ".trd", "rw", fd)) {
            j["ok"] = false; j["error"] = "Failed to open data file.";
            res.set_content(j.dump(), "application/json");
            return;
        }

        uint32_t recordsPerPage = 4080 / header.recordSize;
        if (recordsPerPage == 0) recordsPerPage = 1;
        
        uint32_t pid = rowIndex / recordsPerPage;
        uint32_t offset = (rowIndex % recordsPerPage) * header.recordSize;

        void* pageData = BufferPool::GetPage(fd, pid);
        if (pageData) {
            char* recordPtr = static_cast<char*>(pageData) + offset;
            for (size_t i = 0; i < fields.size() && i < row.size(); ++i) {
                std::string strVal = row[i].get<std::string>();
                switch (fields[i].type) {
                    case DataType::TYPE_INT: {
                        int32_t val = 0;
                        try { val = std::stoi(strVal); } catch(...) {}
                        std::memcpy(recordPtr + fields[i].offset, &val, 4);
                        break;
                    }
                    case DataType::TYPE_FLOAT: {
                        float val = 0.0f;
                        try { val = std::stof(strVal); } catch(...) {}
                        std::memcpy(recordPtr + fields[i].offset, &val, 4);
                        break;
                    }
                    case DataType::TYPE_DOUBLE: {
                        double val = 0.0;
                        try { val = std::stod(strVal); } catch(...) {}
                        std::memcpy(recordPtr + fields[i].offset, &val, 8);
                        break;
                    }
                    case DataType::TYPE_BOOLEAN: {
                        uint8_t bval = 0;
                        // 兼容 true/false/T/F/1/0/YES/NO
                        std::string upper = strVal;
                        for (auto& c : upper) c = static_cast<unsigned char>(std::toupper(c));
                        if (upper == "TRUE" || upper == "T" || upper == "1" || upper == "YES" || upper == "Y")
                            bval = 1;
                        *(recordPtr + fields[i].offset) = bval;
                        break;
                    }
                    default: { // VARCHAR, CHAR, TEXT, DATETIME
                        std::strncpy(recordPtr + fields[i].offset, strVal.c_str(), fields[i].length - 1);
                        recordPtr[fields[i].offset + fields[i].length - 1] = '\0';
                        break;
                    }
                }
            }
            BufferPool::MarkDirty(fd, pid);
            BufferPool::ReleasePage(fd, pid);
        }

        FileManager::CloseFile(fd);
        j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    });

    // 8.2 删除行
    svr.Delete(R"(/api/data/(.*)/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string table = req.matches[1];
        int rowIndex = -1;
        try { rowIndex = std::stoi(req.matches[2].str()); } catch(...) {}
        
        json j;
        TableHeader header;
        std::vector<ColumnDef> fields;
        if (DictManager::loadTable(table, header, fields) != ErrorCode::DB_OK || rowIndex < 0 || rowIndex >= header.recordCount) {
            j["ok"] = false; j["error"] = "Invalid row index or table.";
            res.set_content(j.dump(), "application/json");
            return;
        }

        int fd;
        if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + table + ".trd", "rw", fd)) {
            j["ok"] = false; j["error"] = "Failed to open data file.";
            res.set_content(j.dump(), "application/json");
            return;
        }

        uint32_t recordsPerPage = 4080 / header.recordSize;
        if (recordsPerPage == 0) recordsPerPage = 1;

        // 向前覆盖，相当于删除这一行
        for (uint32_t i = rowIndex; i < header.recordCount - 1; ++i) {
            uint32_t srcPid = (i + 1) / recordsPerPage;
            uint32_t srcOffset = ((i + 1) % recordsPerPage) * header.recordSize;
            uint32_t dstPid = i / recordsPerPage;
            uint32_t dstOffset = (i % recordsPerPage) * header.recordSize;

            void* srcPage = BufferPool::GetPage(fd, srcPid);
            char temp[4096];
            std::memcpy(temp, static_cast<char*>(srcPage) + srcOffset, header.recordSize);
            BufferPool::ReleasePage(fd, srcPid);

            void* dstPage = BufferPool::GetPage(fd, dstPid);
            std::memcpy(static_cast<char*>(dstPage) + dstOffset, temp, header.recordSize);
            BufferPool::MarkDirty(fd, dstPid);
            BufferPool::ReleasePage(fd, dstPid);
        }

        FileManager::CloseFile(fd);
        DictManager::updateRecordCount(table, header.recordCount - 1);

        j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    });

    // 9. 通用查询入口
    svr.Post("/api/query", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        std::string sql = body.value("sql", "");
        json j = ExecSql(sql);
        res.set_content(j.dump(), "application/json");
    });

    std::cout << "[RuankoDB HttpServer] Listening on http://0.0.0.0:" << port << "..." << std::endl;
    svr.listen("0.0.0.0", port);
}

} // namespace Ruanko
