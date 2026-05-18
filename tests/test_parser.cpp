#include <iostream>
#include <string>
#include "parser/sql_parser.h"
#include "parser/ast_nodes.h"

using namespace ruanko::parser;
using namespace std;

void test_parse(const string& sql) {
    cout << "=== TEST: " << sql << " ===" << endl;
    SqlParser p;
    auto ast = p.parse(sql);
    if (!ast) {
        cout << "FAIL" << endl << endl;
        return;
    }
    cout << "OK type=" << static_cast<int>(ast->type) << endl << endl;
}

int main() {
    test_parse("CREATE DATABASE test;");
    test_parse("USE test;");
    test_parse("CREATE TABLE t(id INT);");
    test_parse("INSERT INTO t VALUES(1);");
    test_parse("SELECT * FROM t;");
    test_parse("UPDATE t SET id=2;");
    test_parse("DELETE FROM t WHERE id=1;");
    test_parse("DROP TABLE t;");
    test_parse("DROP DATABASE test;");
    return 0;
}
