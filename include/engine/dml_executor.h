// include/engine/dml_executor.h
#pragma once
#include "parser/ast_nodes.h"
#include <vector>
#include <string>

// 统一的执行返回结果结构，无论是 DDL 还是 DML，都会向顶层返回这个结果对象
struct ExecuteResult {
    int error = 0;    // 错误码，0 代表成功
    std::string msg;  // 执行的字符串信息回显，类似于 "Query OK, 1 row affected"
    std::vector<std::vector<std::string>> rows; // 查询出的数据行组合，仅 SELECT 时有数据
    std::vector<std::string> headers;           // 表的列名字段集合，用于打印表头
};

class DMLExecutor {
public:
    // 数据操纵语言 (DML) 核心接口，全部接收 AST 树进行逻辑处理
    static ExecuteResult insertRecord(const ASTNode* ast);
    static ExecuteResult selectRecord(const ASTNode* ast);
    static ExecuteResult updateRecord(const ASTNode* ast);
    static ExecuteResult deleteRecord(const ASTNode* ast);
    
    // 新增：事务控制接口
    static ExecuteResult executeBegin(const ASTNode* ast);
    static ExecuteResult executeCommit(const ASTNode* ast);
    static ExecuteResult executeRollback(const ASTNode* ast);
};
