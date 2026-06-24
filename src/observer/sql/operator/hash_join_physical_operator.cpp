/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/hash_join_physical_operator.h"
#include "common/log/log.h"
#include "storage/trx/trx.h"
#include "sql/expr/tuple.h"
#include "sql/expr/expression.h"
#include "common/value.h"
#include <functional>

HashJoinPhysicalOperator::HashJoinPhysicalOperator() {}

static int64_t compute_hash(const Value &val) {
  if (val.attr_type() == AttrType::INTS) {
    return std::hash<int>()(val.get_int());
  } else if (val.attr_type() == AttrType::FLOATS) {
    return std::hash<float>()(val.get_float());
  } else if (val.attr_type() == AttrType::CHARS) {
    return std::hash<std::string>()(val.get_string());
  }
  return 0;
}

RC HashJoinPhysicalOperator::open(Trx *trx)
{
  trx_ = trx;
  if (children_.size() != 2) {
    LOG_WARN("HashJoinPhysicalOperator should have 2 children");
    return RC::INTERNAL;
  }

  left_ = children_[0].get();
  right_ = children_[1].get();

  RC rc = left_->open(trx);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  rc = right_->open(trx);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  rc = build_hash_table();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  return RC::SUCCESS;
}

RC HashJoinPhysicalOperator::build_hash_table()
{
  RC rc = RC::SUCCESS;
  while ((rc = left_->next()) == RC::SUCCESS) {
    Tuple *left_tuple = left_->current_tuple();
    if (!left_tuple) {
      continue;
    }

    if (join_predicates_.empty()) {
      return RC::INTERNAL;
    }

    int64_t hash_value = 0;
    bool has_hash = false;

    for (auto &pred : join_predicates_) {
      if (pred->type() == ExprType::COMPARISON) {
        auto comp_expr = static_cast<ComparisonExpr *>(pred.get());
        if (comp_expr->comp() == CompOp::EQUAL_TO) {
          Value val;
          rc = comp_expr->left()->get_value(*left_tuple, val);
          if (rc == RC::SUCCESS) {
            hash_value ^= compute_hash(val);
            has_hash = true;
          } else {
            rc = comp_expr->right()->get_value(*left_tuple, val);
            if (rc == RC::SUCCESS) {
              hash_value ^= compute_hash(val);
              has_hash = true;
            }
          }
        }
      }
    }

    if (!has_hash) {
      hash_value = 0;
    }

    ValueListTuple *stored_tuple = new ValueListTuple;
    ValueListTuple::make(*left_tuple, *stored_tuple);

    left_tuples_stored_.push_back(stored_tuple);
    hash_table_[hash_value].push_back(stored_tuple);
  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  return rc;
}

RC HashJoinPhysicalOperator::next()
{
  RC rc = RC::SUCCESS;

  while (true) {
    if (current_matches_ && current_match_idx_ < current_matches_->size()) {
      Tuple *left_tuple = (*current_matches_)[current_match_idx_++];
      joined_tuple_.set_left(left_tuple);
      joined_tuple_.set_right(right_tuple_);

      bool condition_matched = true;
      for (auto &pred : join_predicates_) {
        Value val;
        if (pred->get_value(joined_tuple_, val) == RC::SUCCESS) {
          if (!val.get_boolean()) {
            condition_matched = false;
            break;
          }
        } else {
          condition_matched = false;
          break;
        }
      }

      if (condition_matched) {
        return RC::SUCCESS;
      }
      continue;
    }

    rc = right_->next();
    if (rc != RC::SUCCESS) {
      return rc;
    }

    right_tuple_ = right_->current_tuple();
    if (!right_tuple_) {
      continue;
    }

    int64_t hash_value = 0;
    bool has_hash = false;

    for (auto &pred : join_predicates_) {
      if (pred->type() == ExprType::COMPARISON) {
        auto comp_expr = static_cast<ComparisonExpr *>(pred.get());
        if (comp_expr->comp() == CompOp::EQUAL_TO) {
          Value val;
          rc = comp_expr->right()->get_value(*right_tuple_, val);
          if (rc == RC::SUCCESS) {
            hash_value ^= compute_hash(val);
            has_hash = true;
          } else {
            rc = comp_expr->left()->get_value(*right_tuple_, val);
            if (rc == RC::SUCCESS) {
              hash_value ^= compute_hash(val);
              has_hash = true;
            }
          }
        }
      }
    }

    if (!has_hash) {
      hash_value = 0;
    }

    auto it = hash_table_.find(hash_value);
    if (it != hash_table_.end()) {
      current_matches_ = &it->second;
      current_match_idx_ = 0;
    } else {
      current_matches_ = nullptr;
    }
  }

  return RC::RECORD_EOF;
}

RC HashJoinPhysicalOperator::close()
{
  left_->close();
  right_->close();
  
  for (Tuple *t : left_tuples_stored_) {
    delete t;
  }
  left_tuples_stored_.clear();
  hash_table_.clear();

  return RC::SUCCESS;
}

Tuple *HashJoinPhysicalOperator::current_tuple()
{
  return &joined_tuple_;
}
