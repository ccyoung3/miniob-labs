/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/compaction/ob_compaction_picker.h"
#include "common/log/log.h"
#include "oblsm/util/ob_coding.h"

namespace oceanbase {

// TODO: put it in options
unique_ptr<ObCompaction> TiredCompactionPicker::pick(SSTablesPtr sstables)
{
  if (sstables->size() < options_->default_run_num) {
    return nullptr;
  }
  unique_ptr<ObCompaction> compaction(new ObCompaction(0));
  // TODO(opt): a tricky compaction picker, just pick all sstables if enough sstables.
  for (size_t i = 0; i < sstables->size(); ++i) {
    size_t tire_i_size = (*sstables)[i].size();
    for (size_t j = 0; j < tire_i_size; ++j) {
      compaction->inputs_[0].emplace_back((*sstables)[i][j]);
    }
  }
  // TODO: LOG_DEBUG for debug
  return compaction;
}

unique_ptr<ObCompaction> LeveledCompactionPicker::pick(SSTablesPtr sstables)
{
  if (sstables->empty()) return nullptr;

  // 1. Check L0
  if (sstables->at(0).size() > options_->default_l0_file_num) {
    unique_ptr<ObCompaction> compaction(new ObCompaction(0));
    const auto &l0 = sstables->at(0);
    string min_user_key, max_user_key;
    const ObComparator* comp = l0.front()->comparator();
    bool first = true;
    for (const auto &sst : l0) {
      compaction->inputs_[0].push_back(sst);
      string u_first = string(extract_user_key(sst->first_key()));
      string u_last = string(extract_user_key(sst->last_key()));
      if (first) {
        min_user_key = u_first;
        max_user_key = u_last;
        first = false;
      } else {
        if (comp->compare(u_first, min_user_key) < 0) min_user_key = u_first;
        if (comp->compare(u_last, max_user_key) > 0) max_user_key = u_last;
      }
    }
    // find overlapping in L1
    if (sstables->size() > 1) {
      for (const auto &sst : sstables->at(1)) {
        string u_first = string(extract_user_key(sst->first_key()));
        string u_last = string(extract_user_key(sst->last_key()));
        if (!(comp->compare(u_last, min_user_key) < 0 || comp->compare(u_first, max_user_key) > 0)) {
          compaction->inputs_[1].push_back(sst);
        }
      }
    }
    return compaction;
  }

  // 2. Check L1 ~ L_n
  size_t limit = options_->default_l1_level_size;
  for (size_t i = 1; i < sstables->size() - 1; ++i) {
    size_t cur_size = 0;
    for (const auto &sst : sstables->at(i)) {
      cur_size += sst->size();
    }
    if (cur_size > limit) {
      unique_ptr<ObCompaction> compaction(new ObCompaction(i));
      auto sst = sstables->at(i).front(); // Pick first file
      compaction->inputs_[0].push_back(sst);
      const ObComparator* comp = sst->comparator();
      string min_user_key = string(extract_user_key(sst->first_key()));
      string max_user_key = string(extract_user_key(sst->last_key()));

      for (const auto &sst_next : sstables->at(i+1)) {
        string u_first = string(extract_user_key(sst_next->first_key()));
        string u_last = string(extract_user_key(sst_next->last_key()));
        if (!(comp->compare(u_last, min_user_key) < 0 || comp->compare(u_first, max_user_key) > 0)) {
          compaction->inputs_[1].push_back(sst_next);
        }
      }
      return compaction;
    }
    limit *= options_->default_level_ratio;
  }

  return nullptr;
}

ObCompactionPicker *ObCompactionPicker::create(CompactionType type, ObLsmOptions *options)
{
  switch (type) {
    case CompactionType::TIRED: return new TiredCompactionPicker(options);
    case CompactionType::LEVELED: return new LeveledCompactionPicker(options);
    default: return nullptr;
  }
  return nullptr;
}

}  // namespace oceanbase