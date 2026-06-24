#pragma once

#include "sql/operator/physical_operator.h"
#include "sql/parser/parse_defs.h"
#include <vector>

class SortPhysicalOperator : public PhysicalOperator
{
public:
  SortPhysicalOperator(std::vector<OrderBySqlNode> &&order_bys);
  virtual ~SortPhysicalOperator() = default;

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;
  RC     tuple_schema(TupleSchema &schema) const override;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::SORT; }
  OpType get_op_type() const override { return OpType::ORDERBY; }

private:
  int compare_tuple(Tuple *a, Tuple *b);

private:
  std::vector<OrderBySqlNode> order_bys_;
  std::vector<Tuple *>        tuples_;
  size_t                      current_index_ = 0;
  TupleSchema                 schema_;
};
