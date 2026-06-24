/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/table/lsm_table_engine.h"
#include "storage/record/heap_record_scanner.h"
#include "common/log/log.h"
#include "storage/index/bplus_tree_index.h"
#include "storage/common/meta_util.h"
#include "storage/db/db.h"
#include "storage/record/lsm_record_scanner.h"
#include "storage/common/codec.h"
#include "storage/trx/lsm_mvcc_trx.h"

RC LsmTableEngine::insert_record(Record &record)
{
  RC rc = RC::SUCCESS;
  // TODO: set auto increment id, and keep durability.
  // TODO: support set primary key as a part of lsm_key.
  bytes lsm_key;
  Codec::encode(table_->table_id(), inc_id_.fetch_add(1), lsm_key);
  record.set_key(string((char *)lsm_key.data(), lsm_key.size()));
  rc = lsm_->put(string_view((char *)lsm_key.data(), lsm_key.size()), string_view(record.data(), record.len()));
  return rc;
}

RC LsmTableEngine::insert_record_with_trx(Record &record, Trx *trx)
{
  if (trx == nullptr || trx->type() != TrxKit::Type::LSM) return RC::INVALID_ARGUMENT;
  auto lsm_trx = static_cast<LsmMvccTrx *>(trx)->get_trx();
  bytes lsm_key;
  Codec::encode(table_->table_id(), inc_id_.fetch_add(1), lsm_key);
  record.set_key(string((char *)lsm_key.data(), lsm_key.size()));
  return lsm_trx->put(string_view((char *)lsm_key.data(), lsm_key.size()), string_view(record.data(), record.len()));
}

RC LsmTableEngine::delete_record_with_trx(const Record &record, Trx *trx)
{
  if (trx == nullptr || trx->type() != TrxKit::Type::LSM) return RC::INVALID_ARGUMENT;
  auto lsm_trx = static_cast<LsmMvccTrx *>(trx)->get_trx();
  return lsm_trx->remove(string_view(record.key()));
}

RC LsmTableEngine::update_record_with_trx(const Record &old_record, const Record &new_record, Trx *trx)
{
  if (trx == nullptr || trx->type() != TrxKit::Type::LSM) return RC::INVALID_ARGUMENT;
  auto lsm_trx = static_cast<LsmMvccTrx *>(trx)->get_trx();
  return lsm_trx->put(string_view(old_record.key()), string_view(new_record.data(), new_record.len()));
}

RC LsmTableEngine::get_record_scanner(RecordScanner *&scanner, Trx *trx, ReadWriteMode mode)
{
  scanner = new LsmRecordScanner(table_, db_->lsm(), trx);
  RC rc = scanner->open_scan();
  if (rc != RC::SUCCESS) {
    LOG_ERROR("failed to open scanner. rc=%s", strrc(rc));
  }
  return rc;
}

RC LsmTableEngine::open()
{
  RC rc = RC::SUCCESS;
  Trx *trx = db_->trx_kit().create_trx(db_->log_handler());
  RecordScanner *scanner = nullptr;
  rc = get_record_scanner(scanner, trx, ReadWriteMode::READ_ONLY);
  if (rc != RC::SUCCESS) {
    delete trx;
    return rc;
  }

  uint64_t max_id = 0;
  Record record;
  while (scanner->next(record) == RC::SUCCESS) {
    bytes key_bytes(record.key().begin(), record.key().end());
    int64_t table_id = 0;
    uint64_t row_id = 0;
    if (Codec::decode(key_bytes, table_id, row_id) == RC::SUCCESS) {
      if (table_id == table_->table_id()) {
        if (row_id > max_id) max_id = row_id;
      }
    }
  }

  inc_id_.store(max_id + 1);

  scanner->close_scan();
  delete scanner;
  delete trx;

  return RC::SUCCESS;
}