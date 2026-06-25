# Lab 3 学习记录

> 声明：本笔记为《当代数据管理》大作业的个人学习记录。在 Lab 3 的实验中，基于 MiniOB 的 LSM-Tree 存储引擎，搭建了多版本并发控制（MVCC）模型以及 Write-Ahead Logging (WAL) 持久化系统。

## 1. MVCC 快照隔离与事务合并读

### 设计逻辑
为提升并发性能，采用了 MVCC。为每个开启的事务分配递增的序列号 `seq_` 作为逻辑时间戳。
- **写操作的缓存**：事务执行期间，未提交数据暂存于由 `std::map<string, string>` 构成的 `inner_store_` 中，避免直接写入全局可见的 `MemTable`，防止了脏读。
- **读操作的可见性**：事务内部执行 `get` 查询时，优先检索 `inner_store_`，若未命中，则携带当前快照版本号查询全局存储。该策略实现了快照隔离（Snapshot Isolation）。
- **范围查询与合并读**：为实现遍历，利用归并（Merge）算法结合了 `inner_store_` 的本地更改与全局 LSM-Tree 迭代器，通过优先队列处理各类覆盖和删除标记。

### 资源管理
初期存在内存泄漏和悬空指针现象。由于 `Db` 组件抽象层级较高，并未直接提供对应的 `destroy_trx`，因为在 `open()` 阶段利用了 `trx_kit()` 创建占位事务。通过分析 `Trx` 基类的虚析构行为，最终采用了直接 `delete trx` 规范对象释放。

## 2. WAL 持久化与故障恢复 (Recovery)

### 设计逻辑
为保证 Durability，新数据写入内存前，需先记录于磁盘的 WAL 文件中（Append-Only 模式）。
- **二进制序列化协议**：设计了固定结构的数据格式：
  `[Sequence Number (uint64_t)]` + `[Key Length (size_t)]` + `[Key Bytes]` + `[Value Length (size_t)]` + `[Value Bytes]`。
- **故障恢复 (Recovery)**：服务器冷启动时，如 WAL 文件存在，则按顺序读取字节，反序列化为 `WalRecord`，随后调用 `mem_table_->put()` 重新载入内存，以重建宕机前的状态。

### 二进制 I/O 处理
在编写 WAL `recover` 阶段，因工具类 `ObFileReader` 未提供自动递增偏移量的 API，曾出现死循环或读取长度不匹配的问题。
解决方法：严格维护 `pos` 计数器，并使用 `sizeof(uint64_t)` 等明确数据量进行前移，通过 `file_size` 完善边界检查，保障了二进制流的安全解析。

## 3. 自增主键 ID 的恢复处理
服务器重启会导致内存中表引擎负责自增的主键分配器 `inc_id_` 归零，可能引发主键冲突。
针对此情况，在 `LsmTableEngine::open()` 初始化期间，通过 `RecordScanner` 遍历 LSM-Tree。同时对 `Codec::decode` 接口作了调整，使其能够解析出 `table_id` 和 `row_id`，以动态求取并恢复正确的自增键起点。

## 总结
本次实验完整串联了事务的 ACID 特性保障手段，从 WAL 重播到 MemTable，从多版本快照到合并读取，展示了数据库状态在一致性、持久化与性能之间进行权衡的实现过程。

## 4. 实验效果演示

事务引擎最直接的体现是隔离级别的保障与数据的回滚：
```sql
miniob > begin;
SUCCESS
miniob > insert into users values (3, 'Charlie');
SUCCESS
-- 事务内部可以合并读取到未提交的 Charlie：
miniob > select * from users;
id | name
1 | Alice
2 | Bob
3 | Charlie
miniob > rollback;
SUCCESS
-- Rollback 回滚后，数据恢复隔离状态：
miniob > select * from users;
id | name
1 | Alice
2 | Bob
```
