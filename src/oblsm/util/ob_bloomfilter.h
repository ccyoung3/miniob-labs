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

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <functional>

namespace oceanbase {

/**
 * @class ObBloomfilter
 * @brief 支持并发访问的布隆过滤器实现
 */
class ObBloomfilter {
public:
    /**
     * @brief 构造函数
     * @param hash_func_count 哈希函数数量
     * @param total_bits 位数组总长度
     */
    ObBloomfilter(size_t hash_func_count = 4, size_t total_bits = 65536)
        : hash_func_count_(hash_func_count),
          total_bits_(total_bits),
          object_count_(0) {
        // 向上取整计算字节数
        size_t byte_count = (total_bits_ + 7) / 8;
        bits_ = std::make_unique<std::atomic<uint8_t>[]>(byte_count);
        for (size_t i = 0; i < byte_count; ++i) {
            bits_[i].store(0, std::memory_order_relaxed);
        }
    }

    /**
     * @brief 插入元素（支持并发）
     */
    void insert(const std::string &object) {
        size_t h1, h2;
        generate_base_hashes(object, h1, h2);

        for (size_t i = 0; i < hash_func_count_; ++i) {
            size_t bit_pos = (h1 + i * h2) % total_bits_;
            set_bit_atomic(bit_pos);
        }
        object_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief 检查元素是否存在（支持并发）
     */
    bool contains(const std::string &object) const {
        size_t h1, h2;
        generate_base_hashes(object, h1, h2);

        for (size_t i = 0; i < hash_func_count_; ++i) {
            size_t bit_pos = (h1 + i * h2) % total_bits_;
            if (!get_bit(bit_pos)) {
                return false; 
            }
        }
        return true; 
    }

    /**
     * @brief 清空布隆过滤器
     */
    void clear() {
        size_t byte_count = (total_bits_ + 7) / 8;
        for (size_t i = 0; i < byte_count; ++i) {
            bits_[i].store(0, std::memory_order_relaxed);
        }
        object_count_.store(0, std::memory_order_relaxed);
    }

    size_t object_count() const {
        return object_count_.load(std::memory_order_relaxed);
    }

    bool empty() const {
        return object_count() == 0;
    }

private:
    /**
     * @brief 生成基础哈希值
     */
    void generate_base_hashes(const std::string &data, size_t &h1, size_t &h2) const {
        std::hash<std::string> hasher;
        h1 = hasher(data);
        h2 = (h1 >> 17) | (h1 << 15); // 注意这里已修正为半角 |
        h2 ^= 0xdeadbeef;
    }

    /**
     * @brief 原子设置位
     */
    void set_bit_atomic(size_t pos) {
        size_t byte_idx = pos / 8;
        uint8_t bit_mask = static_cast<uint8_t>(1 << (pos % 8)); // 修正拼写
        bits_[byte_idx].fetch_or(bit_mask, std::memory_order_relaxed);
    }

    /**
     * @brief 读取位状态
     */
    bool get_bit(size_t pos) const {
        size_t byte_idx = pos / 8;
        uint8_t bit_mask = static_cast<uint8_t>(1 << (pos % 8)); // 修正拼写
        return (bits_[byte_idx].load(std::memory_order_relaxed) & bit_mask) != 0;
    }

private:
    size_t hash_func_count_;
    size_t total_bits_;
    std::atomic<size_t> object_count_;
    std::unique_ptr<std::atomic<uint8_t>[]> bits_;
};

}  // namespace oceanbase