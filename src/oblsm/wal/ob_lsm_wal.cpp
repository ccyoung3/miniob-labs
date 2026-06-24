/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
   miniob is licensed under Mulan PSL v2.
   You can use this software according to the terms and conditions of the Mulan PSL v2.
   You may obtain a copy of Mulan PSL v2 at:
            http://license.coscl.org.cn/MulanPSL2
   THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
   EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
   MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
   See the Mulan PSL v2 for more details. */

#include "oblsm/wal/ob_lsm_wal.h"
#include "common/log/log.h"
#include "oblsm/util/ob_file_reader.h"

namespace oceanbase {
RC WAL::open(const std::string &filename)
{
  filename_ = filename;
  writer_ = std::make_unique<ObFileWriter>(filename, true);
  return writer_->open_file();
}

RC WAL::sync()
{
  if (writer_) {
    return writer_->flush();
  }
  return RC::SUCCESS;
}

RC WAL::recover(const std::string &wal_file, std::vector<WalRecord> &wal_records)
{
  ObFileReader reader(wal_file);
  RC rc = reader.open_file();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  uint32_t pos = 0;
  uint32_t file_size = reader.file_size();

  while (pos < file_size) {
    if (pos + sizeof(uint64_t) > file_size) break;
    std::string seq_str = reader.read_pos(pos, sizeof(uint64_t));
    uint64_t seq = *reinterpret_cast<const uint64_t*>(seq_str.data());
    pos += sizeof(uint64_t);

    if (pos + sizeof(size_t) > file_size) break;
    std::string key_len_str = reader.read_pos(pos, sizeof(size_t));
    size_t key_len = *reinterpret_cast<const size_t*>(key_len_str.data());
    pos += sizeof(size_t);

    if (pos + key_len > file_size) break;
    std::string key = reader.read_pos(pos, key_len);
    pos += key_len;

    if (pos + sizeof(size_t) > file_size) break;
    std::string val_len_str = reader.read_pos(pos, sizeof(size_t));
    size_t val_len = *reinterpret_cast<const size_t*>(val_len_str.data());
    pos += sizeof(size_t);

    if (pos + val_len > file_size) break;
    std::string val = reader.read_pos(pos, val_len);
    pos += val_len;

    wal_records.emplace_back(seq, std::move(key), std::move(val));
  }

  reader.close_file();
  return RC::SUCCESS;
}

RC WAL::put(uint64_t seq, string_view key, string_view val)
{
  if (!writer_) return RC::INTERNAL;

  size_t key_len = key.size();
  size_t val_len = val.size();

  RC rc = writer_->write(string_view(reinterpret_cast<const char*>(&seq), sizeof(seq)));
  if (rc != RC::SUCCESS) return rc;

  rc = writer_->write(string_view(reinterpret_cast<const char*>(&key_len), sizeof(key_len)));
  if (rc != RC::SUCCESS) return rc;

  rc = writer_->write(key);
  if (rc != RC::SUCCESS) return rc;

  rc = writer_->write(string_view(reinterpret_cast<const char*>(&val_len), sizeof(val_len)));
  if (rc != RC::SUCCESS) return rc;

  rc = writer_->write(val);
  return rc;
}
}  // namespace oceanbase