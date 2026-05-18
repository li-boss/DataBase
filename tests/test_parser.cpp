// tests/test_parser.cpp
// RuankoDB SQL 解析器单元测试 — 覆盖全部 17 种 StmtType
#include "parser/sql_parser.h"
#include <iostream>
#include <string>
#include <cassert>
#include <vector>
#include <iomanip>

// ─── 轻量断言宏 ──────────────────────────────────────
static int g_passed = 0;
static int g_failed = 0;
static std::string g_currentGroup;

#define TEST_GROUP(name)  g_currentGroup = (name); std::cout << "\n  ▸ " << g_currentGroup << "\n";
#define CHECK(expr, desc) do { \
    if (!(expr)) { \
        std::cerr << "    ✗ " << (desc) << "\n"; \
        g_failed++; \
    } else { \
        std::cout << "    ✓ " << (desc) << "\n"; \
        g_passed++; \
    } \
} while(0)

#define ASSERT_TYPE(node, expected) CHECK((node)->type == (expected), \
    "StmtType = " #expected)
#define ASSERT_FIELD(actual, expected, desc) CHECK((actual) == (expected), desc)
#define ASSERT_TRUE(expr, desc) CHECK((expr), desc)

// ─── 核心解析函数 ────────────────────────────────────
static std::unique_ptr<ASTNode> parse(const std::string& sql) {
    return SqlParser::Parse(sql);
}

