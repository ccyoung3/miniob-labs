/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/update_stmt.h"
#include "sql/stmt/filter_stmt.h"
#include "sql/parser/expression_binder.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/parse_defs.h"
#include "common/log/log.h"
#include "session/session.h"

UpdateStmt::UpdateStmt(Table *table, FilterStmt *filter_stmt, const string &attribute_name, unique_ptr<Expression> update_expr)
    : table_(table), filter_stmt_(filter_stmt), attribute_name_(attribute_name), update_expr_(std::move(update_expr))
{}

UpdateStmt::~UpdateStmt()
{
  delete filter_stmt_;
}

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update, Stmt *&stmt)
{
  Table *table = db->find_table(update.relation_name.c_str());
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), update.relation_name.c_str());
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  unordered_map<string, Table *> table_map;
  table_map.insert({update.relation_name, table});

  FilterStmt *filter_stmt = nullptr;
  RC rc = FilterStmt::create(db, table, &table_map, update.conditions.data(), update.conditions.size(), filter_stmt);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  unique_ptr<Expression> value_expr = update.value_expr->copy();
  BinderContext binder_context;
  binder_context.add_table(table);
  ExpressionBinder expr_binder(binder_context);
  
  vector<unique_ptr<Expression>> bound_expressions;
  rc = expr_binder.bind_expression(value_expr, bound_expressions);
  if (rc != RC::SUCCESS) {
    delete filter_stmt;
    return rc;
  }
  
  value_expr = std::move(bound_expressions[0]);

  stmt = new UpdateStmt(table, filter_stmt, update.attribute_name, std::move(value_expr));
  return RC::SUCCESS;
}
