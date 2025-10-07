#pragma once
#include "gaussdb/page.h"
#include "gaussdb/buffer_pool.h"

#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>

namespace gaussdb::buffer
{

    /**
     * @brief LRUBufferPool：实现基于 LRU 的缓冲池
     *
     * 特性：
     *  - 缓存最近使用的热点页；
     *  - 当缓存容量满时，驱逐最久未使用且未被 pin 的页；
     *  - 使用 std::shared_ptr<Page> 管理内存；
     *  - 提供线程安全访问；
     *  - 统计命中率；
     *  - 使用 pread/pwrite 实现随机 I/O。
     */
    class LRUBufferPool : public BufferPool
    {
    public:
        LRUBufferPool(std::string file_name, const std::map<size_t, size_t> &page_no_info);
        ~LRUBufferPool() override;

        void read_page(pageno no, unsigned int page_size, void *buf, int t_idx) override;
        void write_page(pageno no, unsigned int page_size, void *buf, int t_idx) override;
        void show_hit_rate() override;

    private:
        std::shared_ptr<Page> GetPage(pageno no, unsigned int page_size);
        std::shared_ptr<Page> LoadPageFromDisk(pageno no, unsigned int page_size);
        void EvictIfNeeded();
        void MoveToFront(pageno no);
        bool FlushPage(std::shared_ptr<Page> page);
        void FlushAll();

    private:
        int fd_{-1};
        size_t capacity_{0};
        size_t page_size_{0};

        std::unordered_map<pageno, std::shared_ptr<Page>> page_table_;
        std::list<pageno> lru_list_;
        std::mutex latch_;

        std::atomic<size_t> hit_count_{0};
        std::atomic<size_t> miss_count_{0};
    };

} // namespace gaussdb::buffer
