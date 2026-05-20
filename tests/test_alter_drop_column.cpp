#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "engine/record_manager.h"
#include "parser/sql_parser.h"
#include "storage/buffer_pool.h"

static ExecuteResult execSql(const std::string& sql) {
    std::unique_ptr<ASTNode> ast = SqlParser::Parse(sql);
    return RecordManager::Execute(ast.get());
}

int main() {
    BufferPool::init(64);

    const std::string dbName = "__alter_drop_column_db__";
    std::filesystem::remove_all(dbName);

    auto res = execSql("CREATE DATABASE " + dbName + ";");
    assert(res.error == 0);
    res = execSql("USE " + dbName + ";");
    assert(res.error == 0);
    res = execSql("CREATE TABLE employees (ID INT, NAME VARCHAR, SALARY INT);");
    assert(res.error == 0);
    res = execSql("INSERT INTO employees VALUES (1, 'alice', 9000);");
    assert(res.error == 0);
    res = execSql("INSERT INTO employees VALUES (2, 'bob', 8000);");
    assert(res.error == 0);

    res = execSql("ALTER TABLE employees DROP COLUMN SALARY;");
    assert(res.error == 0);

    res = execSql("SELECT * FROM employees;");
    assert(res.error == 0);
    assert((res.headers == std::vector<std::string>{"ID", "NAME"}));
    assert(res.rows.size() == 2);
    assert((res.rows[0] == std::vector<std::string>{"1", "alice"}));
    assert((res.rows[1] == std::vector<std::string>{"2", "bob"}));

    BufferPool::shutdown();
    std::filesystem::remove_all(dbName);

    std::cout << "ALTER DROP COLUMN preserves remaining row data: PASS\n";
    return 0;
}
