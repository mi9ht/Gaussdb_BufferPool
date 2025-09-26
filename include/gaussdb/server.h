#pragma once
#include "gaussdb/buffer_pool.h"
#include <string>

namespace gaussdb::server
{

    /**
     * Server - 使用 UNIX domain socket 监听请求并调用 BufferPool 接口。
     * 头文件只声明类接口，具体实现放在 src/server.cpp。
     */
    class Server
    {
    public:
        explicit Server(gaussdb::buffer::BufferPool *bp, const char *socket_file);
        ~Server();

        // 创建 socket 并 bind（返回 0 表示成功）
        int create_socket();

        // 进入主循环，阻塞直到退出（SIGINT 等会设置全局标志）
        void listen_forever();

    private:
        // 不将实现细节暴露在头文件中
        struct Impl;
        Impl *pimpl_;
    };

} // namespace gaussdb::server
