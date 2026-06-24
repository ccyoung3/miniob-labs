#pragma once

#include "sql/operator/logical_operator.h"
#include "sql/parser/parse_defs.h"

class SortLogicalOperator : public LogicalOperator
{
public:
  SortLogicalOperator(std::vector<OrderBySqlNode> &&order_bys);
  virtual ~SortLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::SORT; }
  OpType              get_op_type() const override { return OpType::LOGICALSORT; }

  const std::vector<OrderBySqlNode> &order_bys() const { return order_bys_; }

private:
  std::vector<OrderBySqlNode> order_bys_;
};
