// include/storage/log_manager.h
#pragma once
#include <string>
#include <functional>

/**
 * @file log_manager.h
 * @brief 数据库日志管理器 — 记录关键 DDL/DML 操作和错误
 *
 * 日志文件位置：<当前数据库目录>/ruanko.log
 * 格式：[时间戳] [级别] 操作类型 详情
 *
 * 线程安全：使用文件追加模式，单写者模型
 */
class LogManager {
public:
    /// 日志级别
    enum class Level { INFO, WARN, ERROR };

    /// 操作类型（用于审计追踪）
    enum class OpType {
        CREATE_DB, DROP_DB, USE_DB,
        CREATE_TABLE, DROP_TABLE, ALTER_TABLE,
        CREATE_INDEX, DROP_INDEX,
        INSERT, UPDATE, DELETE, SELECT,
        BEGIN_TX, COMMIT_TX, ROLLBACK_TX,
        SYSTEM  // 系统级消息（启动/关闭等）
    };

    /**
     * @brief 记录一条日志
     * @param op    操作类型
     * @param msg   日志详情
     * @param level 日志级别，默认 INFO
     */
    static void log(OpType op, const std::string& msg,
                    Level level = Level::INFO);

    /**
     * @brief 记录错误日志
     */
    static void error(OpType op, const std::string& msg);

    /**
     * @brief 记录警告日志
     */
    static void warn(OpType op, const std::string& msg);

    /**
     * @brief 读取当前数据库的日志内容
     * @param outLines  输出：日志行列表
     * @param maxLines  最大读取行数（0=全部，默认 100）
     * @return 是否成功读取
     */
    static bool readLog(std::vector<std::string>& outLines,
                        size_t maxLines = 100);

private:
    /**
     * @brief 操作类型转可读字符串
     */
    static const char* opToString(OpType op);

    /**
     * @brief 日志级别转可读字符串
     */
    static const char* levelToString(Level level);

    /**
     * @brief 获取当前时间戳字符串
     */
    static std::string timestamp();
};
