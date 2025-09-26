#include "gaussdb/simple_buffer_pool.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <cerrno>
#include <cstring>

using namespace std;

namespace gaussdb::buffer
{

    SimpleBufferPool::SimpleBufferPool(const std::string &file_name, const std::map<size_t, size_t> &page_no_info)
        : BufferPool(file_name, page_no_info), page_no_info_(page_no_info)
    {
        // open multiple FDs to reduce contention in multithreaded tests
        for (size_t i = 0; i < fds_.size(); ++i)
        {
            int fd = open(file_name_.c_str(), O_RDWR);
            if (fd <= 0)
            {
                // 失败则清理已打开的并抛异常
                for (size_t j = 0; j < i; ++j)
                {
                    if (fds_[j] > 0)
                        close(fds_[j]);
                }
                throw std::runtime_error(std::string("Cannot open source file: ") + file_name_ + " errno=" + to_string(errno));
            }
            fds_[i] = fd;
        }
    }

    SimpleBufferPool::~SimpleBufferPool()
    {
        for (int fd : fds_)
        {
            if (fd > 0)
                close(fd);
        }
    }

    size_t SimpleBufferPool::page_start_offset(pageno no)
    {
        size_t boundary = 0;
        size_t n = no;
        for (auto &range : page_no_info_)
        {
            size_t psize = range.first;
            size_t pcount = range.second;
            if (n >= pcount)
            {
                boundary += psize * pcount;
                n -= pcount;
            }
            else
            {
                return boundary + (n * psize);
            }
        }
        return static_cast<size_t>(-1);
    }

    void SimpleBufferPool::read_page(pageno no, unsigned int page_size, void *buf, int t_idx)
    {
        size_t offset = page_start_offset(no);
        if (offset == static_cast<size_t>(-1))
        {
            cerr << "[SimpleBufferPool] read_page: page no out of range: " << no << "\n";
            return;
        }
        int fd = fds_[t_idx % static_cast<int>(fds_.size())];
        if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) == -1)
        {
            cerr << "[SimpleBufferPool] lseek failed: " << strerror(errno) << "\n";
            return;
        }
        ssize_t r = ::read(fd, buf, page_size);
        if (r != static_cast<ssize_t>(page_size))
        {
            cerr << "[SimpleBufferPool] read size mismatch: read=" << r << " expect=" << page_size << " errno=" << strerror(errno) << "\n";
        }
        assert(r == static_cast<ssize_t>(page_size));
    }

    void SimpleBufferPool::write_page(pageno no, unsigned int page_size, void *buf, int t_idx)
    {
        size_t offset = page_start_offset(no);
        if (offset == static_cast<size_t>(-1))
        {
            cerr << "[SimpleBufferPool] write_page: page no out of range: " << no << "\n";
            return;
        }
        int fd = fds_[t_idx % static_cast<int>(fds_.size())];
        if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) == -1)
        {
            cerr << "[SimpleBufferPool] lseek failed: " << strerror(errno) << "\n";
            return;
        }
        ssize_t w = ::write(fd, buf, page_size);
        if (w != static_cast<ssize_t>(page_size))
        {
            cerr << "[SimpleBufferPool] write size mismatch: write=" << w << " expect=" << page_size << " errno=" << strerror(errno) << "\n";
        }
        assert(w == static_cast<ssize_t>(page_size));
    }

    void SimpleBufferPool::show_hit_rate()
    {
        // 待实现
        cerr << "[SimpleBufferPool] show_hit_rate(): not implemented in simple pool\n";
    }

} // namespace gaussdb::buffer
