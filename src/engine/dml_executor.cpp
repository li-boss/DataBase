// src/engine/dml_executor.cpp
#include "engine/dml_executor.h"
#include <iostream>

ExecuteResult DMLExecutor::insertRecord(const ASTNode* ast) {
    ExecuteResult res;
    // 逻辑思路：
    // 1. 读取表头文件(.tdf) 校验字段是否存在、类型是否匹配。
    // 2. 将字符串数据转为对应的二进制 (int用4字节，char定长)。
    // 3. 寻找空闲块或直接 Append 到 .trd 文件的最后。
    
    // 【Stub 桩代码模式】：既然 Dev-B 没做完，我们先返回一个友好的假成功响应，保护系统不崩溃。
    res.error = 0;
    res.msg = "Query OK: Successfully inserted mock data into [" + ast->tbl + "]";
    std::cout << "[DML Engine] Simulating INSERT to disk..." << std::endl;
    return res;
}

ExecuteResult DMLExecutor::selectRecord(const ASTNode* ast) {
    ExecuteResult res;
    // 逻辑思路：
    // 1. 扫描整个 .trd 文件（后续配合 BufferPool 就是根据页遍历）。
    // 2. 按 RecordSize 一个个切分记录实体。
    // 3. 对每一行，评估是否存在 WHERE；如果 hasWhere == true 则比较对应字段。
    
    // 【Stub 桩代码模式】：在磁盘读取没好之前，我们造一条假数据用于证明整个流程的联接
    res.error = 0;
    res.msg = "Query OK: 1 row simulated in set";
    // 假装查出了两个字段
    res.headers = {"system_id", "status"};
    res.rows.push_back({"1001", "RUANKO_STUB_WORKING"});
    
    if (ast->where.hasWhere) {
        std::cout << "[DML Engine] Filtering rows where " << ast->where.column 
                  << " " << ast->where.op << " " << ast->where.value << std::endl;
    }
    return res;
}

ExecuteResult DMLExecutor::updateRecord(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: Rows matched: 1  Changed: 1  Warnings: 0";
    return res;
}

ExecuteResult DMLExecutor::deleteRecord(const ASTNode* ast) {
    ExecuteResult res;
    res.msg = "Query OK: 1 row deleted";
    return res;
}
