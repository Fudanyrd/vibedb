//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "common/macros.h"

namespace bustub {

/**
 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
 *
 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
 *
 * @param frame_id The frame ID / index of the frame we are creating a header for.
 */
FrameHeader::FrameHeader(frame_id_t frame_id) : frame_id_(frame_id), data_(BUSTUB_PAGE_SIZE, 0) { Reset(); }

/**
 * @brief Get a raw const pointer to the frame's data.
 *
 * @return const char* A pointer to immutable data that the frame stores.
 */
auto FrameHeader::GetData() const -> const char * { return data_.data(); }

/**
 * @brief Get a raw mutable pointer to the frame's data.
 *
 * @return char* A pointer to mutable data that the frame stores.
 */
auto FrameHeader::GetDataMut() -> char * { return data_.data(); }

/**
 * @brief Resets a `FrameHeader`'s member fields.
 */
void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  pin_count_.store(0);
  is_dirty_ = false;
  page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Creates a new `BufferPoolManager` instance and initializes all fields.
 *
 * See the documentation for `BufferPoolManager` in "buffer/buffer_pool_manager.h" for more information.
 *
 * ### Implementation
 *
 * We have implemented the constructor for you in a way that makes sense with our reference solution. You are free to
 * change anything you would like here if it doesn't fit with you implementation.
 *
 * Be warned, though! If you stray too far away from our guidance, it will be much harder for us to help you. Our
 * recommendation would be to first implement the buffer pool manager using the stepping stones we have provided.
 *
 * Once you have a fully working solution (all Gradescope test cases pass), then you can try more interesting things!
 *
 * @param num_frames The size of the buffer pool.
 * @param disk_manager The disk manager.
 * @param log_manager The log manager. Please ignore this for P1.
 */
BufferPoolManager::BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager)
    : num_frames_(num_frames),
      next_page_id_(0),
      bpm_latch_(std::make_shared<std::mutex>()),
      replacer_(std::make_shared<ArcReplacer>(num_frames)),
      disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)),
      log_manager_(log_manager) {
  // Not strictly necessary...
  std::scoped_lock latch(*bpm_latch_);

  // Initialize the monotonically increasing counter at 0.
  next_page_id_.store(0);

  // Allocate all of the in-memory frames up front.
  frames_.reserve(num_frames_);

  // The page table should have exactly `num_frames_` slots, corresponding to exactly `num_frames_` frames.
  page_table_.reserve(num_frames_);

  // Initialize all of the frame headers, and fill the free frame list with all possible frame IDs (since all frames are
  // initially free).
  for (size_t i = 0; i < num_frames_; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(static_cast<int>(i));
  }
}

/**
 * @brief Removes a page from the database, both on disk and in memory.
 *
 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
 *
 * ### Implementation
 *
 * Think about all of the places that a page or a page's metadata could be, and use that to guide you on implementing
 * this function. You will probably want to implement this function _after_ you have implemented `CheckedReadPage` and
 * `CheckedWritePage`.
 *
 * You should call `DeallocatePage` in the disk scheduler to make the space available for new pages.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to delete.
 * @return `false` if the page exists but could not be deleted, `true` if the page didn't exist or deletion succeeded.
 */
auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::scoped_lock lock(*bpm_latch_);

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    const frame_id_t frame_id = it->second;
    auto &frame = frames_[frame_id];
    // A pinned page is still in use, so it cannot be deleted.
    if (frame->pin_count_.load() > 0) {
      return false;
    }
    replacer_->Remove(frame_id);
    page_table_.erase(it);
    frame->Reset();
    free_frames_.push_back(frame_id);
  }

  // The page may exist on disk even if it is not in the buffer pool, so always deallocate it.
  disk_scheduler_->DeallocatePage(page_id);
  return true;
}

/**
 * @brief Finds or creates a frame that holds `page_id`, pinning it in the process.
 *
 * Assumes the caller holds `bpm_latch_`. This is the core of both `CheckedReadPage` and `CheckedWritePage`.
 */
