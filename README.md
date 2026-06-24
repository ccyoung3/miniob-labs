# 《当代数据管理》大作业 - 基于 MiniOB 的数据库内核演进实现

## 📖 项目简介
本项目为**《当代数据管理》**课程的期末大作业代码仓库。
作为一个研一学生，为了深入理解现代数据库的底层机制，我选取了开源教学数据库引擎 **MiniOB** 作为基础骨架，并通过一系列高难度的核心组件迭代，亲手去探索并实现一个数据库从存储引擎、查询执行到事务控制的完整生命周期。

本项目的核心目标**不仅是让系统“跑起来”，更是为了在踩坑的过程中摸索各种数据库架构（如 LSM-Tree）中的设计哲学，尤其在并发控制、读写放大平衡、内存管理上的取舍。**

## 🎯 演进路线与已实现模块
整个大作业通过分阶段的实验（Labs）来逐步完善底层能力，各模块的实现笔记和深度思考可以参考 `docs/blog/` 目录下的相关文档。

### Lab 0: C++ 基础与无锁并发初步 (已完成)
- 实现了支持并发访问的 `ObBloomfilter` (布隆过滤器)。
- **技术亮点**: 不借助任何读写锁，单纯依靠 `std::atomic` 和 CAS 操作实现无锁并发写入与查询。

### Lab 1: LSM-Tree 存储引擎 (已完成)
这是整个大作业中最具挑战也是最核心的基础存储部分。主要包含了以下特性：
- **无锁跳表 (Concurrent SkipList)**: 用于内存中的 MemTable 快速增删改查。
- **SSTable 与 LRU Cache**: 实现了数据文件块的解析及带驱逐机制的缓存控制，大幅降低磁盘 I/O 带来的延迟。
- **Leveled Compaction**: 控制读写放大，设计并实现将 L0 与更深层数据有效合并的挑选策略。

### Lab 2: 查询引擎 - 语法解析与物理算子执行 (已完成)
扩展查询引擎，实现高阶查询算子。
- **语法树与 AST 扩展**: 扩展了 `lex`/`yacc` 规则，支持了 `ORDER BY` 与 `INNER JOIN ... ON` 语法。
- **物理算子开发**: 实现了 `SortPhysicalOperator` 和 `HashJoinPhysicalOperator`。其中 Hash Join 使用哈希桶在内存中缓存左表数据，将原本 $O(N^2)$ 的嵌套循环连接效率提升至 $O(N)$。
- **查询优化器规则**: 实现了谓词下推（Predicate Pushdown），自动将 `ON` 子句中的等值条件分配给对应的 Join 算子；在 `PhysicalPlanGenerator` 中集成基于代价/规则的算子选择（智能切换 `HashJoin` 替代 `NestedLoopJoin`）。

### Lab 3: 事务引擎 - MVCC 与 WAL 持久化 (已完成)
扩展底层架构以支持并发事务控制与数据容灾机制：
- **快照隔离 (Snapshot Isolation) 的 MVCC 实现**: 为每个写入分配自增 `Sequence Number` 作为逻辑时间戳，通过 `ObLsmTransaction` 管理事务内部私有的 WriteBatch (即 `inner_store_`)。实现了基于 `TrxIterator` 的合并读逻辑，使得事务可以“读取未提交”的自身数据并与全局快照隔离。
- **WAL (Write-Ahead Logging) 持久化与宕机恢复**: 实现顺序写追加日志以保证 MemTable 的 Durability。支持服务器冷启动时，解析反序列化 WAL 记录并在内存中回放以实现完全无损的数据恢复；在恢复阶段能自动扫描已存在的底层存储计算最大自增键，避免插入冲突。

### Lab 4 (规划中)
- **性能基准测试**: 构建吞吐与延迟压测体系。

## ⚙️ 如何编译与运行

本项目使用 CMake 构建，依赖标准 C++14/17 编译器。

```bash
# 克隆代码
git clone https://github.com/ccyoung3/miniob-labs.git
cd miniob-labs

# 编译（默认 debug 模式）
bash build.sh debug --make -j4

# 运行服务端
./build_debug/bin/observer -f etc/observer.ini

# 在另一个终端运行客户端进行连接
./build_debug/bin/obclient
```

## 📝 个人思考与心得
在做这个大作业的过程中，我遇到了非常多关于并发边界条件与死锁的难题。对于每一个重要模块，我都单独写了思考总结，详见：
- [Lab0 并发布隆过滤器无锁之美](./docs/blog/lab0-thoughts.md)（已整理完毕）
- [Lab1 实验心得与踩坑笔记](./docs/blog/lab1-thoughts.md)（已整理完毕）
- [Lab2 查询引擎：优化器与 Hash Join 实现的心得与踩坑](./docs/blog/lab2-thoughts.md)（已整理完毕）
- [Lab3 事务引擎与 WAL 持久化：并发隔离与状态重建的哲学](./docs/blog/lab3-thoughts.md)（已整理完毕）

> **作者**: ccyoung3
> **身份**: 研一在读
> **声明**: 本仓库仅作为《当代数据管理》的个人学习作业记录。
