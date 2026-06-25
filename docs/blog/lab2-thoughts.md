# Lab 2 学习记录

> 在 Lab 2 实验中，打通了 SQL 查询引擎，扩展了解析语法树，并在查询优化器中增加了等值谓词的智能改写，最终实现了基于火山模型的 `HashJoinPhysicalOperator` 物理算子。

## 1. 实践记录

### 1.1 悬挂指针 (Dangling Pointers) 与元组拷贝
**现象**：在使用 `HashJoin` 时，需要在内存中将左表扫描的 Tuple 存入哈希桶 (`std::unordered_map`)。初期直接保存了 `left_tuple` 的指针，导致匹配到的左表数据异常或引发段错误 (Segfault)。
**原因**：执行引擎通常在 `next()` 调用时复用底层 Buffer，导致 `left_tuple` 指向的内存不断被新数据覆盖。
**解决**：必须进行深拷贝。通过 `ValueListTuple::make` 方法，将行内的字段值 (Value) 独立克隆并存入内存桶，从而保证数据生命周期的安全。

### 1.2 表达式求值时的 Tuple 上下文处理
**现象**：匹配 `INNER JOIN ... ON a.id = b.id` 时，等号两侧分属左右两表。而 `Expression::evaluate(tuple)` 接口仅接受单一 `Tuple` 参数。
**原因**：火山模型中，常规 `Filter` 算子具有单一输入源；而 `Join` 的谓词验证需要在联合行结构上求值。
**解决**：引入 `JoinedTuple` 作为代理视图模式，分别绑定左行与右行数据。表达式在调用 `find_cell` 取值时，若在左侧未找到相应字段，则代理自动检索右侧，解决了多数据源上下文问题。

### 1.3 Bison/Yacc 的语法歧义 (Shift/Reduce Conflicts)
**现象**：在 `yacc_sql.y` 中增加 `INNER JOIN` 规约时，容易与现有的查询 `WHERE` 子句甚至 `From` 子句引发冲突。
**原因**：在表名列表 (`rel_list`) 和 `join_condition` 之间若无明确优先级，Bison 无法确定合并策略。
**解决**：明确调整产生式层级，将 `INNER JOIN ... ON ...` 作为整体的 `RelListSqlNode` 节点，嵌套于原本的 `rel_list` 语法结构中，以此明晰 AST 生成路径。

## 2. 实验反思

### 2.1 SQL 解析流水线
通过将解析器映射至 `LogicalOperator` (逻辑算子) 的过程，清晰展现了系统化处理流：**Parser 生成 AST -> Optimizer 改写 AST -> Physical Generator 选择算法 -> 执行算子树**。通过这条流水线，声明式语言得以转化为底层控制流。

### 2.2 火山模型 (Volcano Model) 特性
执行计划基于火山模型：物理算子实现 `open()`、`next()`、`close()` 接口。
- **优点**：结构清晰，算子之间高度解耦。
- **缺点**：虚函数调用开销较大，单条记录的处理模式无法充分利用 CPU 缓存和 SIMD 指令。这解释了分析型数据库 (OLAP) 倾向于向量化执行的原因。

### 2.3 算法在底层系统中的应用
实验中应用了部分算法思想以提高执行效率：
- 使用哈希表将连接复杂度由 $O(N^2)$ 降低至 $O(M+N)$。
- 在 `PhysicalPlanGenerator` 中，通过检测 `ExprType::COMPARISON` 是否具有 `CompOp::EQUAL_TO` 的形式以决策是否采用 `HashJoin`。这属于基于规则的优化 (RBO) 的基础实践。

## 3. 实验效果演示

通过查询优化器重写并在物理层通过 `HashJoinPhysicalOperator` 运行的 `INNER JOIN` 展现了预期的多表联动效果：
```sql
miniob > create table scores (id int, score float);
SUCCESS
miniob > insert into scores values (1, 99.5);
SUCCESS
miniob > insert into scores values (2, 85.0);
SUCCESS
miniob > select users.name, scores.score from users inner join scores on users.id = scores.id;
name | score
Alice | 99.5
Bob | 85
```
