#pragma once
#include <string>
#include <memory>
#include "parser/ast_nodes.h"

class SqlParser {
public:
    // 解析完整 SQL 字符串并返回 AST 树
    static std::unique_ptr<ASTNode> Parse(const std::string& sql);

    // SQL 快速合法性验证
    static bool Validate(const std::string& sql);
};
