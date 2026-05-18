#include "access/cli_handler.h"
#include "parser/sql_parser.h"
#include "engine/ddl_executor.h"
#include "engine/dml_executor.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QString>

using namespace ruanko::access;
using namespace ruanko::parser;
using namespace ruanko::engine;

CliHandler::CliHandler(QWidget *parent) : QDialog(parent) {
    setWindowTitle("RuankoDB 对话框交互终端");
    setFixedSize(400, 200);
}

// 启动对话框式交互
void CliHandler::run() {
    QMessageBox::information(this, "欢迎", "RuankoDB 对话框终端已启动\n输入 exit 退出");
    
    while (true) {
        bool ok;
        QString sql = QInputDialog::getMultiLineText(
            this, "输入 SQL", "请输入 SQL 语句：", "", &ok);
        
        if (!ok || sql.trimmed().isEmpty()) {
            continue;
        }
        
        std::string sql_str = sql.toStdString();
        
        if (sql_str == "exit" || sql_str == "quit") {
            QMessageBox::information(this, "退出", "已退出 RuankoDB 终端");
            break;
        }
        
        // 执行SQL（核心逻辑完全不变）
        auto res = execute_sql(sql_str);
        // 弹出结果对话框
        show_result_dialog(res);
    }
}

// 核心执行逻辑：完全不变！
CliHandler::ExecuteResult CliHandler::execute_sql(const std::string& sql) {
    ExecuteResult res{};
    
    if (sql.empty()) {
        res.error = common::ErrorCode::INVALID_ARG;
        res.msg = "SQL 不能为空";
        return res;
    }
    
    SqlParser parser;
    auto ast = parser.parse(sql);
    
    if (!ast) {
        res.error = common::ErrorCode::PARSE_ERROR;
        res.msg = "SQL 解析失败";
        return res;
    }
    
    // 调用引擎执行
    auto engine_res = engine::execute(ast.get());
    
    res.error = engine_res.error;
    res.msg = engine_res.msg;
    res.headers = engine_res.headers;
    res.rows = engine_res.rows;
    
    return res;
}

// 用表格对话框展示结果（关键修改）
void CliHandler::show_result_dialog(const ExecuteResult& res) {
    if (res.error != common::ErrorCode::OK) {
        QMessageBox::critical(this, "执行失败",
                              QString::fromStdString(res.msg));
        return;
    }
    
    if (res.headers.empty()) {
        QMessageBox::information(this, "执行成功",
                                 QString::fromStdString(res.msg));
        return;
    }
    
    // 表格弹窗展示查询结果
    QDialog dlg(this);
    dlg.setWindowTitle("执行结果");
    dlg.setMinimumSize(700, 400);
    
    auto* layout = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget;
    
    table->setColumnCount(res.headers.size());
    QStringList h;
    for (auto& s : res.headers) h << QString::fromStdString(s);
    table->setHorizontalHeaderLabels(h);
    
    table->setRowCount(res.rows.size());
    for (int i = 0; i < res.rows.size(); ++i) {
        auto& row = res.rows[i];
        for (int j = 0; j < row.size(); ++j) {
            table->setItem(i, j, new QTableWidgetItem(QString::fromStdString(row[j])));
        }
    }
    
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    dlg.exec();
}
