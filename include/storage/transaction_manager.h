// include/storage/transaction_manager.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "common/db_errors.h"

/**
 * @file transaction_manager.h
 * @brief 事务管理器 — 支持 BEGIN / COMMIT / ROLLBACK 基础流程
 *
 * 设计：
 *   - BEGIN 时，将当前表数据文件(.trd)快照到内存
 *   - COMMIT 时，释放快照
 *   - ROLLBACK 时，用快照恢复 .trd 文件和 recordCount
 *
 * 限制：
 *   - 仅支持单表单事务（同时只能有一个活跃事务）
 *   - 不支持跨表事务
 *   - ROLLBACK 会撤销 BEGIN 之后的所有 DML 操作
 */
class TransactionManager {
public:
    /**
     * @brief 开启事务（快照当前表的数据文件）
     * @param tableName  表名
     * @return ErrorCode
     */
    static ErrorCode begin(const std::string& tableName);

    /**
     * @brief 提交事务（释放快照）
     * @return ErrorCode
     */
    static ErrorCode commit();

    /**
     * @brief 回滚事务（用快照恢复 .trd 和 recordCount）
     * @return ErrorCode
     */
    static ErrorCode rollback();

    /**
     * @brief 是否有活跃事务
     */
    static bool isActive();

    /**
     * @brief 获取当前事务关联的表名
     */
    static const std::string& getTableName();

private:
    static bool s_active;                          // 是否有活跃事务
    static std::string s_tableName;                // 事务关联的表名
    static std::vector<char> s_snapshotData;       // .trd 文件快照
    static uint32_t s_snapshotRecordCount;         // 快照时的 recordCount
};
