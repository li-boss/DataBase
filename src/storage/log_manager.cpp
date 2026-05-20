// src/storage/log_manager.cpp
#include "../../include/storage/log_manager.h"
#include "../../include/storage/dict_manager.h"
#include "../../include/storage/file_manager.h"
#include "../../include/common/db_errors.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstring>
#include <vector>
#include <algorithm>

// ─── log ──────────────────────────────────────────────
void LogManager::log(OpType op, const std::string& msg, Level level) {
    std::string dbDir = DictManager::GetCurrentDB();
    if (dbDir.empty()) {
        // 没有选中的数据库，输出到 stderr
        std::cerr << "[" << timestamp() << "] [" << levelToString(level)
                  << "] " << opToString(op) << " " << msg << "\n";
        return;
    }

    std::string logPath = dbDir + "/ruanko.log";

    // 追加写入
    std::ofstream ofs(logPath, std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "[LogManager] Failed to open log file: " << logPath << "\n";
        return;
    }

    ofs << "[" << timestamp() << "] "
        << "[" << levelToString(level) << "] "
        << opToString(op) << " " << msg << "\n";
    ofs.close();
}

void LogManager::error(OpType op, const std::string& msg) {
    log(op, msg, Level::ERROR);
}

void LogManager::warn(OpType op, const std::string& msg) {
    log(op, msg, Level::WARN);
}

// ─── readLog ─────────────────────────────────────────
bool LogManager::readLog(std::vector<std::string>& outLines, size_t maxLines) {
    std::string dbDir = DictManager::GetCurrentDB();
    if (dbDir.empty()) return false;

    std::string logPath = dbDir + "/ruanko.log";
    std::ifstream ifs(logPath);
    if (!ifs.is_open()) return false;

    // 全部读入
    std::vector<std::string> allLines;
    std::string line;
    while (std::getline(ifs, line)) {
        allLines.push_back(line);
    }
    ifs.close();

    // 取最后 maxLines 行
    if (maxLines == 0 || maxLines >= allLines.size()) {
        outLines = std::move(allLines);
    } else {
        outLines.assign(allLines.end() - maxLines, allLines.end());
    }
    return true;
}

// ─── opToString ──────────────────────────────────────
const char* LogManager::opToString(OpType op) {
    switch (op) {
        case OpType::CREATE_DB:     return "CREATE_DB";
        case OpType::DROP_DB:       return "DROP_DB";
        case OpType::USE_DB:        return "USE_DB";
        case OpType::CREATE_TABLE:  return "CREATE_TABLE";
        case OpType::DROP_TABLE:    return "DROP_TABLE";
        case OpType::ALTER_TABLE:   return "ALTER_TABLE";
        case OpType::CREATE_INDEX:  return "CREATE_INDEX";
        case OpType::DROP_INDEX:    return "DROP_INDEX";
        case OpType::CREATE_VIEW:   return "CREATE_VIEW";
        case OpType::DROP_VIEW:     return "DROP_VIEW";
        case OpType::INSERT:        return "INSERT";
        case OpType::UPDATE:        return "UPDATE";
        case OpType::DELETE:        return "DELETE";
        case OpType::SELECT:        return "SELECT";
        case OpType::BEGIN_TX:      return "BEGIN";
        case OpType::COMMIT_TX:     return "COMMIT";
        case OpType::ROLLBACK_TX:   return "ROLLBACK";
        case OpType::SYSTEM:        return "SYSTEM";
    }
    return "UNKNOWN";
}

// ─── levelToString ───────────────────────────────────
const char* LogManager::levelToString(Level level) {
    switch (level) {
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

// ─── timestamp ───────────────────────────────────────
std::string LogManager::timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}
