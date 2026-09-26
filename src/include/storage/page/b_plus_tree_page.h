//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_page.h
//
// Identification: src/include/storage/page/b_plus_tree_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/index/generic_key.h"

namespace bustub {

#define MappingType std::pair<KeyType, ValueType>

#define FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN \
  template <typename KeyType, typename ValueType, typename KeyComparator, ssize_t NumTombs = 0>
#define FULL_INDEX_TEMPLATE_ARGUMENTS \
  template <typename KeyType, typename ValueType, typename KeyComparator, ssize_t NumTombs>
#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>

// define page type enum
enum class IndexPageType { INVALID_INDEX_PAGE = 0, LEAF_PAGE, INTERNAL_PAGE };

/**
 * Both internal and leaf page are inherited from this page.
 *
 * It actually serves as a header part for each B+ tree page and
 * contains information shared by both leaf page and internal page.
 *
 * Header format (size in byte, 12 bytes in total):
 * ---------------------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |  ...   |
 * ---------------------------------------------------------
 */
class BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreePage() = delete;
  BPlusTreePage(const BPlusTreePage &other) = delete;
  ~BPlusTreePage() = delete;

  auto IsLeafPage() const -> bool { return page_type_ == IndexPageType::LEAF_PAGE; }
  void SetPageType(IndexPageType page_type) { page_type_ = page_type; }

  auto GetSize() const -> int { return size_; }
  void SetSize(int size) { size_ = size; }
  void ChangeSizeBy(int amount) { size_ += amount; }

  auto GetMaxSize() const -> int { return max_size_; }
  void SetMaxSize(int max_size) { max_size_ = max_size; }

  /*
   * Helper method to get min page size
   * Generally, min page size == max page size / 2
   * But whether you will take ceil() or floor() depends on your implementation
   */
  auto GetMinSize() const -> int { return IsLeafPage() ? max_size_ / 2 : (max_size_ + 1) / 2; }

 private:
  // Member variables, attributes that both internal and leaf page share
  IndexPageType page_type_;
  // Number of key & value pairs in a page
  int size_;
  // Max number of key & value pairs in a page
  int max_size_;
};

}  // namespace bustub
