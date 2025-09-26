// src/server.cpp
#include "gaussdb/server.h"
#include "gaussdb/buffer_pool.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cassert>
#include <iostream>
#include <cstring>
#include <array>
#include <memory>

using namespace std;
using gaussdb::buffer::BufferPool;
using gaussdb::buffer::pageno;

/* Global shutdown socket/state (kept for compatibility with example.cc's signal handler) */
static bool g_program_shutdown = false;
static int server_socket = -1;

/* simple logging */
#define LOG_INFO(msg)                     \
    do                                    \
    {                                     \
        cerr << "[INFO] " << msg << endl; \
    } while (0)
#define LOG_ERROR(msg)                     \
    do                                     \
    {                                      \
        cerr << "[ERROR] " << msg << endl; \
    } while (0)
#define LOG_DEBUG(msg)                     \
    do                                     \
    {                                      \
        cerr << "[DEBUG] " << msg << endl; \
    } while (0)

/* Message types and header (same layout as example) */
enum MSG_TYPE
{
    GET = 0,
    SET,
    INVALID_TYPE
};

struct __attribute__((packed)) Header
{
    unsigned char msg_type;
    unsigned int page_no;
    unsigned int page_size;
};

namespace gaussdb::server
{

    /* Impl hides details from header */
    struct Server::Impl
    {
        sockaddr_un m_server_addr{};
        BufferPool *m_bufferpool;
        const char *m_socket_file;

        Impl(BufferPool *bp, const char *socket_file) : m_bufferpool(bp), m_socket_file(socket_file) {}
    };

    /* read/write loop helpers */
    static int read_loop(int fd, unsigned char *buf, uint count)
    {
        int ret = count;
        while (count)
        {
            ssize_t recvcnt = ::read(fd, buf, count);
            if (recvcnt == -1)
            {
                if (errno == EINTR)
                    continue;
                LOG_ERROR("Package read error: " << strerror(errno));
                return -1;
            }
            else if (recvcnt == 0)
            {
                LOG_DEBUG("Connection disconnected.");
                return 0;
            }
            count -= recvcnt;
            buf += recvcnt;
        }
        return ret;
    }

    static int write_loop(int fd, unsigned char *buf, uint count)
    {
        int ret = count;
        while (count)
        {
            ssize_t sendcnt = ::write(fd, buf, count);
            if (sendcnt == -1)
            {
                if (errno == EINTR)
                    continue;
                LOG_ERROR("Package write error: " << strerror(errno));
                return -1;
            }
            else if (sendcnt == 0)
            {
                LOG_INFO("Connection disconnected.");
                return 0;
            }
            count -= sendcnt;
            buf += sendcnt;
        }
        return ret;
    }

    /* Thread worker data (managed by Server::listen_forever) */
    struct ThreadData
    {
        std::thread th;
        BufferPool *bufferpool;
        int client_socket;
        int thread_index;

        ThreadData(BufferPool *bp, int socket, int t_idx)
            : bufferpool(bp), client_socket(socket), thread_index(t_idx) {}
    };

    /* thread handler now returns void and accepts ThreadData* */
    static void thread_handler(ThreadData *worker_data)
    {
        auto *buffer = new unsigned char[2 * 1024 * 1024];
        while (!g_program_shutdown)
        {
            Header header{};
            int rr = read_loop(worker_data->client_socket, (unsigned char *)&header, sizeof(header));
            if (rr <= 0)
                break;

            switch (header.msg_type)
            {
            case SET:
                if (read_loop(worker_data->client_socket, buffer, header.page_size) <= 0)
                {
                    break;
                }
                worker_data->bufferpool->write_page(header.page_no, header.page_size, buffer, worker_data->thread_index);
                if (write_loop(worker_data->client_socket,
                               (unsigned char *)&header.page_size,
                               sizeof(header.page_size)) <= 0)
                {
                }
                break;
            case GET:
                worker_data->bufferpool->read_page(header.page_no, header.page_size, buffer, worker_data->thread_index);
                if (write_loop(worker_data->client_socket,
                               (unsigned char *)&header.page_size,
                               sizeof(header.page_size)) <= 0)
                {
                }
                if (write_loop(worker_data->client_socket, buffer, header.page_size) <= 0)
                {
                }
                break;
            default:
                LOG_ERROR("Invalid msg type");
            }
        }
        delete[] buffer;
        LOG_DEBUG("Thread exit for socket " << worker_data->client_socket);
        // try to close client socket if not already closed
        ::close(worker_data->client_socket);
        // show stats (no-op for simple pool)
        if (worker_data->bufferpool)
            worker_data->bufferpool->show_hit_rate();
    }

