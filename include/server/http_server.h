// include/server/http_server.h
#pragma once

namespace Ruanko {
    class HttpServer {
    public:
        // 启动 HTTP 服务器监听前端 API 请求
        static void Start(int port = 8080);
    };
}
