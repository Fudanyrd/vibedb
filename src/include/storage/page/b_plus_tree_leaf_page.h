//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.h
//
// Identification: src/include/storage/page/b_plus_tree_leaf_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>
#define LEAF_PAGE_HEADER_SIZE 16
#define LEAF_PAGE_DEFAULT_TOMB_CNT 0
#define LEAF_PAGE_TOMB_CNT ((NumTombs < 0) ? LEAF_PAGE_DEFAULT_TOMB_CNT : NumTombs)
#define LEAF_PAGE_SLOT_CNT                                                                               \
  ((BUSTUB_PAGE_SIZE - LEAF_PAGE_HEADER_SIZE - sizeof(size_t) - (LEAF_PAGE_TOMB_CNT * sizeof(size_t))) / \
   (sizeof(KeyType) + sizeof(ValueType)))  // NOLINT

/**
 * Store indexed key and record id(record id = page id combined with slot id,
 * see include/common/rid.h for detailed implementation) together within leaf
 * page. Only support unique key.
 *
 * Leaf pages also contain a fixed buffer of "tombstone" indexes for entries
 * that have been deleted.
 *
 * Leaf page format (keys are stored in order, tomb order is up to you):
 *  --------------------
 * | HEADER | TOMB_SIZE | (where TOMB_SIZE is num_tombstones_)
 *  --------------------
 *  -----------------------------------
 * | TOMB(0) | TOMB(1) | ... | TOMB(k) |
 *  -----------------------------------
 *  ---------------------------------
 * | KEY(1) | KEY(2) | ... | KEY(n) |
 *  ---------------------------------
 *  ---------------------------------
 * | RID(1) | RID(2) | ... | RID(n) |
 *  ---------------------------------
 *
 *  Header format (size in byte, 16 bytes in total):
 *  -----------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |
 *  -----------------------------------------------
 *  -----------------
 * | NextPageId (4) |
 *  -----------------
 */
FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class BPlusTreeLeafPage : public BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreeLeafPage() = delete;
  BPlusTreeLeafPage(const BPlusTreeLeafPage &other) = delete;

  /**
   * @brief Init method after creating a new leaf page
   *
   * After creating a new leaf page from buffer pool, must call initialize method to set default values,
   * including set page type, set current size to zero, set page id/parent id, set
   * next page id and set max size.
   *
   * @param max_size Max size of the leaf node
   */
  void Init(int max_size = LEAF_PAGE_SLOT_CNT) {
    SetPageType(IndexPageType::LEAF_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetNextPageId(INVALID_PAGE_ID);
    num_tombstones_ = 0;
  }

  /**
   * @brief Helper function for fetching tombstones of a page.
   * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
   */
  auto GetTombstones() const -> std::vector<KeyType> {
    std::vector<KeyType> tombstones;
    tombstones.reserve(num_tombstones_);
    for (size_t i = 0; i < num_tombstones_; i++) {
      tombstones.push_back(key_array_[tombstones_[i]]);
    }
    return tombstones;
  }

  // Helper methods
  auto GetNextPageId() const -> page_id_t { return next_page_id_; }
  void SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }
  auto KeyAt(int index) const -> KeyType { return key_array_[index]; }

  auto ValueAt(int index) const -> ValueType { return rid_array_[index]; }

  void SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

  void SetValueAt(int index, const ValueType &value) { rid_array_[index] = value; }

  /** @return The number of pending deletions currently buffered in this page. */
  auto GetNumTombstones() const -> size_t { return num_tombstones_; }

  /** @return The maximum number of pending deletions this page can buffer. */
  auto GetTombstoneCapacity() const -> size_t { return LEAF_PAGE_TOMB_CNT; }

  /** @return Whether the entry at `index` has a pending deletion. */
  auto IsTombstoned(int index) const -> bool {
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (static_cast<int>(tombstones_[i]) == index) {
        return true;
      }
    }
    return false;
  }

  /** @brief Buffer a pending deletion for the entry at `index` (assumes there is room). */
  void AddTombstone(int index) { tombstones_[num_tombstones_++] = static_cast<size_t>(index); }

  /** @brief Drop the pending deletion for the entry at `index`, keeping the rest in recency order. */
  void ClearTombstone(int index) {
    size_t write = 0;
    for (size_t read = 0; read < num_tombstones_; read++) {
      if (static_cast<int>(tombstones_[read]) != index) {
        tombstones_[write++] = tombstones_[read];
      }
    }
    num_tombstones_ = write;
  }

  /**
   * @brief Physically remove the entry at `index`, maintaining the tombstone buffer.
   *
   * Any pending deletion for the removed entry is discarded and all buffered indexes after `index` are shifted down.
   */
  void RemoveEntryAt(int index) {
    int size = GetSize();
    for (int i = index; i < size - 1; i++) {
      key_array_[i] = key_array_[i + 1];
      rid_array_[i] = rid_array_[i + 1];
    }
    SetSize(size - 1);
    size_t write = 0;
    for (size_t read = 0; read < num_tombstones_; read++) {
      size_t tomb = tombstones_[read];
      if (static_cast<int>(tomb) == index) {
        continue;
      }
      if (static_cast<int>(tomb) > index) {
        tomb--;
      }
      tombstones_[write++] = tomb;
    }
    num_tombstones_ = write;
  }

  /** @brief Physically remove the entry associated with the oldest pending deletion. */
  void EvictOldestTombstone() { RemoveEntryAt(static_cast<int>(tombstones_[0])); }

  /** @brief Insert an entry at `index`, shifting buffered tombstone indexes as necessary. */
  void InsertAt(int index, const KeyType &key, const ValueType &value) {
    for (int i = GetSize(); i > index; i--) {
      key_array_[i] = key_array_[i - 1];
      rid_array_[i] = rid_array_[i - 1];
    }
    key_array_[index] = key;
    rid_array_[index] = value;
    ChangeSizeBy(1);
    for (size_t i = 0; i < num_tombstones_; i++) {
      if (static_cast<int>(tombstones_[i]) >= index) {
        tombstones_[i]++;
      }
    }
  }

  /**
   * @brief Insert `key`/`value`, or revive a tombstoned entry with the new value.
   * @return false if a live (non-tombstoned) entry with `key` already exists.
   */
  auto InsertPair(const KeyType &key, const ValueType &value, const KeyComparator &comparator) -> bool {
    int index = LowerBound(key, comparator);
    if (index < GetSize() && comparator(KeyAt(index), key) == 0) {
      if (!IsTombstoned(index)) {
        return false;
      }
      ClearTombstone(index);
      SetValueAt(index, value);
      return true;
    }
    InsertAt(index, key, value);
    return true;
  }

  /**
   * @brief Split this leaf by moving entries `[mid, GetSize())` into `new_leaf`.
   *
   * Tombstones are partitioned by recency order and re-based onto their new page.
   */
  void SplitMoveTo(BPlusTreeLeafPage *new_leaf, int mid) {
    int total = GetSize();
    for (int i = mid; i < total; i++) {
      new_leaf->key_array_[i - mid] = key_array_[i];
      new_leaf->rid_array_[i - mid] = rid_array_[i];
    }
    new_leaf->SetSize(total - mid);
    SetSize(mid);

    std::vector<size_t> left;
    std::vector<size_t> right;
    left.reserve(num_tombstones_);
    right.reserve(num_tombstones_);
    for (size_t i = 0; i < num_tombstones_; i++) {
      size_t tomb = tombstones_[i];
      if (static_cast<int>(tomb) < mid) {
        left.push_back(tomb);
      } else {
        right.push_back(tomb - static_cast<size_t>(mid));
      }
    }
    for (size_t i = 0; i < left.size(); i++) {
      tombstones_[i] = left[i];
    }
    for (size_t i = 0; i < right.size(); i++) {
      new_leaf->tombstones_[i] = right[i];
    }
    num_tombstones_ = left.size();
    new_leaf->num_tombstones_ = right.size();
  }

  /**
   * @brief Merge `right` into this page. `this` must be the left page of the pair.
   *
   * The recipient's pending deletions are considered older, so they are applied first whenever the combined
   * tombstone buffers would overflow. `right`'s (more recent) deletions are always preserved.
   */
  void MergeFrom(BPlusTreeLeafPage *right) {
    while (num_tombstones_ + right->num_tombstones_ > LEAF_PAGE_TOMB_CNT) {
      EvictOldestTombstone();
    }
    int base = GetSize();
    for (int i = 0; i < right->GetSize(); i++) {
      key_array_[base + i] = right->key_array_[i];
      rid_array_[base + i] = right->rid_array_[i];
    }
    SetSize(base + right->GetSize());
    for (size_t i = 0; i < right->num_tombstones_; i++) {
      tombstones_[num_tombstones_++] = static_cast<size_t>(base) + right->tombstones_[i];
    }
    SetNextPageId(right->GetNextPageId());
  }

  /**
   * @brief Return the index of the first key that is not less than `key`.
   *
   * @param key The key being searched for.
   * @param comparator The comparator used to order keys.
   * @return The first index `i` such that `KeyAt(i) >= key`, or `GetSize()` if none.
   */
  auto LowerBound(const KeyType &key, const KeyComparator &comparator) const -> int {
    int lo = 0;
    int hi = GetSize();
    while (lo < hi) {
      int mid = lo + (hi - lo) / 2;
      if (comparator(key_array_[mid], key) < 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  /**
   * @brief for test only return a string representing all keys in
   * this leaf page formatted as "(tombkey1, tombkey2, ...|key1,key2,key3,...)"
   *
   * @return std::string
   */
  auto ToString() const -> std::string {
    std::string kstr = "(";
    bool first = true;

    auto tombs = GetTombstones();
    for (size_t i = 0; i < tombs.size(); i++) {
      kstr.append(std::to_string(tombs[i].ToString()));
      if ((i + 1) < tombs.size()) {
        kstr.append(",");
      }
    }

    kstr.append("|");

    for (int i = 0; i < GetSize(); i++) {
      KeyType key = KeyAt(i);
      if (first) {
        first = false;
      } else {
        kstr.append(",");
      }

      kstr.append(std::to_string(key.ToString()));
    }
    kstr.append(")");

    return kstr;
  }

 private:
  page_id_t next_page_id_;
  size_t num_tombstones_;
  // Fixed-size tombstone buffer (indexes into key_array_ / rid_array_).
  size_t tombstones_[LEAF_PAGE_TOMB_CNT];
  // Array members for page data.
  KeyType key_array_[LEAF_PAGE_SLOT_CNT];
  ValueType rid_array_[LEAF_PAGE_SLOT_CNT];
  // (Spring 2025) Feel free to add more fields and helper functions below if needed
};

}  // namespace bustub