auto BufferPoolManager::GetAvailableFrame(page_id_t page_id, AccessType access_type) -> std::optional<frame_id_t> {
  // Case 1: the page is already in the buffer pool, so simply pin the existing frame.
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    const frame_id_t frame_id = it->second;
    frames_[frame_id]->pin_count_.fetch_add(1);
    replacer_->SetEvictable(frame_id, false);
    replacer_->RecordAccess(frame_id, page_id, access_type);
    return frame_id;
  }

  frame_id_t frame_id;
  if (!free_frames_.empty()) {
    // Case 2: there is an unused frame available.
    frame_id = free_frames_.front();
    free_frames_.pop_front();
  } else {
    // Case 3: evict an unpinned page to free up a frame, writing it back if it is dirty.
    auto victim = replacer_->Evict();
    if (!victim.has_value()) {
      return std::nullopt;
    }
    frame_id = victim.value();
    auto &victim_frame = frames_[frame_id];
    if (victim_frame->is_dirty_) {
      WriteFrameToDisk(frame_id, victim_frame->page_id_);
      victim_frame->is_dirty_ = false;
    }
    page_table_.erase(victim_frame->page_id_);
  }

  auto &frame = frames_[frame_id];
  frame->Reset();
  frame->page_id_ = page_id;
  ReadFrameFromDisk(frame_id, page_id);
  page_table_[page_id] = frame_id;
  frame->pin_count_.store(1);
  replacer_->RecordAccess(frame_id, page_id, access_type);
  return frame_id;
}

/**
 * @brief Synchronously writes a frame's data out to disk.
 *
 * Assumes the caller holds `bpm_latch_`.
 */
void BufferPoolManager::WriteFrameToDisk(frame_id_t frame_id, page_id_t page_id) {
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  std::vector<DiskRequest> requests;
  requests.push_back(DiskRequest{true, frames_[frame_id]->GetDataMut(), page_id, std::move(promise)});
  disk_scheduler_->Schedule(requests);
  future.get();
}

/**
 * @brief Synchronously reads a page from disk into a frame.
 *
 * Assumes the caller holds `bpm_latch_`.
 */
void BufferPoolManager::ReadFrameFromDisk(frame_id_t frame_id, page_id_t page_id) {
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  std::vector<DiskRequest> requests;
  requests.push_back(DiskRequest{false, frames_[frame_id]->GetDataMut(), page_id, std::move(promise)});
  disk_scheduler_->Schedule(requests);
  future.get();
}

/**
 * @brief Acquires an optional write-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can only be 1 `WritePageGuard` reading/writing a page at a time. This allows data access to be both immutable
 * and mutable, meaning the thread that owns the `WritePageGuard` is allowed to manipulate the page's data however they
 * want. If a user wants to have multiple threads reading the page at the same time, they must acquire a `ReadPageGuard`
 * with `CheckedReadPage` instead.
 *
 * ### Implementation
 *
 * There are three main cases that you will have to implement. The first two are relatively simple: one is when there is
 * plenty of available memory, and the other is when we don't actually need to perform any additional I/O. Think about
 * what exactly these two cases entail.
 *
 * The third case is the trickiest, and it is when we do not have any _easily_ available memory at our disposal. The
 * buffer pool is tasked with finding memory that it can use to bring in a page of memory, using the replacement
 * algorithm you implemented previously to find candidate frames for eviction.
 *
 * Once the buffer pool has identified a frame for eviction, several I/O operations may be necessary to bring in the
 * page of data we want into the frame.
 *
 * There is likely going to be a lot of shared code with `CheckedReadPage`, so you may find creating helper functions
 * useful.
 *
 * These two functions are the crux of this project, so we won't give you more hints than this. Good luck!
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to write to.
 * @param access_type The type of page access.
 * @return std::optional<WritePageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`; otherwise, returns a `WritePageGuard` ensuring exclusive and mutable access to a page's data.
 */
auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  auto frame_id = GetAvailableFrame(page_id, access_type);
  if (!frame_id.has_value()) {
    return std::nullopt;
  }
  auto frame = frames_[frame_id.value()];
  // Release the buffer pool latch before latching the frame to avoid deadlocks. The pin we took keeps the page in
  // memory, so the frame cannot be evicted while we wait for its latch.
  lock.unlock();
  return WritePageGuard(page_id, std::move(frame), replacer_, bpm_latch_, disk_scheduler_);
}

/**
 * @brief Acquires an optional read-locked guard over a page of data. The user can specify an `AccessType` if needed.
 *
 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
 *
 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
 * ensures that any access of data is thread-safe.
 *
 * There can be any number of `ReadPageGuard`s reading the same page of data at a time across different threads.
 * However, all data access must be immutable. If a user wants to mutate the page's data, they must acquire a
 * `WritePageGuard` with `CheckedWritePage` instead.
 *
 * ### Implementation
 *
 * See the implementation details of `CheckedWritePage`.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The ID of the page we want to read.
 * @param access_type The type of page access.
 * @return std::optional<ReadPageGuard> An optional latch guard where if there are no more free frames (out of memory)
 * returns `std::nullopt`; otherwise, returns a `ReadPageGuard` ensuring shared and read-only access to a page's data.
 */
auto BufferPoolManager::CheckedReadPage(page_id_t page_id, AccessType access_type) -> std::optional<ReadPageGuard> {
  std::unique_lock<std::mutex> lock(*bpm_latch_);
  auto frame_id = GetAvailableFrame(page_id, access_type);
  if (!frame_id.has_value()) {
    return std::nullopt;
  }
  auto frame = frames_[frame_id.value()];
  // Release the buffer pool latch before latching the frame to avoid deadlocks. The pin we took keeps the page in
  // memory, so the frame cannot be evicted while we wait for its latch.
  lock.unlock();
  return ReadPageGuard(page_id, std::move(frame), replacer_, bpm_latch_, disk_scheduler_);
}

/**
 * @brief Flushes a page's data out to disk unsafely.
 *
 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
 * function will return `false`.
 *
 * You should not take a lock on the page in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage` and
 * `CheckedWritePage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 *
 * @param page_id The page ID of the page to be flushed.
 * @return `false` if the page could not be found in the page table; otherwise, `true`.
 */
auto BufferPoolManager::FlushPageUnsafe(page_id_t page_id) -> bool {
  std::scoped_lock lock(*bpm_latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }
  auto &frame = frames_[it->second];
  if (frame->is_dirty_) {
    // We do not hold the page latch here, so another thread could set the dirty bit again after we clear it. This is
    // why this variant is "unsafe".
    WriteFrameToDisk(frame->frame_id_, page_id);
    frame->is_dirty_ = false;
  }
  return true;
}

/**
 * @brief Flushes all page data that is in memory to disk unsafely.
 *
 * You should not take locks on the pages in this function.
 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 */
void BufferPoolManager::FlushAllPagesUnsafe() {
  std::scoped_lock lock(*bpm_latch_);
  for (auto &[page_id, frame_id] : page_table_) {
    auto &frame = frames_[frame_id];
    if (frame->is_dirty_) {
      WriteFrameToDisk(frame_id, page_id);
      frame->is_dirty_ = false;
    }
  }
}

/**
 * @brief Flushes all page data that is in memory to disk safely.
 *
 * You should take locks on the pages in this function to ensure that a consistent state is flushed to disk.
 *
 * ### Implementation
 *
 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
 *
 * TODO(P1): Add implementation
 */
void BufferPoolManager::FlushAllPages() {
  // Snapshot the pages currently in memory, then flush each one with its page latch held.
  std::vector<page_id_t> page_ids;
  {
    std::scoped_lock lock(*bpm_latch_);
    page_ids.reserve(page_table_.size());
    for (const auto &entry : page_table_) {
      page_ids.push_back(entry.first);
    }
  }
  for (const page_id_t page_id : page_ids) {
    FlushPage(page_id);
  }
}

}  // namespace bustub
