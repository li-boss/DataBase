// include/server/http_server.h
#pragma once

namespace Ruanko {
    class HttpServer {
    public:
        // 启动 HTTP 服务器监听前端 API 请求（阻塞，直到 Stop() 被调用）
        static void Start(int port = 8080);

        // 从外部（如信号处理器）停止服务器，使 Start() 返回
        static void Stop();
    };
}
