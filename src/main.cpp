// src/main.cpp
#include <iostream>
#include <vector>
#include <cstring>
#include "../include/engine/ddl_executor.h"

// 辅助函数：快速构造字段
ColumnDef makeField(const char* name, DataType type, uint32_t length, uint32_t isPk) {
    ColumnDef fd;
    std::memset(&fd, 0, sizeof(ColumnDef));
    std::strncpy(fd.fieldName, name, MAX_NAME_LEN - 1);
    fd.type = type;
    fd.length = length;
    fd.isPrimaryKey = isPk;
    return fd;
}

int main() {
    std::cout << "=== RuankoDB Booting ===" << std::endl;

    // 模拟 SQL: CREATE TABLE Users (id INT PRIMARY KEY, name VARCHAR(32), age INT);
    std::vector<ColumnDef> userFields;
    // INT 类型固定占 4 字节
    userFields.push_back(makeField("id", DataType::TYPE_INT, 4, 1));
    // VARCHAR 设定最大 32 字节
    userFields.push_back(makeField("name", DataType::TYPE_VARCHAR, 32, 0));
    // INT 固定 4 字节
    userFields.push_back(makeField("age", DataType::TYPE_INT, 4, 0));

    std::cout << "Executing DDL: Creating table 'Users'..." << std::endl;
    
    // 先清理可能存在的旧文件
    DDLExecutor::dropTable("Users");
    
    // 执行建表
    DDLExecutor::createTable("Users", userFields);

    return 0;
}
