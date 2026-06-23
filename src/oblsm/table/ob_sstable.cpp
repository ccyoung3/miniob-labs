/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/table/ob_sstable.h"
#include "oblsm/util/ob_coding.h"
#include "common/log/log.h"
#include "common/lang/filesystem.h"
namespace oceanbase {

void ObSSTable::init()
{
  file_reader_ = make_unique<ObFileReader>(file_name_);
  if (file_reader_->open_file() != RC::SUCCESS) return;
  uint32_t file_size = file_reader_->file_size();
  if (file_size < sizeof(uint32_t)) return;

  string trailer_str = file_reader_->read_pos(file_size - sizeof(uint32_t), sizeof(uint32_t));
  uint32_t meta_offset = get_numeric<uint32_t>(trailer_str.data());

  if (meta_offset >= file_size) return;

  string metas_data = file_reader_->read_pos(meta_offset, file_size - meta_offset - sizeof(uint32_t));
  
  const char *ptr = metas_data.data();
  uint32_t num_metas = get_numeric<uint32_t>(ptr);
  ptr += sizeof(uint32_t);

  for (uint32_t i = 0; i < num_metas; ++i) {
    uint32_t meta_size = get_numeric<uint32_t>(ptr);
    ptr += sizeof(uint32_t);
    string meta_str(ptr, meta_size);
    ptr += meta_size;
    BlockMeta meta;
    meta.decode(meta_str);
    block_metas_.push_back(meta);
  }
}

shared_ptr<ObBlock> ObSSTable::read_block_with_cache(uint32_t block_idx) const
{
  if (block_cache_ == nullptr) {
    return read_block(block_idx);
  }
  uint64_t cache_key = ((uint64_t)sst_id_ << 32) | block_idx;
  shared_ptr<ObBlock> block;
  if (block_cache_->get(cache_key, block)) {
    return block;
  }
  block = read_block(block_idx);
  if (block != nullptr) {
    block_cache_->put(cache_key, block);
  }
  return block;
}

shared_ptr<ObBlock> ObSSTable::read_block(uint32_t block_idx) const
{
  if (block_idx >= block_metas_.size()) {
    return nullptr;
  }
  const BlockMeta &meta = block_metas_[block_idx];
  string block_data = file_reader_->read_pos(meta.offset_, meta.size_);
  auto block = make_shared<ObBlock>(comparator_);
  block->decode(block_data);
  return block;
}

void ObSSTable::remove() { filesystem::remove(file_name_); }

ObLsmIterator *ObSSTable::new_iterator() { return new TableIterator(get_shared_ptr()); }

void TableIterator::read_block_with_cache()
{
  block_ = sst_->read_block_with_cache(curr_block_idx_);
  block_iterator_.reset(block_->new_iterator());
}

void TableIterator::seek_to_first()
{
  curr_block_idx_ = 0;
  read_block_with_cache();
  block_iterator_->seek_to_first();
}

void TableIterator::seek_to_last()
{
  curr_block_idx_ = block_cnt_ - 1;
  read_block_with_cache();
  block_iterator_->seek_to_last();
}

void TableIterator::next()
{
  block_iterator_->next();
  if (block_iterator_->valid()) {
  } else if (curr_block_idx_ < block_cnt_ - 1) {
    curr_block_idx_++;
    read_block_with_cache();
    block_iterator_->seek_to_first();
  }
}

void TableIterator::seek(const string_view &lookup_key)
{
  curr_block_idx_ = 0;
  // TODO: use binary search
  for (; curr_block_idx_ < block_cnt_; curr_block_idx_++) {
    const auto &block_meta = sst_->block_meta(curr_block_idx_);
    if (sst_->comparator()->compare(extract_user_key(block_meta.last_key_), extract_user_key_from_lookup_key(lookup_key)) >= 0) {
      break;
    }
  }
  if (curr_block_idx_ == block_cnt_) {
    block_iterator_ = nullptr;
    return;
  }
  read_block_with_cache();
  block_iterator_->seek(lookup_key);
};

}  // namespace oceanbase
