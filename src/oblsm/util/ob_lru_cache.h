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

#include <stdint.h>
#include <cstddef>
#include <mutex>
#include <list>
#include <unordered_map>

namespace oceanbase {

/**
 * @class ObLRUCache
 * @brief A thread-safe implementation of an LRU (Least Recently Used) cache.
 *
 * The `ObLRUCache` class provides a fixed-size cache that evicts the least recently used
 * entries when the cache exceeds its capacity. It supports thread-safe operations for
 * inserting, retrieving, and checking the existence of cache entries.
 *
 * @tparam KeyType The type of keys used to identify cache entries.
 * @tparam ValueType The type of values stored in the cache.
 */
template <typename KeyType, typename ValueType>
class ObLRUCache
{
public:
  /**
   * @brief Constructs an `ObLRUCache` with a specified capacity.
   *
   * @param capacity The maximum number of elements the cache can hold.
   */
  ObLRUCache(size_t capacity) : capacity_(capacity) {}

  /**
   * @brief Retrieves a value from the cache using the specified key.
   *
   * This method searches for the specified key in the cache. If the key is found, the
   * corresponding value is returned and the key-value pair is moved to the front of the
   * LRU list (indicating recent use).
   *
   * @param key The key to search for in the cache.
   * @param value A reference to store the value associated with the key.
   * @return `true` if the key is found and the value is retrieved; `false` otherwise.
   */
  bool get(const KeyType &key, ValueType &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    list_.splice(list_.begin(), list_, it->second);
    value = it->second->second;
    return true;
  }

  void put(const KeyType &key, const ValueType &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capacity_ == 0) {
      return;
    }
    auto it = map_.find(key);
    if (it != map_.end()) {
      list_.splice(list_.begin(), list_, it->second);
      it->second->second = value;
      return;
    }
    if (capacity_ > 0 && map_.size() >= capacity_) {
      auto last = list_.back();
      map_.erase(last.first);
      list_.pop_back();
    }
    list_.emplace_front(key, value);
    map_[key] = list_.begin();
  }

  bool contains(const KeyType &key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.find(key) != map_.end();
  }

private:
  size_t capacity_;
  mutable std::mutex mutex_;
  std::list<std::pair<KeyType, ValueType>> list_;
  std::unordered_map<KeyType, typename std::list<std::pair<KeyType, ValueType>>::iterator> map_;
};

template <typename Key, typename Value>
ObLRUCache<Key, Value> *new_lru_cache(uint32_t capacity)
{
  return new ObLRUCache<Key, Value>(capacity);
}

}  // namespace oceanbase
