#pragma once

#include "sql/operator/physical_operator.h"
#include <string>

using namespace std;

class Table;

class UpdatePhysicalOperator : public PhysicalOperator
{
public:
  UpdatePhysicalOperator(Table *table, const string &attribute_name, unique_ptr<Expression> update_expr);
  virtual ~UpdatePhysicalOperator() = default;

  OpType get_op_type() const override { return OpType::UPDATE; }
  PhysicalOperatorType type() const override { return PhysicalOperatorType::UPDATE; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return nullptr; }

private:
  Table *table_ = nullptr;
  string attribute_name_;
  unique_ptr<Expression> update_expr_;
};
