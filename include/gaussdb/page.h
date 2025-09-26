#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <functional>
#include <string>

namespace gaussdb::buffer
{

    using page_id_t = uint32_t; // 页编号类型
    using byte = uint8_t;       // 单字节类型，用于数据缓冲区

    /**
     * @brief Page 类：表示一个内存页的数据结构
     *
     * 特性：
     *  - 每个 Page 对应唯一的页号 (page_id) 和固定的页大小。
     *  - 持有实际数据缓冲区（data_），由智能指针管理生命周期。
     *  - 提供线程安全的读写接口：多线程可并发读，写操作互斥。
     *  - 维护 pin_count（页面被使用的引用计数），供缓存淘汰算法判断是否可驱逐。
     *  - 维护 dirty / loaded 状态，用于区分是否需要写回磁盘。
     *  - 可选传入 flush 回调，用于刷盘策略（也可直接调用 flush_to_fd）。
     *
     * 线程安全说明：
     *  - pin()/unpin() 使用原子操作，可在多线程下安全调用。
     *  - ReadAt() 使用 shared_lock 共享读锁，可并行读取。
     *  - WriteAt()/load_from_fd() 使用 unique_lock 独占锁，保证写入一致性。
     *  - flush_to_fd() 在持有读锁时复制数据，避免长时间阻塞读者。
     */
    class Page : public std::enable_shared_from_this<Page>
    {
    public:
        /// 定义刷盘回调类型：参数为当前 Page，返回 true 表示刷盘成功
        using FlushCallback = std::function<bool(const Page &)>;

        /**
         * @brief 构造函数
         * @param id 页编号
         * @param page_size 页大小（字节数）
         * @param flush_cb 可选刷盘回调（供 BufferPool 注入）
         */
        Page(page_id_t id, size_t page_size, FlushCallback flush_cb = nullptr);
        ~Page();

        // 禁止拷贝，避免无意复制大块内存
        Page(const Page &) = delete;
        Page &operator=(const Page &) = delete;

        // 基本访问接口
        page_id_t id() const noexcept { return page_id_; }
        size_t size() const noexcept { return page_size_; }
        byte *data() noexcept { return data_.get(); }
        const byte *data() const noexcept { return data_.get(); }

        // ======================
        // 引用计数 (pin/unpin)
        // ======================

        /**
         * @brief 增加 pin_count，表示该页正在被使用（不可驱逐）
         */
        void pin() noexcept;

        /**
         * @brief 减少 pin_count
         * @return 减少后的计数值（>= 0）
         */
        int unpin() noexcept;

        /**
         * @brief 获取当前 pin_count
         */
        int pin_count() const noexcept { return pin_count_.load(std::memory_order_acquire); }

        // ======================
        // 数据读写接口
        // ======================

        /**
         * @brief 读取页面数据
         * @param offset 起始偏移（字节）
         * @param out 目标缓冲区指针
         * @param len 读取长度
         * @return 实际读取字节数（可能 < len）
         * @note 多线程可并发调用
         */
        size_t ReadAt(size_t offset, void *out, size_t len) const;

        /**
         * @brief 写入页面数据
         * @param offset 起始偏移
         * @param buf 源数据指针
         * @param len 写入长度
         * @return 实际写入字节数
         * @note 写入后会标记页面为 dirty
         */
        size_t WriteAt(size_t offset, const void *buf, size_t len);

        // ======================
        // I/O 接口
        // ======================

        /**
         * @brief 从文件加载页面数据
         * @param fd 文件描述符
         * @param file_offset 页在文件中的字节偏移
         * @return true 表示读取成功（即使读到 EOF 也会填充 0）
         */
        bool load_from_fd(int fd, off_t file_offset);

        /**
         * @brief 将页面内容写回文件
         * @param fd 文件描述符
         * @param file_offset 页在文件中的字节偏移
         * @return true 表示写入成功
         */
        bool flush_to_fd(int fd, off_t file_offset);

        /**
         * @brief 使用回调刷盘
         * @return true 表示刷盘成功
         */
        bool flush_with_callback();

        // ======================
        // 元数据访问
        // ======================
        bool is_dirty() const noexcept { return dirty_.load(std::memory_order_acquire); }
        bool is_loaded() const noexcept { return loaded_.load(std::memory_order_acquire); }
        void mark_dirty() noexcept { dirty_.store(true, std::memory_order_release); }
        void clear_dirty() noexcept { dirty_.store(false, std::memory_order_release); }

        void set_lsn(uint64_t lsn) noexcept { lsn_ = lsn; }
        uint64_t lsn() const noexcept { return lsn_; }

        /// 生成调试用字符串
        std::string debug_string() const;

        // ======================
        // RAII Pin 守卫
        // ======================

        /**
         * @brief PinGuard: 构造时自动 pin 页面，析构时自动 unpin
         * 适用于函数内临时保护页面不被驱逐
         */
        struct PinGuard
        {
            explicit PinGuard(std::shared_ptr<Page> p) : page(std::move(p))
            {
                if (page)
                    page->pin();
            }
            ~PinGuard()
            {
                if (page)
                    page->unpin();
            }
            // 禁止拷贝
            PinGuard(const PinGuard &) = delete;
            PinGuard &operator=(const PinGuard &) = delete;
            PinGuard(PinGuard &&) = default;
            PinGuard &operator=(PinGuard &&) = default;

            std::shared_ptr<Page> page;
        };

        /// 获取内部共享锁对象（高级用法：可在外部手动加锁）
        std::shared_mutex &latch() const noexcept { return latch_; }

    private:
        page_id_t page_id_;
        size_t page_size_;
        std::unique_ptr<byte[]> data_; ///< 实际页面数据缓冲区

        // 元数据
        std::atomic<int> pin_count_{0}; ///< 当前 pin 次数
        std::atomic<bool> dirty_{false};
        std::atomic<bool> loaded_{false};
        uint64_t lsn_{0}; ///< 可选的日志序号（恢复用）

        // 读写锁：允许多读单写
        mutable std::shared_mutex latch_;

        // 可选刷盘回调
        FlushCallback flush_cb_;
    };

} // namespace gaussdb::buffer
