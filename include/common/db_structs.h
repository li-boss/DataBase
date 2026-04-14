// include/common/db_structs.h
#pragma once

#include <cstdint>
#include <cstring>

#include "common/db_types.h"   // MAX_NAME_LEN, FieldType, ConstraintFlags

// ─── 核心结构体定义 ──────────────────────────────────────
enum class FieldType : uint32_t {
    TYPE_INT = 1,
    TYPE_CHAR = 2,
    TYPE_VARCHAR = 3,
    TYPE_DATETIME = 4,
    TYPE_BOOLEAN = 5
};

// 强制编译器使用4字节对齐
#pragma pack(push, 4)

// 系统数据库文件 ruanko.db 的描述记录
struct SysDBRecord {
    char dbName[MAX_NAME_LEN];
    uint32_t createTime;
    uint32_t tableCount;
};

// 表描述文件 .tb 的头部结构
struct TableHeader {
    char tableName[MAX_NAME_LEN];
    uint32_t recordCount; // 当前表中的数据行数
    uint32_t fieldCount;  // 列的数量
    uint32_t createTime;
    uint32_t modifyTime;
    uint32_t recordSize;  // 单行记录的总字节数（关键：用于定位 .trd 文件中的偏移量）
};

// 表定义文件 .tdf 的单个字段描述
struct FieldDefinition {
    char fieldName[MAX_NAME_LEN];
    FieldType type;
    uint32_t length;      // 类型长度（例如 CHAR(50) 则为 50，补齐后可能为 52）
    uint32_t offset;      // 该字段在单条记录(recordSize)中的起始字节偏移量
    uint32_t isPrimaryKey; 
    uint32_t constraints; // 位图掩码：如 1表示NOT NULL, 2表示UNIQUE等
};

// 恢复默认对齐
#pragma pack(pop)