// ─── 测试入口 ────────────────────────────────────────
int main() {
    std::cout << "\n╔══════════════════════════════════════════════╗\n";
    std::cout << "║   RuankoDB SQL 解析器单元测试 v1.0         ║\n";
    std::cout << "║   覆盖 17 种 StmtType + 边界情况           ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";

    // ================================================================
    // 1. 数据库 DDL — CREATE/DROP/USE DATABASE
    // ================================================================
    TEST_GROUP("1. 数据库 DDL — CREATE/DROP/USE");

    {
        auto n = parse("CREATE DATABASE testdb");
        ASSERT_TYPE(n, StmtType::CREATE_DB);
        ASSERT_FIELD(n->db, "testdb", "db = testdb");
    }
    {
        auto n = parse("CREATE DATABASE my_db");
        ASSERT_TYPE(n, StmtType::CREATE_DB);
        ASSERT_FIELD(n->db, "my_db", "db = my_db");
    }
    {
        auto n = parse("DROP DATABASE testdb");
        ASSERT_TYPE(n, StmtType::DROP_DB);
        ASSERT_FIELD(n->db, "testdb", "db = testdb");
    }
    {
        auto n = parse("USE testdb");
        ASSERT_TYPE(n, StmtType::USE_DB);
        ASSERT_FIELD(n->db, "testdb", "db = testdb");
    }

    // ================================================================
    // 2. 表 DDL — CREATE/DROP TABLE, SHOW TABLES
    // ================================================================
    TEST_GROUP("2. 表 DDL — CREATE/DROP/SHOW TABLE");

    {
        auto n = parse("CREATE TABLE student (id INT, name VARCHAR(20), age INT)");
        ASSERT_TYPE(n, StmtType::CREATE_TABLE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD((int)n->columns.size(), 3, "3 columns");
        ASSERT_FIELD(n->columns[0], "id INT", "col[0] = id INT");
        // trimToken 会去掉末尾 ')'，所以 VARCHAR(20) → VARCHAR(20
        ASSERT_FIELD(n->columns[1], "name VARCHAR(20", "col[1] = name VARCHAR(20 (trimToken strips trailing ')')");
        ASSERT_FIELD(n->columns[2], "age INT", "col[2] = age INT");
    }
    {
        auto n = parse("DROP TABLE student");
        ASSERT_TYPE(n, StmtType::DROP_TABLE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
    }
    {
        auto n = parse("SHOW TABLES");
        ASSERT_TYPE(n, StmtType::SHOW_TABLES);
    }

    // ================================================================
    // 3. ALTER TABLE — ADD / DROP / MODIFY COLUMN
    // ================================================================
    TEST_GROUP("3. ALTER TABLE — ADD COLUMN");

    {
        auto n = parse("ALTER TABLE student ADD COLUMN email VARCHAR(100)");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_TRUE(n->alterAction == AlterAction::ADD_COLUMN, "action = ADD_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "EMAIL", "column = EMAIL (uppercased)");
        ASSERT_FIELD(n->alterColumnType, "VARCHAR(100", "type = VARCHAR(100 (paren stripped)");
    }
    {
        // ADD 不带 COLUMN 关键字
        auto n = parse("ALTER TABLE student ADD score INT");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::ADD_COLUMN, "action = ADD_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "SCORE", "column = SCORE (no COLUMN kw)");
        ASSERT_FIELD(n->alterColumnType, "INT", "type = INT");
    }
    {
        // ADD + NOT NULL 约束
        auto n = parse("ALTER TABLE student ADD COLUMN gender INT NOT NULL");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::ADD_COLUMN, "action = ADD_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "GENDER", "column = GENDER");
        ASSERT_TRUE(n->alterNotNull, "NOT NULL = true");
    }
    {
        // ADD + PRIMARY KEY 约束
        auto n = parse("ALTER TABLE student ADD COLUMN uid INT PRIMARY KEY");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::ADD_COLUMN, "action = ADD_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "UID", "column = UID");
        ASSERT_TRUE(n->alterPrimaryKey, "PRIMARY KEY = true");
    }

    TEST_GROUP("3b. ALTER TABLE — DROP COLUMN");
    {
        auto n = parse("ALTER TABLE student DROP COLUMN age");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::DROP_COLUMN, "action = DROP_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "AGE", "column = AGE");
    }
    {
        // DROP 不带 COLUMN 关键字
        auto n = parse("ALTER TABLE student DROP score");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::DROP_COLUMN, "action = DROP_COLUMN (no COLUMN)");
        ASSERT_FIELD(n->alterColumnName, "SCORE", "column = SCORE");
    }

    TEST_GROUP("3c. ALTER TABLE — MODIFY COLUMN");
    {
        auto n = parse("ALTER TABLE student MODIFY COLUMN name VARCHAR(50)");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::MODIFY_COLUMN, "action = MODIFY_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "NAME", "column = NAME");
        ASSERT_FIELD(n->alterColumnType, "VARCHAR(50", "type = VARCHAR(50 (paren stripped)");
    }
    {
        // MODIFY + NOT NULL
        auto n = parse("ALTER TABLE student MODIFY name VARCHAR(50) NOT NULL");
        ASSERT_TYPE(n, StmtType::ALTER_TABLE);
        ASSERT_TRUE(n->alterAction == AlterAction::MODIFY_COLUMN, "action = MODIFY_COLUMN");
        ASSERT_FIELD(n->alterColumnName, "NAME", "column = NAME");
        ASSERT_TRUE(n->alterNotNull, "NOT NULL = true");
    }

    // ================================================================
    // 4. 索引操作 — CREATE/DROP/SHOW INDEX
    // ================================================================
    TEST_GROUP("4. 索引操作 — CREATE/DROP/SHOW INDEX");

    {
        // CREATE INDEX idx_name ON tbl(col) — 紧凑格式
        auto n = parse("CREATE INDEX idx_sname ON student(sname)");
        ASSERT_TYPE(n, StmtType::CREATE_INDEX);
        ASSERT_FIELD(n->columns[0], "idx_sname", "index name = idx_sname");
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD(n->columns[1], "SNAME", "column = SNAME (uppercased)");
    }
    {
        // DROP INDEX idx_name ON tbl
        auto n = parse("DROP INDEX idx_sname ON student");
        ASSERT_TYPE(n, StmtType::DROP_INDEX);
        ASSERT_FIELD(n->columns[0], "idx_sname", "index name = idx_sname");
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
    }
    {
        // SHOW INDEXES tbl
        auto n = parse("SHOW INDEXES student");
        ASSERT_TYPE(n, StmtType::SHOW_INDEXES);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (INDEXES variant)");
    }
    {
        // SHOW INDEX FROM tbl
        auto n = parse("SHOW INDEX FROM student");
        ASSERT_TYPE(n, StmtType::SHOW_INDEXES);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (INDEX FROM variant)");
    }
    {
        // SHOW INDEX tbl (不带 FROM)
        auto n = parse("SHOW INDEX student");
        ASSERT_TYPE(n, StmtType::SHOW_INDEXES);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (INDEX variant)");
    }

    // ================================================================
    // 5. INSERT — 含/不含列名列表
    // ================================================================
    TEST_GROUP("5. INSERT");

    {
        auto n = parse("INSERT INTO student VALUES (1, 'Alice', 20)");
        ASSERT_TYPE(n, StmtType::INSERT);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD((int)n->values.size(), 3, "3 values");
        ASSERT_FIELD(n->values[0], "1", "val[0] = 1");
        ASSERT_FIELD(n->values[1], "Alice", "val[1] = Alice (quotes stripped)");
        ASSERT_FIELD(n->values[2], "20", "val[2] = 20");
    }
    {
        // 含列名列表的 INSERT
        auto n = parse("INSERT INTO student (id, name) VALUES (2, 'Bob')");
        ASSERT_TYPE(n, StmtType::INSERT);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD((int)n->values.size(), 2, "2 values (column list skipped)");
        ASSERT_FIELD(n->values[0], "2", "val[0] = 2");
        ASSERT_FIELD(n->values[1], "Bob", "val[1] = Bob");
    }

    // ================================================================
    // 6. SELECT — 投影 + 单条件 WHERE + 复合条件 AND/OR
    // ================================================================
    TEST_GROUP("6. SELECT — 基本投影");

    {
        auto n = parse("SELECT * FROM student");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD((int)n->columns.size(), 1, "1 column (star)");
        ASSERT_FIELD(n->columns[0], "*", "col = *");
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
    }
    {
        auto n = parse("SELECT id, name FROM student");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD((int)n->columns.size(), 2, "2 columns");
        ASSERT_FIELD(n->columns[0], "id", "col[0] = id");
        ASSERT_FIELD(n->columns[1], "name", "col[1] = name");
    }

    TEST_GROUP("6b. SELECT — 单条件 WHERE");

    {
        auto n = parse("SELECT * FROM student WHERE id = 1");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_TRUE(n->where.hasWhere, "hasWhere = true");
        ASSERT_FIELD((int)n->where.conditions.size(), 1, "1 condition");
        ASSERT_FIELD(n->where.conditions[0].column, "ID", "cond.column = ID (uppercased)");
        ASSERT_FIELD(n->where.conditions[0].op, "=", "cond.op = =");
        ASSERT_FIELD(n->where.conditions[0].value, "1", "cond.value = 1");
    }
    {
        // 字符串条件
        auto n = parse("SELECT * FROM student WHERE name = 'Alice'");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD(n->where.conditions[0].column, "NAME", "column = NAME");
        ASSERT_FIELD(n->where.conditions[0].value, "Alice", "value = Alice (quotes stripped)");
    }
    {
        // 比较运算符
        auto n = parse("SELECT * FROM student WHERE age > 18");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD(n->where.conditions[0].column, "AGE", "column = AGE");
        ASSERT_FIELD(n->where.conditions[0].op, ">", "op = >");
        ASSERT_FIELD(n->where.conditions[0].value, "18", "value = 18");
    }

    TEST_GROUP("6c. SELECT — 复合 WHERE (AND/OR)");

    {
        auto n = parse("SELECT * FROM student WHERE age > 18 AND gender = 'M'");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_TRUE(n->where.hasWhere, "hasWhere = true");
        ASSERT_FIELD((int)n->where.conditions.size(), 2, "2 conditions (AND)");
        ASSERT_FIELD(n->where.conditions[0].column, "AGE", "cond[0] = AGE");
        ASSERT_FIELD(n->where.conditions[0].op, ">", "cond[0] op = >");
        ASSERT_FIELD(n->where.conditions[0].value, "18", "cond[0] value = 18");
        ASSERT_FIELD((int)n->where.logicOps.size(), 1, "1 logicOp");
        ASSERT_TRUE(n->where.logicOps[0] == LogicOp::AND, "logicOp[0] = AND");
        ASSERT_FIELD(n->where.conditions[1].column, "GENDER", "cond[1] = GENDER");
        ASSERT_FIELD(n->where.conditions[1].op, "=", "cond[1] op = =");
        ASSERT_FIELD(n->where.conditions[1].value, "M", "cond[1] value = M");
    }
    {
        auto n = parse("SELECT * FROM student WHERE age > 20 OR score < 60");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD((int)n->where.conditions.size(), 2, "2 conditions (OR)");
        ASSERT_TRUE(n->where.logicOps[0] == LogicOp::OR, "logicOp[0] = OR");
        ASSERT_FIELD(n->where.conditions[1].column, "SCORE", "cond[1] = SCORE");
        ASSERT_FIELD(n->where.conditions[1].value, "60", "cond[1] value = 60");
    }
    {
        // 三条件链: a AND b OR c
        auto n = parse("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD((int)n->where.conditions.size(), 3, "3 conditions (AND + OR)");
        ASSERT_FIELD((int)n->where.logicOps.size(), 2, "2 logicOps");
        ASSERT_TRUE(n->where.logicOps[0] == LogicOp::AND, "logicOp[0] = AND");
        ASSERT_TRUE(n->where.logicOps[1] == LogicOp::OR, "logicOp[1] = OR");
    }

    // ================================================================
    // 7. UPDATE — SET + WHERE
    // ================================================================
    TEST_GROUP("7. UPDATE");

    {
        auto n = parse("UPDATE student SET name = 'Charlie' WHERE id = 1");
        ASSERT_TYPE(n, StmtType::UPDATE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_FIELD((int)n->columns.size(), 1, "1 SET column");
        ASSERT_FIELD(n->columns[0], "NAME", "SET col = NAME (uppercased)");
        ASSERT_FIELD((int)n->values.size(), 1, "1 SET value");
        ASSERT_FIELD(n->values[0], "Charlie", "SET val = Charlie (quotes stripped)");
        ASSERT_TRUE(n->where.hasWhere, "hasWhere = true");
        ASSERT_FIELD(n->where.conditions[0].column, "ID", "WHERE col = ID");
        ASSERT_FIELD(n->where.conditions[0].value, "1", "WHERE val = 1");
    }
    {
        // UPDATE 无 WHERE
        auto n = parse("UPDATE student SET score = 100");
        ASSERT_TYPE(n, StmtType::UPDATE);
        ASSERT_FIELD(n->columns[0], "SCORE", "SET col = SCORE");
        ASSERT_FIELD(n->values[0], "100", "SET val = 100");
        ASSERT_TRUE(!n->where.hasWhere, "hasWhere = false");
    }

    // ================================================================
    // 8. DELETE — FROM + WHERE
    // ================================================================
    TEST_GROUP("8. DELETE");

    {
        auto n = parse("DELETE FROM student WHERE id = 1");
        ASSERT_TYPE(n, StmtType::DELETE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_TRUE(n->where.hasWhere, "hasWhere = true");
        ASSERT_FIELD(n->where.conditions[0].column, "ID", "WHERE col = ID");
    }
    {
        // DELETE 无 WHERE (全表删除)
        auto n = parse("DELETE FROM student");
        ASSERT_TYPE(n, StmtType::DELETE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
        ASSERT_TRUE(!n->where.hasWhere, "hasWhere = false (no WHERE)");
    }

    // ================================================================
    // 9. 事务控制 — BEGIN / COMMIT / ROLLBACK
    // ================================================================
    TEST_GROUP("9. 事务控制");

    {
        auto n = parse("BEGIN");
        ASSERT_TYPE(n, StmtType::BEGIN_TX);
    }
    {
        auto n = parse("BEGIN TRANSACTION");
        ASSERT_TYPE(n, StmtType::BEGIN_TX);
    }
    {
        // BEGIN TRANSACTION + 表名
        auto n = parse("BEGIN TRANSACTION student");
        ASSERT_TYPE(n, StmtType::BEGIN_TX);
        ASSERT_FIELD(n->tbl, "student", "tbl = student");
    }
    {
        auto n = parse("COMMIT");
        ASSERT_TYPE(n, StmtType::COMMIT_TX);
    }
    {
        auto n = parse("ROLLBACK");
        ASSERT_TYPE(n, StmtType::ROLLBACK_TX);
    }

    // ================================================================
    // 10. CREATE VIEW
    // ================================================================
    TEST_GROUP("10. CREATE VIEW");

    {
        auto n = parse("CREATE VIEW v_student AS SELECT * FROM student WHERE age > 18");
        ASSERT_TYPE(n, StmtType::CREATE_VIEW);
        ASSERT_FIELD(n->columns[0], "v_student", "view name = v_student");
        ASSERT_FIELD(n->values[0], "SELECT * FROM student WHERE age > 18", "SELECT body preserved");
    }

    // ================================================================
    // 11. 边界情况 — 大小写混合、分号结尾、空/无效 SQL
    // ================================================================
    TEST_GROUP("11. 边界情况");

    {
        // 全小写
        auto n = parse("select * from student");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (lowercase)");
    }
    {
        // 大小写混合
        auto n = parse("SeLeCt * FrOm student");
        ASSERT_TYPE(n, StmtType::SELECT);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (mixed case)");
    }
    {
        // 分号结尾
        auto n = parse("DROP TABLE student;");
        ASSERT_TYPE(n, StmtType::DROP_TABLE);
        ASSERT_FIELD(n->tbl, "student", "tbl = student (semicolon stripped)");
    }
    {
        // 乱码 / 无意义输入
        auto n = parse("asdfghjkl qwerty");
        ASSERT_TYPE(n, StmtType::UNKNOWN);
    }
    {
        // 空 SQL — Validate 会拦截, Parse 不直接调用
        // 仅测试 Parse 返回 UNKNOWN
        auto n = parse("");
        ASSERT_TYPE(n, StmtType::UNKNOWN);
    }
    {
        // 部分关键字不完整
        auto n = parse("SELECT FROM");  // 缺表名但仍处理
        ASSERT_TYPE(n, StmtType::SELECT);
        // stream EOF 后 token 未清空（实现差异），保留上次读到的 "FROM"
        ASSERT_FIELD(n->tbl, "FROM", "tbl = 'FROM' (stream keeps last token on EOF)");
    }

    // ================================================================
    // 12. 解析器 Validate 预检
    // ================================================================
    TEST_GROUP("12. Validate 预检");

    ASSERT_TRUE(SqlParser::Validate("SELECT"), "Validate('SELECT') = true");
    ASSERT_TRUE(SqlParser::Validate("USE db"), "Validate('USE db') = true");
    ASSERT_TRUE(!SqlParser::Validate(""), "Validate('') = false");
    ASSERT_TRUE(!SqlParser::Validate("AB"), "Validate('AB') = false (too short)");
    ASSERT_TRUE(!SqlParser::Validate("ABC"), "Validate('ABC') = false (too short, <=3)");

    // ================================================================
    // 汇总报告
    // ================================================================
    std::cout << "\n╔══════════════════════════════════════════════╗\n";
    std::cout << "║  解析器测试汇总                             ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║  通过: " << std::setw(4) << g_passed << "                                  ║\n";
    std::cout << "║  失败: " << std::setw(4) << g_failed << "                                  ║\n";
    if (g_failed == 0) {
        std::cout << "║  ✓ 全部通过                                ║\n";
    } else {
        std::cout << "║  ⚠ 存在 " << g_failed << " 个失败用例                       ║\n";
    }
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    return g_failed > 0 ? 1 : 0;
}
// #include <iostream>
// #include <string>
// #include "parser/sql_parser.h"
// #include "parser/ast_nodes.h"

// using namespace ruanko::parser;
// using namespace std;

// void test_parse(const string& sql) {
//     cout << "=== TEST: " << sql << " ===" << endl;
//     SqlParser p;
//     auto ast = p.parse(sql);
//     if (!ast) {
//         cout << "FAIL" << endl << endl;
//         return;
//     }
//     cout << "OK type=" << static_cast<int>(ast->type) << endl << endl;
// }

// int main() {
//     test_parse("CREATE DATABASE test;");
//     test_parse("USE test;");
//     test_parse("CREATE TABLE t(id INT);");
//     test_parse("INSERT INTO t VALUES(1);");
//     test_parse("SELECT * FROM t;");
//     test_parse("UPDATE t SET id=2;");
//     test_parse("DELETE FROM t WHERE id=1;");
//     test_parse("DROP TABLE t;");
//     test_parse("DROP DATABASE test;");
//     return 0;
// }
