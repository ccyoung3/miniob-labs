/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/include/ob_lsm_transaction.h"
#include "oblsm/util/ob_comparator.h"
#include "common/lang/memory.h"

namespace oceanbase {

/**
 * @brief merge TrxInnerMapIterator and ObUserIterator
 * @details Merges two iterators of different types into one.
 * If the two iterators have the same key, only
 * produce the key once and prefer the entry from left.
 */
class TrxIterator : public ObLsmIterator
{
public:
  TrxIterator(ObLsmIterator *left, ObLsmIterator *right) : left_(left), right_(right) {
    seek_to_first();
  }
  ~TrxIterator() override = default;

  bool valid() const override {
    if (valid_left()) return true;
    if (valid_right()) return true;
    return false;
  }

  void seek_to_first() override {
    left_->seek_to_first();
    right_->seek_to_first();
    advance();
  }

  void seek_to_last() override {
    left_->seek_to_last();
    right_->seek_to_last();
    // Implementation of reverse iteration could be tricky, skip for now.
  }

  void seek(const string_view &key) override {
    left_->seek(key);
    right_->seek(key);
    advance();
  }

  void next() override {
    if (!valid()) return;
    
    if (valid_left() && (!valid_right() || left_->key().compare(right_->key()) < 0)) {
      left_->next();
    } else if (valid_right() && (!valid_left() || right_->key().compare(left_->key()) < 0)) {
      right_->next();
    } else if (valid_left() && valid_right() && left_->key().compare(right_->key()) == 0) {
      left_->next();
      right_->next();
    }
    advance();
  }

  string_view key() const override {
    if (valid_left() && (!valid_right() || left_->key().compare(right_->key()) <= 0)) {
      return left_->key();
    }
    return right_->key();
  }

  string_view value() const override {
    if (valid_left() && (!valid_right() || left_->key().compare(right_->key()) <= 0)) {
      return left_->value();
    }
    return right_->value();
  }

private:
  void advance() {
    // Skip empty values (tombstones)
    while (valid()) {
      if (valid_left() && (!valid_right() || left_->key().compare(right_->key()) < 0)) {
        if (left_->value().empty()) left_->next();
        else break;
      } else if (valid_right() && (!valid_left() || right_->key().compare(left_->key()) < 0)) {
        break;
      } else if (valid_left() && valid_right() && left_->key().compare(right_->key()) == 0) {
        if (left_->value().empty()) {
          left_->next();
          right_->next();
        } else {
          break;
        }
      }
    }
  }

  bool valid_left() const { return left_->valid(); }
  bool valid_right() const { return right_->valid(); }

  unique_ptr<ObLsmIterator> left_;
  unique_ptr<ObLsmIterator> right_;
};

ObLsmTransaction::ObLsmTransaction(ObLsm *db, uint64_t ts) : db_(db), ts_(ts)
{
  (void)db_;
  (void)ts_;
}

RC ObLsmTransaction::get(const string_view &key, string *value) {
  auto it = inner_store_.find(std::string(key));
  if (it != inner_store_.end()) {
    if (it->second.empty()) { // We use empty string as tombstone
      return RC::NOTFOUND;
    }
    *value = it->second;
    return RC::SUCCESS;
  }
  return db_->get(key, value);
}

RC ObLsmTransaction::put(const string_view &key, const string_view &value) {
  inner_store_[std::string(key)] = std::string(value);
  return RC::SUCCESS;
}

RC ObLsmTransaction::remove(const string_view &key) {
  inner_store_[std::string(key)] = ""; // Tombstone
  return RC::SUCCESS;
}

ObLsmIterator *ObLsmTransaction::new_iterator(ObLsmReadOptions options) {
  options.seq = ts_; // Always use the transaction's snapshot
  auto right_it = db_->new_iterator(options);
  auto left_it = new TrxInnerMapIterator(inner_store_);
  return new TrxIterator(left_it, right_it);
}

RC ObLsmTransaction::commit() {
  if (inner_store_.empty()) {
    return RC::SUCCESS;
  }
  std::vector<std::pair<std::string, std::string>> batch;
  for (const auto &kv : inner_store_) {
    batch.push_back(kv);
  }
  RC rc = db_->batch_put(batch);
  if (rc == RC::SUCCESS) {
    inner_store_.clear();
  }
  return rc;
}

RC ObLsmTransaction::rollback() {
  inner_store_.clear();
  return RC::SUCCESS;
}

}  // namespace oceanbase
