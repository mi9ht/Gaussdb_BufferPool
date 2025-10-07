#include "gaussdb/lru_buffer_pool.h"
#include <iostream>
#include <cstring>
#include <sys/stat.h>

namespace gaussdb::buffer
{

    LRUBufferPool::LRUBufferPool(std::string file_name, const std::map<size_t, size_t> &page_no_info)
        : BufferPool(std::move(file_name), page_no_info)
    {

        // 取第一个配置项作为 page_size
        if (!page_no_info_.empty())
        {
            page_size_ = page_no_info_.begin()->first;
            capacity_ = page_no_info_.begin()->second;
        }

        // 打开文件
        fd_ = ::open(file_name_.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd_ < 0)
        {
            throw std::runtime_error("Failed to open file: " + file_name_);
        }

        std::cout << "[LRUBufferPool] Initialized with capacity=" << capacity_
                  << " pages, page_size=" << page_size_ << " bytes." << std::endl;
    }

    LRUBufferPool::~LRUBufferPool()
    {
        FlushAll();
        if (fd_ >= 0)
            ::close(fd_);
    }

    void LRUBufferPool::read_page(pageno no, unsigned int page_size, void *buf, int /*t_idx*/)
    {
        auto page = GetPage(no, page_size);
        if (!page)
        {
            std::cerr << "[LRU] Failed to get page " << no << std::endl;
            return;
        }

        Page::PinGuard guard(page);
        page->ReadAt(0, buf, page_size);
    }

    void LRUBufferPool::write_page(pageno no, unsigned int page_size, void *buf, int /*t_idx*/)
    {
        auto page = GetPage(no, page_size);
        if (!page)
        {
            std::cerr << "[LRU] Failed to get page " << no << std::endl;
            return;
        }

        Page::PinGuard guard(page);
        page->WriteAt(0, buf, page_size);
    }

    void LRUBufferPool::show_hit_rate()
    {
        size_t hit = hit_count_.load();
        size_t miss = miss_count_.load();
        double rate = (hit + miss == 0) ? 0.0 : (100.0 * hit / (hit + miss));
        std::cout << "[LRUBufferPool] Hit rate: " << rate << "% (" << hit << " / " << (hit + miss) << ")\n";
    }

    // =================== 内部函数 ===================

    std::shared_ptr<Page> LRUBufferPool::GetPage(pageno no, unsigned int page_size)
    {
        std::lock_guard<std::mutex> guard(latch_);

        auto it = page_table_.find(no);
        if (it != page_table_.end())
        {
            // 缓存命中
            hit_count_.fetch_add(1);
            MoveToFront(no);
            return it->second;
        }

        // 未命中 -> 加载并插入
        miss_count_.fetch_add(1);
        EvictIfNeeded();

        auto page = LoadPageFromDisk(no, page_size);
        if (!page)
            return nullptr;

        page_table_[no] = page;
        lru_list_.push_front(no);
        return page;
    }

    std::shared_ptr<Page> LRUBufferPool::LoadPageFromDisk(pageno no, unsigned int page_size)
    {
        auto page = std::make_shared<Page>(no, page_size);
        off_t offset = static_cast<off_t>(no) * static_cast<off_t>(page_size);
        if (!page->load_from_fd(fd_, offset))
        {
            // 如果页不存在，初始化为全零页
            std::memset(page->data(), 0, page_size);
        }
        return page;
    }

    void LRUBufferPool::EvictIfNeeded()
    {
        if (page_table_.size() < capacity_)
            return;

        // 从尾部开始找可驱逐页
        for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it)
        {
            auto pid = *it;
            auto page = page_table_[pid];
            if (page->pin_count() == 0)
            {
                FlushPage(page);
                page_table_.erase(pid);
                lru_list_.erase(std::next(it).base());
                return;
            }
        }

        std::cerr << "[LRU] Warning: all pages pinned, cannot evict!" << std::endl;
    }

    void LRUBufferPool::MoveToFront(pageno no)
    {
        lru_list_.remove(no);
        lru_list_.push_front(no);
    }

    bool LRUBufferPool::FlushPage(std::shared_ptr<Page> page)
    {
        if (!page->is_dirty())
            return true;
        off_t offset = static_cast<off_t>(page->id()) * static_cast<off_t>(page_size_);
        return page->flush_to_fd(fd_, offset);
    }

    void LRUBufferPool::FlushAll()
    {
        std::lock_guard<std::mutex> guard(latch_);
        for (auto &[pid, page] : page_table_)
        {
            FlushPage(page);
        }
    }

} // namespace gaussdb::buffer
