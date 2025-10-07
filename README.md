# 🧠 GaussDB BufferPool — 基于 LRU 的缓冲池实现

## 📘 项目简介
本项目为第七届中国国际“互联网+”大学生创新创业大赛产业命题赛道·华为云 GaussDB 命题项目，  
实现了一个 **基于 LRU 缓存策略的数据库缓冲池系统**，支持多线程并发访问、智能指针内存管理与命中率统计。

---

## 🧩 功能特性

| 功能模块 | 描述 |
|-----------|------|
| **Page 管理** | 使用 `std::shared_ptr<Page>` 管理页面数据，自动回收 |
| **LRU 缓存策略** | 实现最近最少使用算法，提高缓存命中率 |
| **线程安全设计** | 使用 `std::mutex` / `std::shared_mutex` 实现多读单写并发控制 |
| **脏页刷回机制** | 缓存淘汰或关闭时自动写回磁盘 |
| **命中率统计** | 记录命中次数与缺页次数，输出整体命中率 |

---

## 🏗️ 项目结构

```bash
Gaussdb_BufferPool/
├── include/
│   └── gaussdb/
│       ├── buffer_pool.h        # 抽象基类接口
│       ├── lru_buffer_pool.h    # LRU 缓冲池实现
│       ├── page.h               # 页面数据结构
│       └── server.h             # 官方服务端接口
├── src/
│   ├── lru_buffer_pool.cpp
│   ├── page.cpp
│   └── server.cpp
├── example.cpp                  # 程序主入口
├── CMakeLists.txt               # 构建脚本
└── README.md                    # 项目说明文件

