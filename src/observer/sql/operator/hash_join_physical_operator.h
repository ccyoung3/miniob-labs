/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/operator/physical_operator.h"
#include "sql/parser/parse.h"
#include "sql/expr/expression.h"
#include <unordered_map>
#include <vector>
#include <memory>

/**
 * @brief Hash Join 算子
 * @ingroup PhysicalOperator
 */
class HashJoinPhysicalOperator : public PhysicalOperator
{
public:
  HashJoinPhysicalOperator();
  virtual ~HashJoinPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::HASH_JOIN; }
  OpType get_op_type() const override { return OpType::INNERHASHJOIN; }

  void add_join_predicate(std::unique_ptr<Expression> &&predicate) {
    join_predicates_.push_back(std::move(predicate));
  }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;
  Tuple *current_tuple() override;

private:
  RC build_hash_table();
  RC right_next();

private:
  Trx *trx_ = nullptr;
  PhysicalOperator *left_ = nullptr;
  PhysicalOperator *right_ = nullptr;

  std::vector<std::unique_ptr<Expression>> join_predicates_;
  
  std::unordered_map<int64_t, std::vector<Tuple *>> hash_table_;

  JoinedTuple joined_tuple_;
  Tuple *right_tuple_ = nullptr;

  std::vector<Tuple *> *current_matches_ = nullptr;
  size_t current_match_idx_ = 0;
  
  std::vector<Tuple *> left_tuples_stored_;
};