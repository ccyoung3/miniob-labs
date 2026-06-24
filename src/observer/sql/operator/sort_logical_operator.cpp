#include "sql/operator/sort_logical_operator.h"

SortLogicalOperator::SortLogicalOperator(std::vector<OrderBySqlNode> &&order_bys)
    : order_bys_(std::move(order_bys))
{}
