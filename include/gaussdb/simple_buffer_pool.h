#pragma once
#include "gaussdb/buffer_pool.h"
#include <map>
#include <string>
#include <array>

namespace gaussdb::buffer
{

    class SimpleBufferPool : public BufferPool
    {
    public:
        explicit SimpleBufferPool(const std::string &file_name, const std::map<size_t, size_t> &page_no_info);
        ~SimpleBufferPool() override;

        void read_page(pageno no, unsigned int page_size, void *buf, int t_idx) override;
        void write_page(pageno no, unsigned int page_size, void *buf, int t_idx) override;
        void show_hit_rate() override;

    private:
        size_t page_start_offset(pageno no);

        std::map<size_t, size_t> page_no_info_;
        std::array<int, 32> fds_;
    };

} // namespace gaussdb::buffer
