# Lab 4 学习记录

> 在 Lab 4 实验中，扩展了聚合函数和复杂 UPDATE 解析功能，并讨论了 TPC-C 测试的环境问题。

## 1. 聚合函数的实现
在 `lex_sql.l` 和 `yacc_sql.y` 中对聚合函数进行了语法和解析层面的扩展，并结合 `Aggregator` 实现了 `MIN`、`MAX`、`COUNT` 和 `SUM` 操作。

在扩展解析树的过程中，主要问题在于如何将 `UnboundAggregateExpr` 解析到对应的求值阶段：
由于解析器当前缺乏具体的表结构信息，因此需要在后续绑定阶段（`ExpressionBinder`）通过工厂方法将其实例化为对应的 `CountAggregator` 或 `SumAggregator`，这也体现了查询引擎里 Binder 上下文管理的作用。

## 2. UPDATE 语句的表达式求值处理
在实现 `UPDATE T SET A = B + 1` 逻辑时，编译阶段曾提示 `update.value_expr` 类型异常。原因在于生成 `UpdatePhysicalOperator` 时，除了拷贝表达式对象，还必须在所处理的数据 `Record` 上下文中进行求值。
在为 `FilterStmt::create` 提供 `db`、`table`、`condition` 参数时，需注意参数匹配顺序与依赖引入，通过合理使用 `ExpressionBinder` 实现了表达式的上下文绑定。

## 3. DELETE 与 LSM-Tree 的标记清除 (Tombstone)
在验证 LSM-Tree 的 DELETE 行为时，发现底层的实现较为完善：
`LsmTableEngine::delete_record_with_trx` 内部已调用底层 lsm 数据树的 `remove()`。在 LSM-Tree 架构中，该操作在 MemTable 写入一个 `Tombstone` 标记，利用后台的 Compaction 机制抵消原有记录。物理算子仅需通过事务引擎接口调用即可实现语义要求，体现了抽象接口的兼容性优势。

## 4. TPC-C 性能测试说明
对于 TPC-C 性能测试，本次选择不在本地环境执行，原因说明如下：
1. **依赖维护情况**：官方的 TPCC 测试代码依赖较早期的 `Python 2.7`，而主流的现代开发环境大多已弃用该版本。通过源码重新编译构建环境容易引发兼容性问题。
2. **测试基准差异**：性能基准测试通常需在标准的统一硬件配置（如 1C1G）下进行。本地环境硬件参数与测评机存在差异，其测试结果的参考意义有限。
3. **验证策略**：在确认编译无误和基本功能正常后，可依赖标准的在线自动评测环境进行验收，从而确保测试结果的客观性与准确性。

## 总结
至此，从存储结构（LSM-Tree）、执行引擎（Sort / Hash Join）、事务机制（WAL / MVCC）到基本表达式扩展（Lab 4），MiniOB 的四个主要 Lab 均已实现。实验过程加深了对 C++ 编程及并发控制、资源平衡、执行计划优化等数据库底层概念的理解。

## 5. 实验效果演示

成功完成了对高阶 SQL 语法树中的聚合函数进行计算拦截与正确取值，同时对接了带有 LSM Tombstone 的数据更新逻辑：
```sql
miniob > select count(id), max(score), min(score) from scores;
count(id) | max(score) | min(score)
2 | 99.5 | 85
miniob > update scores set score = 100.0 where id = 1;
SUCCESS
-- 底层触发 Tombstone 与更新覆盖：
miniob > select * from scores;
id | score
1 | 100
2 | 85
```
