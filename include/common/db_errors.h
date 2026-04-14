// include/common/db_errors.h
#pragma once

#include <string>

/**
 * @file db_errors.h
 * @brief RuankoDB 统一错误码定义
 *
 * 设计原则:
 * - 枚举值从 0 开始递增，0 始终表示成功
 * - 按模块分组，每组预留 20 个编号空间
 * - 提供 getErrorMessage() 将错误码转为可读字符串
 */

// ─── 错误码分组区间 ──────────────────────────────────────
//  0x0000 ~ 0x000F   通用 / 系统
//  0x0010 ~ 0x001F   文件 I/O
//  0x0020 ~ 0x002F   存储层 (BufferPool / DictMgr)
//  0x0030 ~ 0x003F   DDL (建表 / 删表)
//  0x0040 ~ 0x004F   DML (增删改查)
//  0x0050 ~ 0x005F   SQL 解析器
//  0x0060 ~ 0x006F   约束检查 (PK / NOT NULL 等)

enum class ErrorCode : uint16_t {
    // ─── 通用 (0x00) ─────────────────────
    DB_OK              = 0x0000,  ///< 操作成功
    DB_UNKNOWN_ERROR   = 0x0001,  ///< 未分类错误
    DB_INVALID_PARAM   = 0x0002,  ///< 参数非法（空表名、负数长度等）
    DB_NOT_IMPLEMENTED = 0x0003,  ///< 功能尚未实现（Stub 占位）

    // ─── 文件 I/O (0x10) ──────────────────
    DB_ERR_FILE_NOT_FOUND    = 0x0010,  ///< 文件不存在
    DB_ERR_FILE_ALREADY_EXISTS = 0x0011, ///< 文件已存在
    DB_ERR_FILE_OPEN_FAILED  = 0x0012,  ///< 文件打开失败（权限/路径）
    DB_ERR_FILE_READ_FAILED  = 0x0013,  ///< 文件读取失败
    DB_ERR_FILE_WRITE_FAILED = 0x0014,  ///< 文件写入失败
    DB_ERR_FILE_CORRUPTED    = 0x0015,  ///< 文件数据损坏（大小不匹配等）

    // ─── 存储层 (0x20) ───────────────────
    DB_ERR_PAGE_NOT_FOUND    = 0x0020,  ///< 缓冲池中未找到目标页
    DB_ERR_BUFFER_FULL       = 0x0021,  ///< 缓冲池已满且无页面可淘汰
    DB_ERR_DICT_LOAD_FAILED  = 0x0022,  ///< 数据字典加载失败
    DB_ERR_TABLE_NOT_IN_DICT = 0x0023,  ///< 数据字典中找不到该表元信息

    // ─── DDL (0x30) ──────────────────────
    DB_ERR_TABLE_EXISTS      = 0x0030,  ///< 表已存在（CREATE TABLE 冲突）
    DB_ERR_TABLE_NOT_FOUND   = 0x0031,  ///< 表不存在（DROP / SELECT 目标缺失）
    DB_ERR_DB_EXISTS         = 0x0032,  ///< 数据库已存在
    DB_ERR_DB_NOT_FOUND      = 0x0033,  ///< 数据库不存在
    DB_ERR_FIELD_COUNT_ZERO = 0x0034,  ///< 字段数为零（空表不允许）
    DB_ERR_DUPLICATE_FIELD   = 0x0035,  ///< 表中存在重复字段名

    // ─── DML (0x40) ──────────────────────
    DB_ERR_RECORD_INSERT_FAILED = 0x0040,  ///< 记录插入失败
    DB_ERR_RECORD_NOT_FOUND     = 0x0041,  ///< 目标记录不存在（UPDATE/DELETE WHERE）
    DB_ERR_RECORD_READ_FAILED   = 0x0042,  ///< 记录读取失败（偏移越界等）
    DB_ERR_TYPE_MISMATCH       = 0x0043,  ///< 类型不匹配（如 INT 列写入字符串）

    // ─── 约束检查 (0x60) ─────────────────
    DB_ERR_PK_VIOLATION    = 0x0060,  ///< 主键重复
    DB_ERR_NOT_NULL_VIOLATION = 0x0061, ///< NOT NULL 字段插入 NULL
};

/**
 * @brief 将错误码转换为人类可读的错误消息
 * @param err 错误码枚举值
 * @return 对应的中文错误描述字符串
 */
inline const char* getErrorMessage(ErrorCode err) {
    switch (err) {
        case ErrorCode::DB_OK:                  return "操作成功";
        case ErrorCode::DB_UNKNOWN_ERROR:       return "未知错误";
        case ErrorCode::DB_INVALID_PARAM:       return "参数无效";
        case ErrorCode::DB_NOT_IMPLEMENTED:     return "功能尚未实现";

        case ErrorCode::DB_ERR_FILE_NOT_FOUND:          return "文件不存在";
        case ErrorCode::DB_ERR_FILE_ALREADY_EXISTS:     return "文件已存在";
        case ErrorCode::DB_ERR_FILE_OPEN_FAILED:        return "文件打开失败";
        case ErrorCode::DB_ERR_FILE_READ_FAILED:        return "文件读取失败";
        case ErrorCode::DB_ERR_FILE_WRITE_FAILED:       return "文件写入失败";
        case ErrorCode::DB_ERR_FILE_CORRUPTED:          return "文件数据损坏";

        case ErrorCode::DB_ERR_PAGE_NOT_FOUND:      return "缓冲池页面未找到";
        case ErrorCode::DB_ERR_BUFFER_FULL:         return "缓冲池已满";
        case ErrorCode::DB_ERR_DICT_LOAD_FAILED:    return "数据字典加载失败";
        case ErrorCode::DB_ERR_TABLE_NOT_IN_DICT:   return "表中无此记录元信息";

        case ErrorCode::DB_ERR_TABLE_EXISTS:           return "表已存在";
        case ErrorCode::DB_ERR_TABLE_NOT_FOUND:        return "表不存在";
        case ErrorCode::DB_ERR_DB_EXISTS:              return "数据库已存在";
        case ErrorCode::DB_ERR_DB_NOT_FOUND:           return "数据库不存在";
        case ErrorCode::DB_ERR_FIELD_COUNT_ZERO:       return "字段数不能为零";
        case ErrorCode::DB_ERR_DUPLICATE_FIELD:        return "字段名称重复";

        case ErrorCode::DB_ERR_RECORD_INSERT_FAILED:   return "记录插入失败";
        case ErrorCode::DB_ERR_RECORD_NOT_FOUND:       return "记录不存在";
        case ErrorCode::DB_ERR_RECORD_READ_FAILED:     return "记录读取失败";
        case ErrorCode::DB_ERR_TYPE_MISMATCH:          return "数据类型不匹配";

        case ErrorCode::DB_ERR_PK_VIOLATION:           return "主键冲突：重复值";
        case ErrorCode::DB_ERR_NOT_NULL_VIOLATION:     return "NOT NULL 约束违反：值为空";

        default:                                return "未定义错误码";
    }
}
