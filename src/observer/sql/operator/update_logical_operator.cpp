#include "sql/operator/update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(Table *table, const string &attribute_name, unique_ptr<Expression> update_expr)
    : table_(table), attribute_name_(attribute_name), update_expr_(std::move(update_expr))
{}
