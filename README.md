# GaussDB_BufferPool

**第七届“互联网+”大赛产业命题赛道 · 华为云 GaussDB 缓冲池项目**

本项目旨在实现一个基础但具备可扩展性的 Buffer Pool（页缓存）模块，支持 LRU / LFU / LRU-K 及其变种淘汰策略，以提升热点数据访问效率。  

## 背景与目标

- 赛题要求：  
  1. 实现基本功能的 Buffer Pool，缓存固定大小（page size = 16KB）的一些热点数据；  
  2. 使用 LRU、LFU、LRU-K 等淘汰算法（及变种），提升缓存命中率，加速热点访问。  



