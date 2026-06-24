#include "sql/operator/sort_physical_operator.h"
#include "common/log/log.h"
#include "storage/record/record.h"
#include "sql/expr/tuple.h"
#include <algorithm>
#include <cstring>

using namespace std;

SortPhysicalOperator::SortPhysicalOperator(vector<OrderBySqlNode> &&order_bys)
    : order_bys_(std::move(order_bys))
{
}

RC SortPhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  PhysicalOperator *child = children_[0].get();
  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  rc = child->tuple_schema(schema_);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  while ((rc = child->next()) == RC::SUCCESS) {
    Tuple *tuple = child->current_tuple();
    if (tuple != nullptr) {
      ValueListTuple *value_list = new ValueListTuple();
      if (ValueListTuple::make(*tuple, *value_list) == RC::SUCCESS) {
        tuples_.push_back(value_list);
      } else {
        delete value_list;
      }
    }
  }

  if (rc != RC::RECORD_EOF) {
    return rc;
  }

  std::sort(tuples_.begin(), tuples_.end(), [this](Tuple *a, Tuple *b) {
    return this->compare_tuple(a, b) < 0;
  });

  current_index_ = 0;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::next()
{
  if (current_index_ >= tuples_.size()) {
    return RC::RECORD_EOF;
  }
  current_index_++;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::close()
{
  for (Tuple *tuple : tuples_) {
    delete tuple;
  }
  tuples_.clear();

  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

Tuple *SortPhysicalOperator::current_tuple()
{
  if (current_index_ > 0 && current_index_ <= tuples_.size()) {
    return tuples_[current_index_ - 1];
  }
  return nullptr;
}

RC SortPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  schema = schema_;
  return RC::SUCCESS;
}

int SortPhysicalOperator::compare_tuple(Tuple *a, Tuple *b)
{
  for (const OrderBySqlNode &order_by : order_bys_) {
    int index = -1;
    for (int i = 0; i < schema_.cell_num(); i++) {
      const TupleCellSpec &cell = schema_.cell_at(i);
      if (!order_by.attr.relation_name.empty()) {
        if (cell.table_name() == order_by.attr.relation_name &&
            cell.field_name() == order_by.attr.attribute_name) {
          index = i;
          break;
        }
      } else {
        if (cell.field_name() == order_by.attr.attribute_name ||
            (strlen(cell.alias()) > 0 && cell.alias() == order_by.attr.attribute_name)) {
          index = i;
          break;
        }
      }
    }
    if (index < 0) {
      LOG_WARN("Cannot find field for sorting");
      continue;
    }

    Value value_a, value_b;
    a->cell_at(index, value_a);
    b->cell_at(index, value_b);

    int cmp = value_a.compare(value_b);
    if (cmp != 0) {
      if (order_by.direction == ORDER_BY_DESC) {
        return -cmp;
      }
      return cmp;
    }
  }
  return 0;
}
