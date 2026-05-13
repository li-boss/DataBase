// include/common/db_structs.h
#pragma once

#include <cstdint>
#include <cstring>

#include "db_types.h"            // MAX_NAME_LEN（同目录）

// ─── 核心结构体定义 ──────────────────────────────────────
enum class DataType : uint32_t {
    TYPE_INT = 1,
    TYPE_CHAR = 2,
    TYPE_VARCHAR = 3,
    TYPE_DATETIME = 4,
    TYPE_BOOLEAN = 5,
    TYPE_FLOAT = 6,
    TYPE_DOUBLE = 7,
    TYPE_TEXT = 8
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

// 表定义文件 .tdf 的单个字段描述（方案命名：ColumnDef）
struct ColumnDef {
    char fieldName[MAX_NAME_LEN];
    DataType type;
    uint32_t length;      // 类型长度（例如 CHAR(50) 则为 50，补齐后可能为 52）
    uint32_t offset;      // 该字段在单条记录(recordSize)中的起始字节偏移量
    uint32_t isPrimaryKey;
    uint32_t constraints; // 位图掩码：如 1表示NOT NULL, 2表示UNIQUE等
};

// 单条记录数据（方案要求）
struct RecordData {
    void* rawData;        // 指向序列化后的字节缓冲区
    uint32_t size;        // 记录字节长度（= recordSize）
};

// ─── 索引相关结构体 ─────────────────────────────────────

// 索引头部（.idx 文件头，紧跟在 IndexHeader 后面）
// 注意：.idx 文件布局 = IndexHeader + IndexEntry数组
// IndexEntry 为变长结构，不在此定义为固定结构体
struct IndexHeader {
    char      indexName[MAX_NAME_LEN];  // 索引名，如 idx_students_age
    char      tableName[MAX_NAME_LEN];  // 所属表名
    char      columnName[MAX_NAME_LEN];  // 索引字段名
    uint32_t  columnIndex;              // 字段在表中的序号
    uint32_t  keyType;                 // 索引键类型（复用 DataType）
    uint32_t  entryCount;              // 当前索引条目数
    uint32_t  createTime;              // 创建时间（Unix 时间戳）
    uint32_t  keySize;                 // 键的字节数（由类型决定，加速读取）
    uint32_t  reserved[7];            // 保留字段，对齐用（凑足 8 个 reserved）
};
// sizeof(IndexHeader) 应为：128*3 + 4*5 + 4*7 = 384 + 20 + 28 = 432 字节

// IndexEntry 磁盘布局（变长，不定义为 struct）：
//   [ keyData: keySize 字节 ][ recordOffset: 4 字节 ]
// 每个条目总长度 = keySize + 4 字节

// 恢复默认对齐
#pragma pack(pop)
