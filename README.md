# 《当代数据管理》大作业 - 基于 MiniOB 的数据库内核演进实现

## 项目简介
本项目为《当代数据管理》课程的期末大作业代码仓库，用于记录我的学习经历。
为了深入理解现代数据库的底层机制，选取了开源教学数据库引擎 **MiniOB** 作为基础骨架，通过一系列核心组件的迭代，实现从存储引擎、查询执行到事务控制的生命周期管理。

本项目的核心目标是探索和实践数据库架构（如 LSM-Tree）中的设计思想，包含并发控制、读写放大平衡及内存管理上的工程取舍。

## 演进路线与已实现模块
整个大作业通过分阶段的实验（Labs）来逐步完善底层能力，各模块的学习记录可参考 `docs/blog/` 目录下的相关文档。

### Lab 0: C++ 基础与无锁并发初步 (已完成)
在这个lab我做了如下工作
- 实现了支持并发访问的 `ObBloomfilter` (布隆过滤器)。
- **技术亮点**: 采用无锁设计，依靠 `std::atomic` 和 CAS 操作实现并发写入与查询。

### Lab 1: LSM-Tree 存储引擎 (已完成)
包含以下特性：
- **无锁跳表 (Concurrent SkipList)**: 用于内存中的 MemTable 增删改查。
- **SSTable 与 LRU Cache**: 实现数据文件块的解析及带驱逐机制的缓存控制，降低磁盘 I/O 延迟。
- **Leveled Compaction**: 控制读写放大，实现将 L0 与更深层数据有效合并的挑选策略。

### Lab 2: 查询引擎 - 语法解析与物理算子执行 (已完成)
扩展查询引擎，实现高阶查询算子。
- **语法树与 AST 扩展**: 扩展了 `lex`/`yacc` 规则，支持 `ORDER BY` 与 `INNER JOIN ... ON`。
- **物理算子开发**: 实现了 `SortPhysicalOperator` 和 `HashJoinPhysicalOperator`。Hash Join 提升了嵌套循环连接效率至 $O(M+N)$。
- **查询优化器规则**: 实现了谓词下推，并在 `PhysicalPlanGenerator` 中集成基于代价/规则的算子选择。

### Lab 3: 事务引擎 - MVCC 与 WAL 持久化 (已完成)
扩展底层架构以支持并发事务控制与数据容灾机制：
- **快照隔离 (Snapshot Isolation) 的 MVCC 实现**: 为写入分配自增 `Sequence Number` 作为逻辑时间戳，通过 `ObLsmTransaction` 管理 WriteBatch，实现合并读与全局快照隔离。
- **WAL 持久化与宕机恢复**: 实现顺序写追加日志以保证 Durability。支持服务器冷启动时解析回放以恢复状态，并在恢复阶段动态求取最大自增键，避免冲突。

### Lab 4: 高阶语句扩展与性能分析准备 (已完成)
扩展引擎的高级特性，为性能评测打好基础：
- **聚合函数与表达式求值**: 支持 `MIN/MAX/COUNT/SUM` 聚合运算；对复杂 `UPDATE` 语句增加求值绑定。
- **UPDATE & DELETE 适配**: 对接 `LSM-Tree` 引擎的事务操作，实现更新和删除对应的 `Tombstone` 清除机制。
- **TPC-C 测试说明**: 出于评测机架构统一考量与老旧 Python2 环境依赖过重的原因，跳过本地构建，重心放置于代码逻辑实现及在线评测。

## 如何编译与运行

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

## 个人思考与心得
对于各模块的分析与技术实现总结，详见：
- [Lab 0 学习记录](./docs/blog/lab0-thoughts.md)
- [Lab 1 学习记录](./docs/blog/lab1-thoughts.md)
- [Lab 2 学习记录](./docs/blog/lab2-thoughts.md)
- [Lab 3 学习记录](./docs/blog/lab3-thoughts.md)
- [Lab 4 学习记录](./docs/blog/lab4-thoughts.md)

> **声明**: 本仓库为《当代数据管理》杨宗霖的个人学习作业记录，更新于2026年6月25日。
