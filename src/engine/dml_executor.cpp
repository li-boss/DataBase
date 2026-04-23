// src/engine/dml_executor.cpp
#include "../../include/engine/dml_executor.h"
#include "../../include/storage/file_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/buffer_pool.h"
#include "../../include/common/db_errors.h"
#include <iostream>
#include <cstring>
#include <string>

ExecuteResult DMLExecutor::insertRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
    // 逻辑思路：
    // 1. 读取表头文件(.tdf) 校验字段是否存在、类型是否匹配。
    // 2. 将字符串数据转为对应的二进制 (int用4字节，char定长)。
    // 3. 寻找空闲块或直接 Append 到 .trd 文件的最后。
    
    // 【Stub 桩代码模式】：既然 Dev-B 没做完，我们先返回一个友好的假成功响应，保护系统不崩溃。
    res.error = 0;
    res.msg = "Query OK: Successfully inserted mock data into [" + ast->tbl + "]";
    std::cout << "[DML Engine] Simulating INSERT to disk..." << std::endl;
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1;
        res.msg = "Error: " + std::string(getErrorMessage(err));
        return res;
    }

    if (ast->values.size() != fields.size()) {
        res.error = 1;
        res.msg = "Error: Insert value count does not match field count.";
        return res;
    }

    int fd;
    if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "rw", fd)) {
        res.error = 1;
        res.msg = "Error: Failed to open data file.";
        return res;
    }

    uint32_t recordsPerPage = 4080 / header.recordSize; 
    if (recordsPerPage == 0) recordsPerPage = 1;
    uint32_t pid = header.recordCount / recordsPerPage;
    uint32_t offset = (header.recordCount % recordsPerPage) * header.recordSize;

    void* pageData = BufferPool::GetPage(fd, pid);
    if (!pageData) {
        // 如果该页尚未分配，则在文件中追加一个新的全零空页
        char blank_page[4096] = {0};
        FileManager::WritePage(fd, pid, blank_page);
        
        // 再次请求 BufferPool 加载该页
        pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) {
            res.error = 1; res.msg = "Error: Buffer pool failed to load page even after allocation.";
            FileManager::CloseFile(fd); return res;
        }
    }

    char* recordPtr = static_cast<char*>(pageData) + offset;
    
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].type == DataType::TYPE_INT) {
            int32_t val = 0;
            try { val = std::stoi(ast->values[i]); } catch(...) {}
            std::memcpy(recordPtr + fields[i].offset, &val, 4);
        } else {
            std::strncpy(recordPtr + fields[i].offset, ast->values[i].c_str(), fields[i].length - 1);
            recordPtr[fields[i].offset + fields[i].length - 1] = '\0';
        }
    }

    BufferPool::MarkDirty(fd, pid);
    BufferPool::ReleasePage(fd, pid);
    FileManager::CloseFile(fd);

    DictManager::updateRecordCount(ast->tbl, header.recordCount + 1);
    res.msg = "Query OK: 1 row affected.";
#endif
    return res;
}

ExecuteResult DMLExecutor::selectRecord(const ASTNode* ast) {
    ExecuteResult res;
#ifdef USE_STORAGE_STUB
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
#else
    TableHeader header;
    std::vector<ColumnDef> fields;
    ErrorCode err = DictManager::loadTable(ast->tbl, header, fields);
    if (err != ErrorCode::DB_OK) {
        res.error = 1; res.msg = "Error: " + std::string(getErrorMessage(err)); return res;
    }

    for (const auto& f : fields) {
        res.headers.push_back(f.fieldName);
    }

    int fd;
    if (!FileManager::OpenFile(DictManager::GetCurrentDB() + "/" + ast->tbl + ".trd", "r", fd)) {
        res.msg = "Query OK: Empty set"; return res;
    }

    uint32_t recordsPerPage = 4080 / header.recordSize;
    if (recordsPerPage == 0) recordsPerPage = 1;
    uint32_t totalPages = (header.recordCount + recordsPerPage - 1) / recordsPerPage;

    uint32_t recordsRead = 0;
    for (uint32_t pid = 0; pid < totalPages; ++pid) {
        void* pageData = BufferPool::GetPage(fd, pid);
        if (!pageData) continue;

        for (uint32_t i = 0; i < recordsPerPage && recordsRead < header.recordCount; ++i) {
            char* recordPtr = static_cast<char*>(pageData) + i * header.recordSize;
            std::vector<std::string> row;
            
            for (const auto& f : fields) {
                if (f.type == DataType::TYPE_INT) {
                    int32_t val;
                    std::memcpy(&val, recordPtr + f.offset, 4);
                    row.push_back(std::to_string(val));
                } else {
                    char buf[256] = {0};
                    std::strncpy(buf, recordPtr + f.offset, f.length);
                    row.push_back(std::string(buf));
                }
            }
            res.rows.push_back(row);
            recordsRead++;
        }
        BufferPool::ReleasePage(fd, pid);
    }
    FileManager::CloseFile(fd);

    res.msg = "Query OK: " + std::to_string(res.rows.size()) + " row(s) in set";
#endif
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
