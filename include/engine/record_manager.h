// include/engine/record_manager.h
#pragma once
#include "parser/ast_nodes.h"
#include "engine/dml_executor.h"

// RecordManager 是 Parser 与 Executor 之间的总闸门
class RecordManager {
public:
    // 连接 Parser 解析语法树和底层具体执行器的交通枢纽
    static ExecuteResult Execute(const ASTNode* ast);
};
