// example.cpp
#include "gaussdb/lru_buffer_pool.h"
#include "gaussdb/server.h"

#include <iostream>
#include <array>
#include <map>
#include <csignal>
#include <cstdlib>

using namespace std;
using gaussdb::buffer::LRUBufferPool;
using gaussdb::buffer::BufferPool;
using gaussdb::server::Server;

static bool g_program_shutdown = false;
static int server_socket = -1;

// 信号处理：Ctrl+C 退出
void signal_handler(int signum)
{
  cerr << "[INFO] Got signal SIGINT.\n";
  g_program_shutdown = true;
  if (server_socket > 0)
  {
    shutdown(server_socket, SHUT_RDWR);
  }
}

/**
 * 服务端主程序入口
 * @param argc 参数列表
 * @param argv 程序名 数据文件路径 socket文件路径 各页大小对应的页数
 * @return
 */
int main(int argc, char *argv[])
{
  if (argc < 5)
  {
    cerr << "usage: " << argv[0]
         << " /path/to/datafile /tmp/sockfile.sock <count_for_8k> <count_for_16k> [<count_for_32k> <count_for_2m>]\n";
    return -1;
  }

  // 注册 Ctrl+C 信号
  signal(SIGINT, signal_handler);

  string datafile = argv[1];
  string socket_file = argv[2];
  cerr << "[INFO] Server will listen at file " << socket_file << "\n";

  // 构造 page_size → page_count 映射
  map<size_t, size_t> page_no_info;
  array<size_t, 4> page_sizes = {8 * 1024, 16 * 1024, 32 * 1024, 2 * 1024 * 1024};
  for (int i = 0; i < (int)page_sizes.size() && i + 3 < argc; i++)
  {
    page_no_info.insert({page_sizes[i], static_cast<size_t>(stoi(argv[3 + i]))});
  }

  // ✅ 创建 LRU 缓冲池实例
  BufferPool *bp = nullptr;
  try
  {
    bp = new LRUBufferPool(datafile, page_no_info);
    cerr << "[INFO] LRUBufferPool created successfully.\n";
  }
  catch (const std::exception &e)
  {
    cerr << "[ERROR] Failed to create LRUBufferPool: " << e.what() << "\n";
    return -1;
  }

  // 启动 Server
  Server server(bp, socket_file.c_str());
  if (server.create_socket() != 0)
  {
    delete bp;
    return -1;
  }

  // 持续监听客户端请求
  server.listen_forever();

  cerr << "[DEBUG] Deinitializing...\n";
  bp->show_hit_rate();   // ✅ 输出命中率
  delete bp;
  return 0;
}
