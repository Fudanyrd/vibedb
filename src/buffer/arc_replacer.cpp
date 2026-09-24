// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <algorithm>
#include <optional>
#include <utility>
#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * @brief Remove an alive frame from its list and alive_map_, updating the
 * evictable counters. Does not touch the ghost lists.
 *
 * @return the removed status, or nullptr if the frame is absent (or pinned
 * when `require_evictable` is set).
 */
auto ArcReplacer::DetachAlive(frame_id_t frame_id, bool require_evictable) -> std::shared_ptr<FrameStatus> {
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return nullptr;
  }
  auto &status = it->second;
  if (require_evictable && !status->evictable_) {
    return nullptr;
  }
  if (status->arc_status_ == ArcStatus::MRU) {
    mru_.erase(status->alive_iter_);
    if (status->evictable_) {
      mru_evictable_--;
    }
  } else {
    mfu_.erase(status->alive_iter_);
    if (status->evictable_) {
      mfu_evictable_--;
    }
  }
  if (status->evictable_) {
    curr_size_--;
  }
  auto result = status;
  alive_map_.erase(it);
  return result;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) {
    return std::nullopt;
  }

  // Decide which side to evict from: if the MRU list is smaller than the
  // target size, try MFU first; otherwise try MRU first.
  bool try_mfu_first = mru_.size() < mru_target_size_;
  if ((try_mfu_first ? mfu_evictable_ : mru_evictable_) == 0) {
    try_mfu_first = !try_mfu_first;
    if ((try_mfu_first ? mfu_evictable_ : mru_evictable_) == 0) {
      return std::nullopt;
    }
  }

  std::list<frame_id_t> &alive = try_mfu_first ? mfu_ : mru_;
  std::list<page_id_t> &ghost = try_mfu_first ? mfu_ghost_ : mru_ghost_;
  ArcStatus ghost_status = try_mfu_first ? ArcStatus::MFU_GHOST : ArcStatus::MRU_GHOST;

  // Skip pinned entries and evict the least recently used evictable frame.
  for (auto it = alive.rbegin(); it != alive.rend(); ++it) {
    frame_id_t frame_id = *it;
    auto entry = alive_map_.find(frame_id);
    if (entry == alive_map_.end() || !entry->second->evictable_) {
      continue;
    }
    page_id_t page_id = entry->second->page_id_;
    std::shared_ptr<FrameStatus> status = DetachAlive(frame_id, true);
    BUSTUB_ENSURE(status != nullptr, "failed to detach an evictable frame");

    ghost.push_front(page_id);
    auto ghost_entry = std::make_shared<FrameStatus>(page_id, -1, false, ghost_status);
    ghost_entry->ghost_iter_ = ghost.begin();
    ghost_map_[page_id] = ghost_entry;
    return frame_id;
  }
  return std::nullopt;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);

  // Case 1: the page is already alive in MRU/MFU. Promote it to the front of MFU.
  auto alive_it = alive_map_.find(frame_id);
  if (alive_it != alive_map_.end()) {
    const auto &status = alive_it->second;
    if (status->arc_status_ == ArcStatus::MRU) {
      mru_.erase(status->alive_iter_);
      if (status->evictable_) {
        mru_evictable_--;
      }
      mfu_.push_front(frame_id);
      status->alive_iter_ = mfu_.begin();
      status->arc_status_ = ArcStatus::MFU;
      if (status->evictable_) {
        mfu_evictable_++;
      }
    } else {
      mfu_.erase(status->alive_iter_);
      mfu_.push_front(frame_id);
      status->alive_iter_ = mfu_.begin();
    }
    return;
  }

  // Cases 2 and 3: the page is a ghost. Adapt the target size, then place it in MFU.
  auto ghost_it = ghost_map_.find(page_id);
  if (ghost_it != ghost_map_.end()) {
    const auto &ghost_status = ghost_it->second;
    if (ghost_status->arc_status_ == ArcStatus::MRU_GHOST) {
      if (mru_ghost_.size() >= mfu_ghost_.size()) {
        mru_target_size_ = std::min(replacer_size_, mru_target_size_ + 1);
      } else if (mru_ghost_.size() > 0) {
        size_t delta = mfu_ghost_.size() / mru_ghost_.size();
        mru_target_size_ = std::min(replacer_size_, mru_target_size_ + delta);
      }
      mru_ghost_.erase(ghost_status->ghost_iter_);
    } else {
      if (mfu_ghost_.size() >= mru_ghost_.size()) {
        if (mru_target_size_ > 0) {
          mru_target_size_--;
        }
      } else if (mfu_ghost_.size() > 0) {
        size_t delta = mru_ghost_.size() / mfu_ghost_.size();
        mru_target_size_ = mru_target_size_ > delta ? mru_target_size_ - delta : 0;
      }
      mfu_ghost_.erase(ghost_status->ghost_iter_);
    }
    ghost_map_.erase(ghost_it);

    auto status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MFU);
    mfu_.push_front(frame_id);
    status->alive_iter_ = mfu_.begin();
    alive_map_[frame_id] = status;
    return;
  }

  // Case 4: the page is not tracked at all. Trim a ghost entry if needed, then
  // insert the page at the front of MRU.
  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    // Case 4a: MRU + MRU ghost is full, drop the oldest MRU ghost.
    if (!mru_ghost_.empty()) {
      page_id_t victim = mru_ghost_.back();
      mru_ghost_.pop_back();
      ghost_map_.erase(victim);
    }
  } else if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() == 2 * replacer_size_) {
    // Case 4b: the four lists are at capacity, drop the oldest MFU ghost.
    if (!mfu_ghost_.empty()) {
      page_id_t victim = mfu_ghost_.back();
      mfu_ghost_.pop_back();
      ghost_map_.erase(victim);
    }
  }

  auto status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
  mru_.push_front(frame_id);
  status->alive_iter_ = mru_.begin();
  alive_map_[frame_id] = status;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  BUSTUB_ENSURE(frame_id >= 0, "invalid frame id");

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }
  auto &status = it->second;
  if (status->evictable_ == set_evictable) {
    return;
  }
  status->evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
    if (status->arc_status_ == ArcStatus::MRU) {
      mru_evictable_++;
    } else {
      mfu_evictable_++;
    }
  } else {
    curr_size_--;
    if (status->arc_status_ == ArcStatus::MRU) {
      mru_evictable_--;
    } else {
      mfu_evictable_--;
    }
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }
  BUSTUB_ENSURE(it->second->evictable_, "Remove called on a non-evictable frame");
  DetachAlive(frame_id, true);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
