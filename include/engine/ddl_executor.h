// include/engine/ddl_executor.h
#pragma once
#include <string>
#include <vector>
#include "../common/db_structs.h"

class DDLExecutor {
public:
    // 核心建表功能：接收表名和字段集合，生成 .tb, .tdf, .trd
    static bool createTable(const std::string& tableName, const std::vector<ColumnDef>& fields);

    // 预留扩展接口
    static bool dropTable(const std::string& tableName);
};
