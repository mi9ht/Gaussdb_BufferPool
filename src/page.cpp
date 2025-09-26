#include "gaussdb/page.h"

#include <unistd.h> // pread/pwrite
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cerrno>

namespace gaussdb::buffer
{

    Page::Page(page_id_t id, size_t page_size, FlushCallback flush_cb)
        : page_id_(id),
          page_size_(page_size),
          data_(new byte[page_size]()), // 初始化分配页缓冲区并清零
          pin_count_(0),
          dirty_(false),
          loaded_(false),
          lsn_(0),
          flush_cb_(std::move(flush_cb))
    {
        // 构造后 loaded_ = false：表示尚未从磁盘加载
    }

    Page::~Page()
    {
        // 不自动 flush，由 BufferPool 控制刷盘策略
    }

    // ======================
    // pin/unpin 引用计数
    // ======================

    void Page::pin() noexcept
    {
        pin_count_.fetch_add(1, std::memory_order_acq_rel);
    }

    int Page::unpin() noexcept
    {
        int prev = pin_count_.fetch_sub(1, std::memory_order_acq_rel);
        if (prev <= 0)
        {
            // 防御性措施：防止 pin_count 出现负数
            pin_count_.store(0);
            return 0;
        }
        return prev - 1;
    }

    // ======================
    // 数据读写
    // ======================

    size_t Page::ReadAt(size_t offset, void *out, size_t len) const
    {
        if (!out)
            throw std::invalid_argument("out pointer is null");
        if (offset >= page_size_)
            return 0;

        // 共享锁，允许并发读取
        std::shared_lock lock(latch_);
        if (!loaded_)
            return 0; // 未加载则返回 0
        size_t to_read = std::min(len, page_size_ - offset);
        std::memcpy(out, data_.get() + offset, to_read);
        return to_read;
    }

    size_t Page::WriteAt(size_t offset, const void *buf, size_t len)
    {
        if (!buf)
            throw std::invalid_argument("buf pointer is null");
        if (offset >= page_size_)
            return 0;

        // 独占锁，避免写写/读写冲突
        std::unique_lock lock(latch_);
        loaded_.store(true, std::memory_order_release); // 写入后视为已加载
        size_t to_write = std::min(len, page_size_ - offset);
        std::memcpy(data_.get() + offset, buf, to_write);
        dirty_.store(true, std::memory_order_release);
        return to_write;
    }

    // ======================
    // I/O 操作
    // ======================

    bool Page::load_from_fd(int fd, off_t file_offset)
    {
        std::unique_lock lock(latch_);
        if (!data_)
            data_.reset(new byte[page_size_]());
        size_t total = 0;
        while (total < page_size_)
        {
            ssize_t r = pread(fd, data_.get() + total, page_size_ - total, file_offset + static_cast<off_t>(total));
            if (r == -1)
            {
                if (errno == EINTR)
                    continue; // 重试
                return false; // 读取失败
            }
            if (r == 0)
            {
                // EOF：填充剩余部分为 0
                std::memset(data_.get() + total, 0, page_size_ - total);
                total = page_size_;
                break;
            }
            total += static_cast<size_t>(r);
        }
        loaded_.store(true, std::memory_order_release);
        dirty_.store(false, std::memory_order_release); // 从磁盘加载的页默认不脏
        return true;
    }

    bool Page::flush_to_fd(int fd, off_t file_offset)
    {
        // 使用共享锁复制数据，避免长时间独占阻塞读者
        std::shared_lock readlock(latch_);
        if (!loaded_)
            return false;
        if (!dirty_)
            return true;

        std::unique_ptr<byte[]> tmp(new byte[page_size_]);
        std::memcpy(tmp.get(), data_.get(), page_size_);
        readlock.unlock();

        size_t total = 0;
        while (total < page_size_)
        {
            ssize_t w = pwrite(fd, tmp.get() + total, page_size_ - total, file_offset + static_cast<off_t>(total));
            if (w == -1)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            total += static_cast<size_t>(w);
        }
        dirty_.store(false, std::memory_order_release);
        return true;
    }

    bool Page::flush_with_callback()
    {
        std::shared_lock lock(latch_);
        if (!flush_cb_)
            return false;
        bool rc = flush_cb_(*this);
        if (rc)
            dirty_.store(false, std::memory_order_release);
        return rc;
    }

    // ======================
    // 调试信息
    // ======================

    std::string Page::debug_string() const
    {
        std::ostringstream oss;
        oss << "Page{id=" << page_id_ << ", size=" << page_size_
            << ", pin=" << pin_count_.load()
            << ", dirty=" << (is_dirty() ? "y" : "n")
            << ", loaded=" << (is_loaded() ? "y" : "n")
            << ", lsn=" << lsn_ << "}";
        return oss.str();
    }

} // namespace gaussdb::buffer
