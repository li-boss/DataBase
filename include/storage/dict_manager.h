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
 *   - 数据库级管理（CreateDatabase / DropDatabase / UseDatabase / ShowTables）
 *   - 加载表元信息（TableHeader + ColumnDef 列表）
 *   - 提供上层 Engine 查询表结构的统一入口
 *   - 维护记录计数（INSERT/DELETE 后更新 .tb 文件）
 *
 * 设计原则：
 *   - 全部静态方法，无状态（不缓存），每次查询直接读文件
 */

class DictManager {
public:

    // ─── 数据库级接口 ─────────────────────────────────────

    /**
     * @brief 创建数据库（创建 db 目录和 ruanko.db 元文件）
     * @param dbName 数据库名称
     * @return ErrorCode
     */
    static ErrorCode CreateDatabase(const std::string& dbName);

    /**
     * @brief 删除数据库（递归删除目录及所有表文件）
     * @param dbName 数据库名称
     * @return ErrorCode
     */
    static ErrorCode DropDatabase(const std::string& dbName);

    /**
     * @brief 切换当前数据库上下文
     * @param dbName 数据库名称
     * @return ErrorCode
     */
    static ErrorCode UseDatabase(const std::string& dbName);

    /**
     * @brief 列出当前数据库下所有表名
     * @param outTables 输出：表名列表
     * @return ErrorCode
     */
    static ErrorCode ShowTables(std::vector<std::string>& outTables);

    /**
     * @brief 获取当前所在数据库名称/路径
     * @return 当前所在数据库路径
     */
    static std::string GetCurrentDB();

    // ─── 核心查询 ────────────────────────────────────────

    /**
     * @brief 从磁盘加载指定表的完整元信息（方案接口：GetTableHeader 扩展版）
     *
     * 依次读取 tableName.tb 和 tableName.tdf，
     * 返回 TableHeader + 字段定义列表。
     *
     * @param[in]  tableName 目标表名（不含后缀）
     * @param[out] header    输出：表头信息
     * @param[out] fields    输出：字段定义数组（ColumnDef）
     * @return ErrorCode 错误码，DB_OK 表示成功
     */
    static ErrorCode loadTable(const std::string& tableName,
                               TableHeader& header,
                               std::vector<ColumnDef>& fields);

    /**
     * @brief 获取表头（方案规定接口：GetTableHeader）
     * @param[in]  tableName 表名
     * @param[out] header    输出表头
     * @return ErrorCode
     */
    static ErrorCode GetTableHeader(const std::string& tableName,
                                    TableHeader& header);

    /**
     * @brief 仅加载表头（轻量级，与 GetTableHeader 等价，保持内部兼容）
     */
    static ErrorCode loadTableHeader(const std::string& tableName,
                                     TableHeader& header);

    // ─── 存在性检查 ──────────────────────────────────────

    /**
     * @brief 检查表是否已存在（通过 .tb 文件判断）
     */
    static bool tableExists(const std::string& tableName);

    // ─── 元数据更新 ──────────────────────────────────────

    /**
     * @brief 更新 .tb 文件中的记录计数（INSERT / DELETE 后调用）
     */
    static ErrorCode updateRecordCount(const std::string& tableName,
                                       uint32_t recordCount);

    /**
     * @brief 更新 .tb 文件中的修改时间戳为当前时间
     */
    static ErrorCode touchModifyTime(const std::string& tableName);

    // ─── 索引元数据管理 ──────────────────────────────────

    /**
     * @brief 创建索引元数据：写入 _index_meta.tdf + 创建空的 .idx 文件
     * @param indexName   索引名称（如 idx_students_age）
     * @param tableName   所属表名
     * @param columnName  索引字段名
     * @param columnIndex 字段在表中的序号
     * @param keyType     索引键类型（DataType 枚举值）
     * @return ErrorCode
     */
    static ErrorCode CreateIndex(const std::string& indexName,
                                  const std::string& tableName,
                                  const std::string& columnName,
                                  uint32_t columnIndex,
                                  uint32_t keyType);

    /**
     * @brief 创建复合索引元数据（多列）
     * @param indexName    索引名称
     * @param tableName    所属表名
     * @param columnNames  索引字段名列表（如 {"name","age","score"}）
     * @param columnIndices 各字段在表中的序号（需与 fields 对应）
     * @param keyTypes     各字段的类型（DataType 枚举值，用于计算 key 大小）
     * @param fields       字段定义列表（用于计算 compositeKeySize）
     * @return ErrorCode
     */
    static ErrorCode CreateIndex(const std::string& indexName,
                                  const std::string& tableName,
                                  const std::vector<std::string>& columnNames,
                                  const std::vector<uint32_t>& columnIndices,
                                  const std::vector<uint32_t>& keyTypes,
                                  const std::vector<ColumnDef>& fields);

    /**
     * @brief 删除索引：从 _index_meta.tdf 移除 + 删除 .idx 文件
     * @param indexName 索引名称
     * @return ErrorCode
     */
    static ErrorCode DropIndex(const std::string& indexName);

    /**
     * @brief 列出指定表上所有索引名
     * @param tableName       表名
     * @param outIndexNames  输出：索引名列表
     * @return ErrorCode
     */
    static ErrorCode ListIndexes(const std::string& tableName,
                                  std::vector<std::string>& outIndexNames);

    /**
     * @brief 加载指定表的所有索引元数据（IndexHeader 列表）
     * @param tableName     表名
     * @param outHeaders   输出：IndexHeader 列表
     * @return ErrorCode
     */
    static ErrorCode LoadTableIndexes(const std::string& tableName,
                                       std::vector<IndexHeader>& outHeaders);

    /**
     * @brief 根据索引名查找其 IndexHeader
     * @param indexName    索引名
     * @param outHeader    输出：索引头信息
     * @return ErrorCode
     */
    static ErrorCode GetIndexHeader(const std::string& indexName,
                                     IndexHeader& outHeader);

    /**
     * @brief 同步内存缓存中的索引元数据（entryCount 等字段变更后调用）
     * @param hdr 最新的 IndexHeader
     */
    static void UpdateIndexCache(const IndexHeader& hdr);

    // ─── 内部辅助 ──────────────────────────────────────
    static std::string indexMetaPath();  // 返回 _index_meta.tdf 完整路径
};

extern std::string g_currentDbDir;  // 当前数据库目录（全局，供其他模块 extern 使用）
