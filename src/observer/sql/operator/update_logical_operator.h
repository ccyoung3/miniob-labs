#pragma once

#include "sql/operator/logical_operator.h"
#include <string>

using namespace std;

class Table;

class UpdateLogicalOperator : public LogicalOperator
{
public:
  UpdateLogicalOperator(Table *table, const string &attribute_name, unique_ptr<Expression> update_expr);
  virtual ~UpdateLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::UPDATE; }
  OpType              get_op_type() const override { return OpType::LOGICALUPDATE; }
  Table              *table() const { return table_; }
  const string       &attribute_name() const { return attribute_name_; }
  Expression         *update_expr() const { return update_expr_.get(); }

private:
  Table *table_ = nullptr;
  string attribute_name_;
  unique_ptr<Expression> update_expr_;
};
