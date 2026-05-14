// include/storage/index_manager.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

#include "../common/db_errors.h"
#include "../common/db_structs.h"

/**
 * @file index_manager.h
 * @brief 索引管理器 — 提供索引的高级接口（表级）
 *
 * 职责：
 *   - 索引的创建与删除（含数据构建）
 *   - INSERT/DELETE 后自动更新表上所有索引
 *   - 等值查询时使用索引加速
 *
 * 设计：
 *   - 全部静态方法
 *   - 底层调用 FileManager 的索引文件读写函数
 *   - 索引元数据由 DictManager 管理
 *   - Engine 层只接触表级接口，不关心具体索引名
 */
class IndexManager {
public:
    // ─── 索引维护 ──────────────────────────────────────

    /**
     * @brief 创建索引并构建索引数据（扫描全表）
     * @param indexName   索引名（如 idx_students_age）
     * @param tableName   所属表名
     * @param columnName  索引字段名
     * @return ErrorCode
     */
    static ErrorCode CreateIndex(const std::string& indexName,
                                  const std::string& tableName,
                                  const std::string& columnName);

    /**
     * @brief 删除索引（元数据 + 索引文件）
     * @param indexName 索引名
     * @return ErrorCode
     */
    static ErrorCode DropIndex(const std::string& indexName);

    /**
     * @brief 列出表上所有索引名
     * @param tableName   表名
     * @param outNames   输出：索引名列表
     * @return ErrorCode
     */
    static ErrorCode ListIndexes(const std::string& tableName,
                                  std::vector<std::string>& outNames);

    // ─── 查询加速 ──────────────────────────────────────

    /**
     * @brief 等值查询：根据 key 查找所有匹配的 recordOffset
     * @param indexName    索引名
     * @param keyData      查找键数据指针
     * @param keySize      键字节数（根据 keyType 计算：INT/DATETIME/BOOLEAN 为 4 字节，其余为字段长度）
     * @param outOffsets   输出：匹配的 recordOffset 列表
     * @return ErrorCode（DB_OK 即使没找到也返回 OK，需检查 outOffsets.empty()）
     */
    static ErrorCode Lookup(const std::string& indexName,
                          const void* keyData,
                          uint32_t keySize,
                          std::vector<uint32_t>& outOffsets);

    // ─── DML 钩子（INSERT/DELETE 时调用，表级）────────────

    /**
     * @brief INSERT 后：向表上所有索引插入条目
     * @param tableName     表名
     * @param recordOffset 记录偏移量（.trd 文件中的字节偏移）
     * @param recordData   完整记录数据指针（用于提取各索引的键）
     * @param fields       字段定义列表（含 offset/length/type）
     * @return ErrorCode
     */
    static ErrorCode InsertEntry(const std::string& tableName,
                                  uint32_t recordOffset,
                                  const void* recordData,
                                  const std::vector<ColumnDef>& fields);

    /**
     * @brief DELETE 后：从表上所有索引删除条目
     * @param tableName     表名
     * @param recordOffset 记录偏移量
     * @param recordData   完整记录数据指针（用于提取各索引的键）
     * @param fields       字段定义列表
     * @return ErrorCode
     */
    static ErrorCode DeleteEntry(const std::string& tableName,
                                 uint32_t recordOffset,
                                 const void* recordData,
                                 const std::vector<ColumnDef>& fields);

    /**
     * @brief UPDATE 后：先从旧记录删索引条目，再向新记录插索引条目
     * @param tableName     表名
     * @param recordOffset 记录偏移量
     * @param oldData      更新前的记录数据
     * @param newData      更新后的记录数据
     * @param fields       字段定义列表
     * @return ErrorCode
     */
    static ErrorCode UpdateEntry(const std::string& tableName,
                                  uint32_t recordOffset,
                                  const void* oldData,
                                  const void* newData,
                                  const std::vector<ColumnDef>& fields);

private:
    // ─── 内部辅助 ──────────────────────────────────────

    /**
     * @brief 构建索引数据：扫描全表，为每个记录插入索引条目
     * @param hdr      表的 TableHeader
     * @param fields   字段定义列表
     * @param idxHdr   索引的 IndexHeader（含 columnIndex/keyType 等）
     * @return ErrorCode
     */
    static ErrorCode buildIndexData(const struct TableHeader& hdr,
                                     const std::vector<ColumnDef>& fields,
                                     const struct IndexHeader& idxHdr);

public:
    /**
     * @brief 重建指定表的所有索引（DELETE 紧缩后调用）
     *        扫描全表数据，为每个索引重新生成 .idx 文件
     * @param tableName 表名
     * @return ErrorCode
     */
    static ErrorCode RebuildAllIndexes(const std::string& tableName);
};
