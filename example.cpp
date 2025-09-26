#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <csignal>
#include <vector>
#include <fcntl.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <log4cxx/propertyconfigurator.h>
#include <cassert>
#include <unordered_map>
#include <list>

using namespace std;
using pageno = unsigned int;

static bool g_program_shutdown = false;
static int server_socket = -1;
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("EXAMPLE");

enum MSG_TYPE {
  GET = 0,
  SET,
  INVALID_TYPE
};

struct __attribute__((packed)) Header {
  unsigned char msg_type;
  unsigned int page_no;
  unsigned int page_size;
};

/** Buffer Pool申明，请按规则实现以下接口*/
class BufferPool {
 protected:
  /** 原始文件 */
  string file_name;
  /** 请根据页面大小从小到大依次编号，即不同的页拥有不同的大小。
   * {{8k, 1024}, {16k, 2048}} 的正确编号为 8kb [0-1023] 16kb [1024-3071]*/
  map<size_t, size_t> page_no_info;
 public:
  /** buffer pool 的最大大小，超过该大小将导致程序OOM */
  static constexpr size_t max_buffer_pool_size = 4l * 1024 * 1024 * 1024;

  BufferPool(string file_name, const map<size_t, size_t> &page_no_info)
      : file_name(std::move(file_name)), page_no_info(page_no_info) {
    /** 初始化你的buffer pool，初始化耗时不得超过3分钟 */
  };

  /** 读取一个指定页号为#no的页面, 将内容填充只buf */
  virtual void read_page(pageno no, unsigned int page_size, void *buf, int t_idx) {};

  /** 将buf内容写入一个指定页号为#no的页面 */
  virtual void write_page(pageno no, unsigned int page_size, void *buf, int t_idx) {};

  virtual void show_hit_rate() {};

  /** 资源清理 */
  virtual ~BufferPool() = default;
};

class SimpleBufferPool : public BufferPool {
 protected:
  map<size_t, size_t> page_no_info;
  int fds[32]{};
 public:
  SimpleBufferPool(const string &file_name,
                   const map<size_t, size_t> &page_no_info)
      : BufferPool(file_name, page_no_info), page_no_info(page_no_info) {
    LOG4CXX_INFO(logger, "BufferPool using file " << file_name);

    for (int &fd : fds) {
      fd = open(file_name.c_str(), O_RDWR);
      if (fd <= 0) {
        throw runtime_error("Can not open the source file");
      }
    }
  }

  size_t page_start_offset(pageno no) {
    size_t boundary = 0;
    for (auto &range : page_no_info) {
      if (no >= range.second) {
        boundary += range.first * range.second;
        no -= range.second;
      } else {
        return boundary + (no * range.first);
      }
    }
    return -1;
  }

  void read_page(pageno no, unsigned int page_size, void *buf, int t_idx) override {
    size_t offset = page_start_offset(no);
    int fd = fds[t_idx];
    lseek(fd, offset, SEEK_SET);
    size_t ret = read(fd, buf, page_size);
    if (ret != page_size) {
      LOG4CXX_FATAL(logger,
                    "read size error read:" << ret << " page size: " << page_size << " errno: " << errno << " page no: "
                                            << no)
    }
    assert(ret == page_size);
  }

  void write_page(pageno no, unsigned int page_size, void *buf, int t_idx) override {
    uint64_t offset = page_start_offset(no);
    int fd = fds[t_idx];
    lseek(fd, offset, SEEK_SET);
    size_t ret = write(fd, buf, page_size);
    assert(ret == page_size);
  }

  ~SimpleBufferPool() override {
    for (int &fd : fds) {
      close(fd);
    }
  }
};

class Server {
 private:
  struct sockaddr_un m_server_addr{};
  BufferPool *m_bufferpool;
  const char *m_socket_file;

  struct ThreadData {
    BufferPool *bufferpool;
    pthread_t thread_id{};
    int client_socket;
    int thread_index;
    ThreadData(BufferPool *bp, int socket, int t_idx)
        : bufferpool(bp), client_socket(socket), thread_index(t_idx) {}
  };

 public:
  explicit Server(BufferPool *bp, const char *socket_file) : m_bufferpool(bp), m_socket_file(socket_file) {}
  
  int create_socket() {
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0) {
      LOG4CXX_ERROR(logger, "Create server socket failed, errno = " << errno);
      return -1;
    }

    memset(&m_server_addr, 0, sizeof(m_server_addr));
    strcpy(m_server_addr.sun_path, m_socket_file);
    m_server_addr.sun_family = AF_UNIX;
    unlink(m_socket_file);

