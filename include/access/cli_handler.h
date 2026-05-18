#pragma once
#include <string>
#include <vector>
#include "../common/db_errors.h"
#include <QDialog>
#include <QTableWidget>

namespace ruanko::access {

// 对话框式 CLI 处理器
class CliHandler : public QDialog {
    Q_OBJECT
public:
    explicit CliHandler(QWidget *parent = nullptr);
    void run(); // 启动对话框交互
    
private:
    // 执行SQL（核心逻辑不变）
    struct ExecuteResult {
        common::ErrorCode error;
        std::string msg;
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
    };
    
    ExecuteResult execute_sql(const std::string& sql);
    void show_result_dialog(const ExecuteResult& res); // 结果弹窗
};

} // namespace ruanko::access
