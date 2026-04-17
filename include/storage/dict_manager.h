// include/storage/dict_manager.h
#pragma once

#include <string>
#include <vector>

#include "../common/db_errors.h"
#include "../common/db_structs.h"

/**
 * @file dict_manager.h
 * @brief 数据字典管理器 — 管理 .tb / .tdf 元数据的读取与查询
 *
 * 职责：
 *   - 加载表元信息（TableHeader + FieldDefinition 列表）
 *   - 提供上层 Engine 查询表结构的统一入口
 *   - 维护记录计数（INSERT/DELETE 后更新 .tb 文件）
 *
 * 设计原则：
 *   - 全部静态方法，无状态（不缓存），每次查询直接读文件
 *   - 未来可扩展为带缓存的版本，但当前保持简单可靠
 */

class DictManager {
public:

    // ─── 核心查询 ────────────────────────────────────────

    /**
     * @brief 从磁盘加载指定表的完整元信息
     *
     * 依次读取 tableName.tb 和 tableName.tdf，
     * 返回 TableHeader + 字段定义列表。
     *
     * @param[in]  tableName 目标表名（不含后缀）
     * @param[out] header    输出：表头信息
     * @param[out] fields    输出：字段定义数组
     * @return ErrorCode 错误码，DB_OK 表示成功
     */
    static ErrorCode loadTable(const std::string& tableName,
                               TableHeader& header,
                               std::vector<FieldDefinition>& fields);

    /**
     * @brief 仅加载表头（不读字段列表，轻量级查询）
     * @param[in]  tableName 表名
     * @param[out] header    输出表头
     * @return ErrorCode
     */
    static ErrorCode loadTableHeader(const std::string& tableName,
                                     TableHeader& header);

    // ─── 存在性检查 ──────────────────────────────────────

    /**
     * @brief 检查表是否已存在（通过 .tb 文件判断）
     * @param tableName 表名
     * @return true 表已存在
     */
    static bool tableExists(const std::string& tableName);

    // ─── 元数据更新 ──────────────────────────────────────

    /**
     * @brief 更新 .tb 文件中的记录计数（INSERT / DELETE 后调用）
     * @param tableName  表名
     * @param recordCount 新的记录数
     * @return ErrorCode
     */
    static ErrorCode updateRecordCount(const std::string& tableName,
                                       uint32_t recordCount);

    /**
     * @brief 更新 .tb 文件中的修改时间戳为当前时间
     * @param tableName 表名
     * @return ErrorCode
     */
    static ErrorCode touchModifyTime(const std::string& tableName);
};