    if (bind(server_socket, (struct sockaddr *) &m_server_addr, sizeof(m_server_addr)) == -1) {
      LOG4CXX_ERROR(logger, "Bind server socket failed, errno = " << errno);
      return -1;
    }
    LOG4CXX_INFO(logger, "Create socket success.");
    return 0;
  }

  void listen_forever() {
    if (listen(server_socket, 1000) == -1) {
      LOG4CXX_ERROR(logger, "Listen server socket failed, errno = " << errno);
      return;
    }

    std::vector<ThreadData *> workers;
    int thread_count = 0;
    while (!g_program_shutdown) {
      pthread_t worker_thread;
      int client_socket = accept(server_socket, nullptr, nullptr);
      if (client_socket == -1) {
        if (g_program_shutdown) {
          LOG4CXX_INFO(logger, "Accecpt abort");
        } else {
          LOG4CXX_ERROR(logger, "Accept failed, errno = " << errno);
        }
        break;
      }
      auto *worker_data = new ThreadData(m_bufferpool, client_socket, thread_count++);

      if (!pthread_create(&worker_thread, nullptr, thread_handler, (void *) worker_data)) {
        worker_data->thread_id = worker_thread;
        workers.push_back(worker_data);
        LOG4CXX_DEBUG(logger, "Thread created, id = " << worker_thread);
      } else {
        LOG4CXX_ERROR(logger, "Create thread failed, errno = " << errno);
        shutdown(worker_data->client_socket, SHUT_RDWR);
      }
    }
    LOG4CXX_INFO(logger, "Start shutting down.");
    for (auto worker : workers) {
      shutdown(worker->client_socket, SHUT_RDWR);
      pthread_join(worker->thread_id, nullptr);
    }
    unlink(m_socket_file);
    LOG4CXX_INFO(logger, "Server closed.");
  }

 private:
  static int read_loop(int fd, unsigned char *buf, uint count, log4cxx::LoggerPtr &log) {
    int ret = count;
    while (count) {
      size_t recvcnt = read(fd, buf, count);
      if (recvcnt == -1) {
        if (errno == EINTR) {
          continue;
        }
        LOG4CXX_ERROR(log, "Package read error.");
        return -1;
      } else if (recvcnt == 0) {
        LOG4CXX_DEBUG(log, "Connection disconnected.");
        return 0;
      }

      count -= recvcnt;
      buf += recvcnt;
    }
    return ret;
  }

  static int write_loop(int fd, unsigned char *buf, uint count, log4cxx::LoggerPtr &log) {
    int ret = count;
    while (count) {
      size_t sendcnt = write(fd, buf, count);
      if (sendcnt == -1) {
        if (errno == EINTR) {
          continue;
        }
        LOG4CXX_ERROR(log, "Package write error.");
        return -1;
      } else if (sendcnt == 0) {
        LOG4CXX_INFO(log, "Connection disconnected.");
        return 0;
      }

      count -= sendcnt;
      buf += sendcnt;
    }
    return ret;
  }
  
  static void *thread_handler(void *data) {
    auto *worker_data = (ThreadData *) data;
    auto *buffer = new unsigned char[2 * 1024 * 1024];
    while (!g_program_shutdown) {
      Header header{};
      if (read_loop(worker_data->client_socket, (unsigned char *) &header, sizeof(header), logger) <= 0) {
        break;
      }
      switch (header.msg_type) {
        case SET:
          if (read_loop(worker_data->client_socket, buffer, header.page_size, logger) <= 0) {
            break;
          }
          worker_data->bufferpool->write_page(header.page_no, header.page_size, buffer, worker_data->thread_index);
          if (write_loop(worker_data->client_socket,
                         (unsigned char *) &header.page_size,
                         sizeof(header.page_size),
                         logger) <= 0) {
          }
          break;
        case GET:worker_data->bufferpool->read_page(header.page_no, header.page_size, buffer, worker_data->thread_index);
          if (write_loop(worker_data->client_socket,
                         (unsigned char *) &header.page_size,
                         sizeof(header.page_size),
                         logger) <= 0) {
          }
          if (write_loop(worker_data->client_socket, buffer, header.page_size, logger) <= 0) {
          }
          break;
        default: LOG4CXX_ERROR(logger, "Invalid msg type");
      }
    }
    delete[] buffer;
    LOG4CXX_DEBUG(logger, "Thread exit: " << std::this_thread::get_id());
    worker_data->bufferpool->show_hit_rate();
    return NULL;
  }
};

void signal_handler(int signum) {
  LOG4CXX_INFO(logger, "Got signal SIGINT.");
  g_program_shutdown = true;
  shutdown(server_socket, SHUT_RDWR);
}

int main(int argc, char *argv[]) {
  log4cxx::PropertyConfigurator::configureAndWatch("log4cxx.properties");
  if (argc != 7) {
    LOG4CXX_ERROR(logger, "usage: " << argv[0] << " /path/to/datafile /tmp/sockfile.sock 8k 16k 32k 2m");
    return -1;
  }
  signal(SIGINT, signal_handler);
  std::string socket_file = std::string(argv[2]);
  LOG4CXX_INFO(logger, "Server will listen at file " + socket_file);

  map<size_t, size_t> page_no_info;
  array<size_t, 4> page_sizes = {8 * 1024, 16 * 1024, 32 * 1024, 2 * 1024 * 1024};
  for (int i = 0; i < page_sizes.size() && i + 3 < argc; i++) {
    page_no_info.insert({page_sizes[i], stoi(argv[3 + i])});
  }
  auto *bp = new SimpleBufferPool(argv[1], page_no_info);
  LOG4CXX_DEBUG(logger, "Start initializing...");

  Server server(bp, socket_file.c_str());
  server.create_socket();
  server.listen_forever();

  LOG4CXX_DEBUG(logger, "Start deinitializing...");
  delete bp;
  return 0;
}
