#pragma once
#include <cstddef>
#include <map>
#include <string>

namespace gaussdb::buffer
{

    using pageno = unsigned int;

    /** Buffer Pool申明，请按规则实现以下接口*/
    class BufferPool
    {
    protected:
        std::string file_name_;
        /** page_size -> page_count 映射（配置），例如 {16*1024, 2048} */
        std::map<size_t, size_t> page_no_info_;

    public:
        static constexpr size_t max_buffer_pool_size = 4ul * 1024 * 1024 * 1024;

        BufferPool(std::string file_name, const std::map<size_t, size_t> &page_no_info)
            : file_name_(std::move(file_name)), page_no_info_(page_no_info) {}
            /** 初始化你的buffer pool，初始化耗时不得超过3分钟 */

        // 读取页面到 buf（子类实现）
        virtual void read_page(pageno no, unsigned int page_size, void *buf, int t_idx) = 0;

        // 将 buf 写回页面（子类实现）
        virtual void write_page(pageno no, unsigned int page_size, void *buf, int t_idx) = 0;

        // 展示命中率 / 状态（可空实现）
        virtual void show_hit_rate() = 0;

        virtual ~BufferPool() = default;
    };

} // namespace gaussdb::buffer
