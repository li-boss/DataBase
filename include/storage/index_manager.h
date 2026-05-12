// include/storage/index_manager.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

#include "../common/db_errors.h"
#include "../common/db_structs.h"

/**
 * @file index_manager.h
 * @brief 索引管理器 — 提供索引的高级接口
 *
 * 职责：
 *   - 索引的创建与删除（含数据构建）
 *   - INSERT/DELETE 时自动更新索引
 *   - 等值查询时使用索引加速
 *
 * 设计：
 *   - 全部静态方法（与 DictManager 一致）
 *   - 底层调用 FileManager 的索引文件读写函数
 *   - 索引元数据由 DictManager 管理
 */
class IndexManager {
public:
    // ─── 索引维护 ──────────────────────────────────────

    /**
     * @brief 创建索引并构建索引数据（扫描全表）
     * @param indexName   索引名（如 idx_students_age）
     * @param tableName   所属表名
     * @param columnName  索引字段名
     * @param columnIndex 字段在表中的序号
     * @param keyType    索引键类型（DataType 枚举值）
     * @param keySize    索引键字节数
     * @return ErrorCode
     */
    static ErrorCode CreateIndex(const std::string& indexName,
                                  const std::string& tableName,
                                  const std::string& columnName,
                                  uint32_t columnIndex,
                                  uint32_t keyType,
                                  uint32_t keySize);

    /**
     * @brief 删除索引（元数据 + 索引文件）
     * @param indexName 索引名
     * @return ErrorCode
     */
    static ErrorCode DropIndex(const std::string& indexName);

    // ─── DML 钩子（INSERT/DELETE 时调用）────────────

    /**
     * @brief INSERT 时向索引插入条目
     * @param indexName    索引名
     * @param keyData     索引键数据指针
     * @param recordOffset 记录偏移量（.trd 文件中的字节偏移）
     * @return ErrorCode
     */
    static ErrorCode InsertEntry(const std::string& indexName,
                                  const void* keyData,
                                  uint32_t recordOffset);

    /**
     * @brief DELETE 时从索引删除条目
     * @param indexName   索引名
     * @param keyData     索引键数据指针
     * @param recordOffset 记录偏移量（用于精确定位）
     * @return ErrorCode
     */
    static ErrorCode DeleteEntry(const std::string& indexName,
                                  const void* keyData,
                                  uint32_t recordOffset);

    // ─── 查询加速 ──────────────────────────────────────

    /**
     * @brief 等值查询：根据 key 查找所有匹配的 recordOffset
     * @param indexName    索引名
     * @param keyData      查找键数据指针
     * @param outOffsets   输出：匹配的 recordOffset 列表
     * @return ErrorCode（DB_OK 即使没找到也返回 OK，需检查 outOffsets.empty()）
     */
    static ErrorCode Lookup(const std::string& indexName,
                             const void* keyData,
                             std::vector<uint32_t>& outOffsets);

private:
    // ─── 内部辅助 ──────────────────────────────────────

    /**
     * @brief 构建索引数据：扫描全表，为每个记录插入索引条目
     * @param hdr  表的 TableHeader
     * @param fields 字段定义列表
     * @param idxHdr 索引的 IndexHeader（含 keySize/keyType 等）
     * @return ErrorCode
     */
    static ErrorCode buildIndexData(const struct TableHeader& hdr,
                                     const std::vector<ColumnDef>& fields,
                                     const struct IndexHeader& idxHdr);
};
