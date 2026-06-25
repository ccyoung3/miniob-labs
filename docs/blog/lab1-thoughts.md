# Lab 1 学习记录

> 声明：本笔记为《当代数据管理》大作业的个人学习记录。在完成 MiniOB 的存储引擎部分时，总结了系统级编程和并发控制上的实现细节。

## 0. 背景说明
在本 Lab 中，目标是实现并发无锁 SkipList 和 LRU 缓存，以深入理解底层存储引擎（如 OceanBase, RocksDB）在高并发场景下的处理机制。为此，实现中采用了无锁（Lock-Free）和细粒度锁的改造。

## 1. 基础数据结构 - 并发 SkipList 实现

MemTable 层的读写性能直接影响 LSM-Tree 引擎吞吐。官方框架中的 `ObSkipList` 提供了基础结构，需要补充 `find_greater_or_equal` 及支持并发无锁的 `insert_concurrently`。

为了降低锁竞争带来的开销，参考了 LevelDB 的实现，结合 `std::atomic` 的 `compare_exchange_weak` 和 `compare_exchange_strong` (CAS) 实现了无锁插入：
- **随机高度的线程安全**: 原逻辑中 `random_height()` 共享全局生成器导致并发数据竞争。通过引入 `thread_local common::RandomGenerator tls_rnd;`，规避了锁的开销，这对无锁设计起到了关键作用。
- **多级 CAS 重试**: 节点插入时前后指针易被并发修改。实现逻辑为：通过 `while(true)` 获取目标节点各层级的 `prev` 和 `succ`，首先在底部的第 0 层执行强 CAS `prev[0]->cas_next(0, succ[0], x)`。若失败说明相邻节点发生变更，则重新寻找；仅当第 0 层插入成功后，再向更高层利用 CAS 建立链接。

此部分有效锻炼了对 C++ 原子操作的细粒度控制，包括 `memory_order_relaxed`、`memory_order_release` 与 `memory_order_acquire` 的使用。

## 2. 阶段二：Block Cache 与 SSTable 构建

SkipList 为内存结构，而数据落盘面临持久化格式设计与磁盘 I/O 效率的问题。本阶段设计了基于 LRU 的 Block Cache，并完成了 SSTable 的读写链路。

### 2.1 LRU 缓存的实现
Block Cache 的核心要求是线程安全与高命中率。使用 `std::list` 与 `std::unordered_map` 实现了双向链表结合哈希表的 LRU。
需要注意的边界条件是：当容量 `capacity == 0` 时，需添加防御性校验，以防止不可预知的行为。此外，`std::list::splice` 在处理 LRU 节点移动时有效避免了内存分配与析构，降低了操作成本。

### 2.2 SSTable 解析策略
SSTable 尾部存储了定长的 trailer（记录 meta 信息的起始偏移量），由此可定位所有 Block Meta。
`ObSSTable::init()` 实现逻辑：
1. 通过 `ObFileReader` 打开文件。
2. 读取文件末尾 4 字节，获取 Meta 数组偏移。
3. 根据偏移量读取所有 `BlockMeta`，在内存中缓存并形成 Block Index。

### 2.3 数据与缓存的桥梁：read_block_with_cache
LRU Cache 与 SSTable 通过此方法联动。读取 Block 的逻辑为：
- 组装 `(sst_id, block_idx)` 作为唯一缓存 Key。
- 查询 LRU，若命中则直接返回。
- 若未命中，则触发文件 I/O (`read_block`) 并将其加入 Cache。

## 3. 阶段三：Leveled Compaction 策略与流式构建

Compaction 任务由全局全量归并改为分层（Leveled）压缩策略。

### 3.1 合并任务挑选
- **Level 0 到 Level 1**: L0 的 SSTable 文件之间可能存在重叠。当 L0 文件数量超过阈值时，需将所有 L0 文件与相关 L1 文件进行合并。
- **Level i 到 Level i+1**: L1 及以上层级文件有序且互不重叠。此时选择单个文件，并在下一层寻找存在 Key 重叠范围的文件组成合并任务，有效控制了读写放大。

### 3.2 内存限制下的流式合并 (Streaming Compaction)
为避免一次性将多个文件合并载入内存导致 OOM，对 `ObSSTableBuilder` API 进行了重构，引入了类似 RocksDB 的流式接口：`start()`, `add()`, `finish()`：
- `merge_iter` 依次迭代输入 SSTable 数据，进行归并；
- 调用 `builder->add()` 分块写入内存；
- 当 `builder->appro_size()` 达到 `options_.table_size` 限制时，调用 `finish()` 刷盘并关闭文件，随后开启新的 `builder->start()`。

该改造使得 Compaction 具备流式处理能力，内存占用稳定。

### 3.3 联调测试
在联调过程中，未实现的 WAL 模块默认报错中断流程。在不影响当前逻辑验证的前提下，将相关的 WAL 调用作 Mock 处理返回 `RC::SUCCESS`，从而使注意力集中于 LSM 的归并逻辑，最终通过了各并发和归并单测。
