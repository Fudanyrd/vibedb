//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.h
//
// Identification: src/include/storage/index/index_iterator.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <utility>
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator, NumTombs>
#define SHORT_INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class IndexIterator {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;

 public:
  // you may define your own constructor based on your member variables
  IndexIterator() = default;
  IndexIterator(TracedBufferPoolManager *bpm, ReadPageGuard guard, int index)
      : bpm_(bpm), guard_(std::move(guard)), index_(index), is_end_(false) {
    Normalize();
  }
  ~IndexIterator() = default;  // NOLINT

  IndexIterator(IndexIterator &&that) noexcept = default;
  auto operator=(IndexIterator &&that) noexcept -> IndexIterator & = default;

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    if (is_end_ || itr.is_end_) {
      return is_end_ && itr.is_end_;
    }
    return guard_.GetPageId() == itr.guard_.GetPageId() && index_ == itr.index_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool { return !(*this == itr); }

 private:
  /** @brief Advance until the iterator lands on a live (non-tombstoned) entry, or becomes the end iterator. */
  void Normalize();

  // add your own private member variables here
  TracedBufferPoolManager *bpm_{nullptr};
  ReadPageGuard guard_{};
  int index_{0};
  bool is_end_{true};
};

}  // namespace bustub
