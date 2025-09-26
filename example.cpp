// example.cpp
#include "gaussdb/simple_buffer_pool.h"
#include "gaussdb/server.h"

#include <iostream>
#include <array>
#include <map>
#include <csignal>
#include <cstdlib>

using namespace std;
using gaussdb::buffer::SimpleBufferPool;
using gaussdb::server::Server;

static bool g_program_shutdown = false;
static int server_socket = -1;

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
 * @param argc 必须保证7个参数
 * @param argv 程序名 数据库文件 socket套接字文件名，8K大小的页面数量，16K大小的页面数量，32K数量，2M数量
 * @return
 */
int main(int argc, char *argv[])
{
  if (argc < 5)
  {
    cerr << "usage: " << argv[0] << " /path/to/datafile /tmp/sockfile.sock <count_for_8k> <count_for_16k> [<count_for_32k> <count_for_2m>]\n";
    return -1;
  }
  signal(SIGINT, signal_handler);

  std::string datafile = argv[1];
  std::string socket_file = argv[2];
  cerr << "[INFO] Server will listen at file " << socket_file << "\n";

  map<size_t, size_t> page_no_info;
  array<size_t, 4> page_sizes = {8 * 1024, 16 * 1024, 32 * 1024, 2 * 1024 * 1024};
  for (int i = 0; i < (int)page_sizes.size() && i + 3 < argc; i++)
  {
    page_no_info.insert({page_sizes[i], static_cast<size_t>(stoi(argv[3 + i]))});
  }

  // 创建 BufferPool
  gaussdb::buffer::BufferPool *bp = nullptr;
  try
  {
    bp = new SimpleBufferPool(datafile, page_no_info);
  }
  catch (const std::exception &e)
  {
    cerr << "[ERROR] Failed to create BufferPool: " << e.what() << "\n";
    return -1;
  }

  Server server(bp, socket_file.c_str());
  if (server.create_socket() != 0)
  {
    delete bp;
    return -1;
  }

  server.listen_forever();

  cerr << "[DEBUG] Deinitializing...\n";
  delete bp;
  return 0;
}
