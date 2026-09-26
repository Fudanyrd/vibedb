//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  return header_guard.As<BPlusTreeHeaderPage>()->root_page_id_ == INVALID_PAGE_ID;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  page_id_t root_id;
  {
    auto header_guard = bpm_->ReadPage(header_page_id_);
    root_id = header_guard.As<BPlusTreeHeaderPage>()->root_page_id_;
  }
  if (root_id == INVALID_PAGE_ID) {
    return false;
  }

  page_id_t current = root_id;
  for (;;) {
    auto guard = bpm_->ReadPage(current);
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      auto leaf = guard.As<LeafPage>();
      int index = leaf->LowerBound(key, comparator_);
      if (index < leaf->GetSize() && comparator_(leaf->KeyAt(index), key) == 0) {
        result->push_back(leaf->ValueAt(index));
        return true;
      }
      return false;
    }
    auto internal = guard.As<InternalPage>();
    current = internal->ValueAt(internal->Lookup(key, comparator_));
  }
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  page_id_t root_id;
  {
    auto header_guard = bpm_->ReadPage(header_page_id_);
    root_id = header_guard.As<BPlusTreeHeaderPage>()->root_page_id_;
  }

  // Empty tree: create the root leaf page.
  if (root_id == INVALID_PAGE_ID) {
    auto header_guard = bpm_->WritePage(header_page_id_);
    auto header = header_guard.AsMut<BPlusTreeHeaderPage>();
    if (header->root_page_id_ != INVALID_PAGE_ID) {
      // Another thread created the root while we were not holding the header latch.
      header_guard.Drop();
      return Insert(key, value);
    }
    page_id_t new_root_id = bpm_->NewPage();
    auto leaf_guard = bpm_->WritePage(new_root_id);
    auto leaf = leaf_guard.AsMut<LeafPage>();
    leaf->Init(leaf_max_size_);
    leaf->SetKeyAt(0, key);
    leaf->SetValueAt(0, value);
    leaf->SetSize(1);
    header->root_page_id_ = new_root_id;
    return true;
  }

  // Optimistic path: descend with read latches and only take a write latch on the leaf if it can absorb the
  // insertion without splitting.
  std::deque<ReadPageGuard> path;
  path.push_back(bpm_->ReadPage(root_id));
  while (!path.back().As<BPlusTreePage>()->IsLeafPage()) {
    auto internal = path.back().As<InternalPage>();
    path.push_back(bpm_->ReadPage(internal->ValueAt(internal->Lookup(key, comparator_))));
  }

  auto leaf = path.back().As<LeafPage>();
  int index = leaf->LowerBound(key, comparator_);
  if (index < leaf->GetSize() && comparator_(leaf->KeyAt(index), key) == 0) {
    return false;
  }

  if (leaf->GetSize() + 1 < leaf->GetMaxSize()) {
    page_id_t leaf_id = path.back().GetPageId();
    path.clear();
    auto leaf_guard = bpm_->WritePage(leaf_id);
    auto leaf_mut = leaf_guard.AsMut<LeafPage>();
    int pos = leaf_mut->LowerBound(key, comparator_);
    if (pos < leaf_mut->GetSize() && comparator_(leaf_mut->KeyAt(pos), key) == 0) {
      return false;
    }
    for (int i = leaf_mut->GetSize(); i > pos; i--) {
      leaf_mut->SetKeyAt(i, leaf_mut->KeyAt(i - 1));
      leaf_mut->SetValueAt(i, leaf_mut->ValueAt(i - 1));
    }
    leaf_mut->SetKeyAt(pos, key);
    leaf_mut->SetValueAt(pos, value);
    leaf_mut->ChangeSizeBy(1);
    return true;
  }

  // The leaf is (nearly) full, so the insertion may split pages. Fall back to a pessimistic top-down insertion.
  path.clear();
  auto root_guard = bpm_->WritePage(root_id);
  KeyType split_key;
  page_id_t split_page = INVALID_PAGE_ID;
  bool split = InsertRecursive(std::move(root_guard), key, value, &split_key, &split_page);

  if (split) {
    page_id_t new_root_id = bpm_->NewPage();
    auto new_root_guard = bpm_->WritePage(new_root_id);
    auto new_root = new_root_guard.AsMut<InternalPage>();
    new_root->Init(internal_max_size_);
    new_root->SetKeyAt(0, KeyType{});
    new_root->SetValueAt(0, root_id);
    new_root->SetKeyAt(1, split_key);
    new_root->SetValueAt(1, split_page);
    new_root->SetSize(2);

    auto header_guard = bpm_->WritePage(header_page_id_);
    header_guard.AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_id;
  }
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertRecursive(WritePageGuard guard, const KeyType &key, const ValueType &value,
                                     KeyType *split_key, page_id_t *split_page) -> bool {
  auto page = guard.AsMut<BPlusTreePage>();

  if (page->IsLeafPage()) {
    auto leaf = guard.AsMut<LeafPage>();
    int pos = leaf->LowerBound(key, comparator_);
    for (int i = leaf->GetSize(); i > pos; i--) {
      leaf->SetKeyAt(i, leaf->KeyAt(i - 1));
      leaf->SetValueAt(i, leaf->ValueAt(i - 1));
    }
    leaf->SetKeyAt(pos, key);
    leaf->SetValueAt(pos, value);
    leaf->ChangeSizeBy(1);

    if (leaf->GetSize() < leaf->GetMaxSize()) {
      return false;
    }

    page_id_t new_leaf_id = bpm_->NewPage();
    auto new_leaf_guard = bpm_->WritePage(new_leaf_id);
    auto new_leaf = new_leaf_guard.AsMut<LeafPage>();
    new_leaf->Init(leaf_max_size_);

    int total = leaf->GetSize();
    int mid = total / 2;
    for (int i = mid; i < total; i++) {
      new_leaf->SetKeyAt(i - mid, leaf->KeyAt(i));
      new_leaf->SetValueAt(i - mid, leaf->ValueAt(i));
    }
    new_leaf->SetSize(total - mid);
    leaf->SetSize(mid);
    new_leaf->SetNextPageId(leaf->GetNextPageId());
    leaf->SetNextPageId(new_leaf_id);

    *split_key = new_leaf->KeyAt(0);
    *split_page = new_leaf_id;
    return true;
  }

  auto internal = guard.AsMut<InternalPage>();
  int child_index = internal->Lookup(key, comparator_);
  page_id_t child_id = internal->ValueAt(child_index);

  KeyType child_split_key;
  page_id_t child_split_page = INVALID_PAGE_ID;
  auto child_guard = bpm_->WritePage(child_id);
  bool child_split = InsertRecursive(std::move(child_guard), key, value, &child_split_key, &child_split_page);
  if (!child_split) {
    return false;
  }

  int insert_index = child_index + 1;
  for (int i = internal->GetSize(); i > insert_index; i--) {
    internal->SetKeyAt(i, internal->KeyAt(i - 1));
    internal->SetValueAt(i, internal->ValueAt(i - 1));
  }
  internal->SetKeyAt(insert_index, child_split_key);
  internal->SetValueAt(insert_index, child_split_page);
  internal->ChangeSizeBy(1);

  if (internal->GetSize() <= internal->GetMaxSize()) {
    return false;
  }

  page_id_t new_internal_id = bpm_->NewPage();
  auto new_internal_guard = bpm_->WritePage(new_internal_id);
  auto new_internal = new_internal_guard.AsMut<InternalPage>();
  new_internal->Init(internal_max_size_);

  int total = internal->GetSize();
  int mid = total / 2;
  *split_key = internal->KeyAt(mid);
  *split_page = new_internal_id;
  new_internal->SetValueAt(0, internal->ValueAt(mid));
  for (int i = mid + 1; i < total; i++) {
    new_internal->SetKeyAt(i - mid, internal->KeyAt(i));
    new_internal->SetValueAt(i - mid, internal->ValueAt(i));
  }
  new_internal->SetSize(total - mid);
  internal->SetSize(mid);
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // Declaration of context instance.
  Context ctx;
  UNIMPLEMENTED("TODO(P2): Add implementation.");
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
