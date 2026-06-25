#include "sql/operator/update_physical_operator.h"
#include "storage/table/table.h"
#include "sql/expr/tuple.h"
#include "storage/trx/trx.h"
#include "common/log/log.h"
#include <string.h>

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, const string &attribute_name, unique_ptr<Expression> update_expr)
    : table_(table), attribute_name_(attribute_name), update_expr_(std::move(update_expr))
{}

RC UpdatePhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];
  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  const FieldMeta *field_meta = table_->table_meta().field(attribute_name_.c_str());
  if (field_meta == nullptr) {
    LOG_WARN("no such field. table=%s, field=%s", table_->name(), attribute_name_.c_str());
    return RC::SCHEMA_FIELD_MISSING;
  }

  vector<Record> records;
  vector<Value> new_values;

  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get current record: %s", strrc(rc));
      return rc;
    }

    Value new_val;
    rc = update_expr_->get_value(*tuple, new_val);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to evaluate update expression: %s", strrc(rc));
      return rc;
    }

    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record   &record    = row_tuple->record();
    records.emplace_back(std::move(record));
    new_values.push_back(new_val);
  }

  child->close();

  for (size_t i = 0; i < records.size(); ++i) {
    Record &old_record = records[i];
    Value &new_val = new_values[i];

    Record new_record = old_record;
    
    // cast to right type
    Value casted_val;
    rc = Value::cast_to(new_val, field_meta->type(), casted_val);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    // copy data
    memcpy(new_record.data() + field_meta->offset(), casted_val.data(), field_meta->len());

    rc = table_->update_record_with_trx(old_record, new_record, trx);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to update record: %s", strrc(rc));
      return rc;
    }
  }

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::next()
{
  return RC::RECORD_EOF;
}

RC UpdatePhysicalOperator::close()
{
  return RC::SUCCESS;
}