    Server::Server(BufferPool *bp, const char *socket_file)
    {
        pimpl_ = new Impl(bp, socket_file);
    }

    Server::~Server()
    {
        delete pimpl_;
    }

    /* create socket and bind -- unchanged logic */
    int Server::create_socket()
    {
        server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_socket < 0)
        {
            LOG_ERROR("Create server socket failed, errno = " << strerror(errno));
            return -1;
        }

        memset(&pimpl_->m_server_addr, 0, sizeof(pimpl_->m_server_addr));
        strncpy(pimpl_->m_server_addr.sun_path, pimpl_->m_socket_file, sizeof(pimpl_->m_server_addr.sun_path) - 1);
        pimpl_->m_server_addr.sun_family = AF_UNIX;
        unlink(pimpl_->m_socket_file);

        if (bind(server_socket, (struct sockaddr *)&pimpl_->m_server_addr, sizeof(pimpl_->m_server_addr)) == -1)
        {
            LOG_ERROR("Bind server socket failed, errno = " << strerror(errno));
            return -1;
        }
        LOG_INFO("Create socket success.");
        return 0;
    }

    /* listen & accept loop -- uses std::thread for workers */
    void Server::listen_forever()
    {
        if (listen(server_socket, 1000) == -1)
        {
            LOG_ERROR("Listen server socket failed, errno = " << strerror(errno));
            return;
        }

        std::vector<std::unique_ptr<ThreadData>> workers;
        int thread_count = 0;

        while (!g_program_shutdown)
        {
            int client_socket = accept(server_socket, nullptr, nullptr);
            if (client_socket == -1)
            {
                if (g_program_shutdown)
                {
                    LOG_INFO("Accept aborted due to shutdown.");
                }
                else
                {
                    LOG_ERROR("Accept failed, errno = " << strerror(errno));
                }
                break;
            }

            // create worker object and push to vector BEFORE starting thread to avoid pointer invalidation
            auto worker = std::make_unique<ThreadData>(pimpl_->m_bufferpool, client_socket, thread_count++);
            workers.push_back(std::move(worker));
            ThreadData *wd = workers.back().get();

            try
            {
                wd->th = std::thread(thread_handler, wd);
                LOG_DEBUG("Thread created for client socket " << client_socket);
            }
            catch (const std::system_error &e)
            {
                LOG_ERROR("Create thread failed: " << e.what());
                shutdown(wd->client_socket, SHUT_RDWR);
                // remove the failed worker entry
                workers.pop_back();
            }
        }

        LOG_INFO("Start shutting down.");
        // Shutdown and join all workers
        for (auto &wptr : workers)
        {
            if (!wptr)
                continue;
            // request socket close so threads unblock from read
            if (wptr->client_socket > 0)
            {
                shutdown(wptr->client_socket, SHUT_RDWR);
            }
        }

        // join threads
        for (auto &wptr : workers)
        {
            if (!wptr)
                continue;
            if (wptr->th.joinable())
            {
                try
                {
                    wptr->th.join();
                }
                catch (const std::system_error &e)
                {
                    LOG_ERROR("Thread join error: " << e.what());
                }
            }
        }

        // remove socket file
        unlink(pimpl_->m_socket_file);
        LOG_INFO("Server closed.");
    }

} // namespace gaussdb::server
