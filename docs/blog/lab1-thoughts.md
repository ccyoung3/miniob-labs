# Lab1 LSM-Tree 存储引擎踩坑笔记与实验思考

> 声明：本笔记为《当代数据管理》大作业的个人学习记录。在完成 MiniOB 的存储引擎部分时，遇到了许多系统级编程和并发控制上的挑战，特此整理。

## 0. 写在前面的话
在刚开始接触这个 Lab 时，看到需要手搓并发无锁 SkipList 和 LRU 缓存，心里还是挺发虚的。毕竟平时在业务代码里，直接一个 `std::mutex` 加上标准库就完事了。但为了深入理解底层存储引擎（比如 OceanBase, RocksDB 是如何扛住高并发的），我决定严格按照要求进行无锁（Lock-Free）和细粒度锁的改造。

## 1. 基础数据结构 - 并发 SkipList 实现心得
MemTable 这一层由于高频读写，性能直接影响整个 LSM-Tree 引擎的吞吐。官方框架里的 `ObSkipList` 只是搭了个骨架，我需要补充底层的 `find_greater_or_equal` 以及支持并发无锁的 `insert_concurrently`。

起初我在思考，多线程如果同时插同样的层高，是不是直接锁整个 SkipList 就行了？但是锁的开销太大了，违背了高性能的设计初衷。后来查阅了 LevelDB 的实现，并结合 `std::atomic` 的 `compare_exchange_weak` 和 `compare_exchange_strong`（也就是 CAS）实现了真正的 Lock-Free 插入：
- **随机高度的线程安全**: 原本代码里 `random_height()` 共用了一个全局的 `rnd` 随机生成器，在并发时会导致数据竞争。我巧妙地利用了 `thread_local common::RandomGenerator tls_rnd;`，避开了锁的开销，这对于无锁设计来说是非常关键的细节。
- **多级 CAS 重试**: 最麻烦的在于节点插入时，因为前后节点的指针随时会被其他线程修改。我的实现是：先用一个 `while(true)` 获取目标节点在所有层级的 `prev` 和 `succ`，接着最底部的第 0 层做强 CAS `prev[0]->cas_next(0, succ[0], x)`，如果失败说明有其它线程抢先插入了相邻节点，直接放弃当前寻找的结果，全部从头 retry；只有第 0 层插入成功（这证明该节点已经在全局链表中立足），才逐步向更高层利用 CAS 链接。

在这个过程中，通过测试发现单测 `ob_skiplist_test` 竟然默认都是 `DISABLED_`，把它开了跑通后，看着绿色的 `[  PASSED  ]`，成就感直接拉满！

其实这层实现最有趣的地方在于 C++ 原子操作的细粒度控制（`memory_order_relaxed` 应对多层级随机高度的生成，以及 `memory_order_release`、`memory_order_acquire` 建立数据依赖）。纸上得来终觉浅，这玩意写起来比读论文要痛苦，但也过瘾。

---

## 2. 阶段二：Block Cache 与 SSTable 的构建

SkipList 虽然写起来复杂，但它毕竟是内存结构。一旦数据要落盘，我们面临的就是另一种维度的挑战：**持久化格式（Format）的设计与磁盘 I/O 效率**。这部分，我花了几个晚上的时间设计和实现基于 LRU 的 Block Cache，并把 SSTable 的读写链路跑通了。

### 2.1 LRU 缓存：锁的艺术

对于 Block Cache，核心要求是线程安全且命中率高。我用 `std::list` 加上 `std::unordered_map` 实现了一个双向链表+哈希表的经典 LRU。
遇到的一个小坑是：当容量 `capacity == 0` 时，如果直接进入 `put()` 的处理逻辑，会导致野指针或者不可预知的行为。虽然是很基础的边界条件，但在调试时让我愣了半天，加上边界防御后终于绿了。
另外在 C++ 里，`std::list::splice` 真的是处理 LRU 节点移动的神器，避免了频繁的内存分配和析构，极大地降低了时间常数。

### 2.2 SSTable 解析：从尾到头的读取策略

SSTable 的格式设计（Format Design）非常像打包文件。在 MiniOB 的设定里，SSTable 尾部放了一个定长的 trailer（存储 meta 信息的起始偏移量），通过这个偏移量能找到所有 Block Meta。

实现 `ObSSTable::init()` 时，我的思路是：
1. 用 `ObFileReader` 打开文件。
2. 读文件最后 4 个字节，拿到 Meta 数组的偏移。
3. 从那个偏移读出所有 `BlockMeta`，缓存在内存里，形成一个基于块的索引（Block Index）。

### 2.3 数据与缓存的桥梁：read_block_with_cache

在这里，LRU Cache 和 SSTable 真正发生了联动。如果需要读取某个 Block：
- 先把 `(sst_id, block_idx)` 组装成唯一的缓存 Key。
- 去 LRU 里面查。如果有，直接返回。
- 如果没有，再去触发真实的文件 I/O (`read_block`) 并且放入 Cache。

这也是现代存储引擎能够高性能响应查询的命脉。写完这部分逻辑，看着 `ob_lru_cache_test`、`ob_block_test`、`ob_table_test` 这三个大测试模块（还有十几个原本 disabled 的边界测试用例）全部通过的那一瞬间，真的很爽。接下来准备攻克最复杂的 Leveled Compaction 了！

---

## 3. Leveled Compaction 合并策略权衡
（持续更新中，待代码完成后补充具体细节）
