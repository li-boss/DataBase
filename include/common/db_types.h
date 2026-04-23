// include/common/db_types.h
#pragma once

#include <cstdint>

/**
 * @file db_types.h
 * @brief RuankoDB 全局类型常量与页面配置
 *
 * 职责划分：
 *   - 本文件：数值常量、页面参数、类型大小映射（纯数据）
 *   - db_structs.h：结构体定义（TableHeader / ColumnDef 等）
 *   - db_errors.h ：错误码枚举与消息
 *
 * 设计原则：所有涉及存储布局的"魔法数字"集中在此处，
 *           修改一处即可全局生效。
 */

// ─── 系统命名限制 ────────────────────────────────────────
constexpr int   MAX_NAME_LEN = 128;       ///< 表名/字段名/数据库名最大长度（字节）

// ─── 页面（Page）参数 ───────────────────────────────────
constexpr size_t PAGE_SIZE      = 4096;   ///< 单个数据页大小（4 KB，标准磁盘块对齐）
constexpr int    MAX_PAGES      = 64;     ///< 缓冲池最大缓存页数（可按需调整）
constexpr size_t PAGE_HEADER_SIZE = 8;    ///< 页头预留空间（page_id:4 + dirty_flag:4）

// ─── 字段类型相关 ────────────────────────────────────────
// （DataType 枚举定义在 db_structs.h 中，此处仅提供类型→字节数映射）

/**
 * @brief 根据 DataType 返回该类型的固定存储宽度（字节）
 * @param type 字段类型枚举
 * @param userDeclaredLength 用户声明的长度（仅 CHAR/VARCHAR 需要传入，其余传 0）
 * @return 该字段占用的字节数；若类型未知返回 0
 */
inline constexpr uint32_t getTypeStorageSize(uint32_t fieldTypeRaw, uint32_t userDeclaredLength = 0) {
    switch (fieldTypeRaw) {
        case 1: return 4;   // TYPE_INT
        case 2: return (userDeclaredLength > 0) ? userDeclaredLength : 1; // TYPE_CHAR
        case 3: return (userDeclaredLength > 0) ? userDeclaredLength : 32; // TYPE_VARCHAR
        case 4: return 8;   // TYPE_DATETIME
        case 5: return 1;   // TYPE_BOOLEAN
        default: return 0;
    }
}

// ─── 约束位图掩码 ───────────────────────────────────────
namespace ConstraintFlags {
    constexpr uint32_t NOT_NULL = 0x01;  ///< 非 NULL 约束
    constexpr uint32_t UNIQUE   = 0x02;  ///< 唯一约束
    constexpr uint32_t PK       = 0x04;  ///< 主键标记（隐含 NOT NULL + UNIQUE）
}
