//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>
#include <utility>

#include "storage/index/index_iterator.h"

namespace bustub {
FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::Normalize() {
  while (!is_end_) {
    auto leaf = guard_.As<LeafPage>();
    while (index_ < leaf->GetSize()) {
      if (!leaf->IsTombstoned(index_)) {
        return;
      }
      index_++;
    }
    page_id_t next_page_id = leaf->GetNextPageId();
    if (next_page_id == INVALID_PAGE_ID) {
      is_end_ = true;
      index_ = 0;
      guard_.Drop();
      return;
    }
    /* Must acquire the lock on its next page before releasing the current page */
    auto sibling = bpm_->ReadPage(next_page_id);
    guard_ = std::move(sibling);
    index_ = 0;
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return is_end_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  auto leaf = guard_.As<LeafPage>();
  const auto &k = leaf->GetKeyArray()[index_];
  const auto &v = leaf->GetValueArray()[index_];
  return {k, v};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  if (!is_end_) {
    index_++;
    Normalize();
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
