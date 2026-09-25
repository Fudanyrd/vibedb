//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

class BufferPoolManager;
class ReadPageGuard;
class WritePageGuard;

/**
 * @brief The declaration of the `BufferPoolManager` class.
 *
 * As stated in the writeup, the buffer pool is responsible for moving physical pages of data back and forth from
 * buffers in main memory to persistent storage. It also behaves as a cache, keeping frequently used pages in memory for
 * faster access, and evicting unused or cold pages back out to storage.
 *
 * Make sure you read the writeup in its entirety before attempting to implement the buffer pool manager. You also need
 * to have completed the implementation of both the `ArcReplacer` and `DiskManager` classes.
 */
class BufferPoolManager {
 public:
  BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager = nullptr);

  /**
   * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
   */
  ~BufferPoolManager() = default;

  /**
   * @brief Returns the number of frames that this buffer pool manages.
   */
  auto Size() const -> size_t { return num_frames_; }

  /**
   * @brief Allocates a new page on disk.
   *
   * ### Implementation
   *
   * You will maintain a thread-safe, monotonically increasing counter in the form of a `std::atomic<page_id_t>`.
   * See the documentation on [atomics](https://en.cppreference.com/w/cpp/atomic/atomic) for more information.
   *
   * @return The page ID of the newly allocated page.
   */
  auto NewPage() -> page_id_t { return next_page_id_.fetch_add(1); }
  auto DeletePage(page_id_t page_id) -> bool;
  auto CheckedWritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown)
      -> std::optional<WritePageGuard>;
  auto CheckedReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> std::optional<ReadPageGuard>;

  /**
   * @brief A wrapper around `CheckedWritePage` that unwraps the inner value if it exists.
   *
   * If `CheckedWritePage` returns a `std::nullopt`, **this function aborts the entire process.**
   *
   * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer
   * pool manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
   *
   * See the documentation for `CheckedPageWrite` for more information about implementation.
   *
   * @param page_id The ID of the page we want to read.
   * @param access_type The type of page access.
   * @return WritePageGuard A page guard ensuring exclusive and mutable access to a page's data.
   */
  auto WritePage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> WritePageGuard {
    auto guard_opt = CheckedWritePage(page_id, access_type);

    if (!guard_opt.has_value()) {
      fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
      std::abort();
    }

    return std::move(guard_opt).value();
  }

  /**
   * @brief A wrapper around `CheckedReadPage` that unwraps the inner value if it exists.
   *
   * If `CheckedReadPage` returns a `std::nullopt`, **this function aborts the entire process.**
   *
   * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer
   * pool manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
   *
   * See the documentation for `CheckedPageRead` for more information about implementation.
   *
   * @param page_id The ID of the page we want to read.
   * @param access_type The type of page access.
   * @return ReadPageGuard A page guard ensuring shared and read-only access to a page's data.
   */
  auto ReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> ReadPageGuard {
    auto guard_opt = CheckedReadPage(page_id, access_type);

    if (!guard_opt.has_value()) {
      fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
      std::abort();
    }

    return std::move(guard_opt).value();
  }
  auto FlushPageUnsafe(page_id_t page_id) -> bool;

  /**
   * @brief Flushes a page's data out to disk safely.
   *
   * This function will write out a page's data to disk if it has been modified. If the given page is not in memory,
   * this function will return `false`.
   *
   * You should take a lock on the page in this function to ensure that a consistent state is flushed to disk.
   *
   * ### Implementation
   *
   * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
   * `CheckedWritePage`, and `Flush` in the page guards, as it will likely be much easier to understand what to do.
   *
   * TODO(P1): Add implementation
   *
   * @param page_id The page ID of the page to be flushed.
   * @return `false` if the page could not be found in the page table; otherwise, `true`.
   */
  auto FlushPage(page_id_t page_id) -> bool {
    auto guard = CheckedReadPage(page_id);
    if (!guard.has_value()) {
      return false;
    }
    guard->Flush();
    return true;
  }
  void FlushAllPagesUnsafe();
  void FlushAllPages();

  /**
   * @brief Retrieves the pin count of a page. If the page does not exist in memory, return `std::nullopt`.
   *
   * This function is thread safe. Callers may invoke this function in a multi-threaded environment where multiple
   * threads access the same page.
   *
   * This function is intended for testing purposes. If this function is implemented incorrectly, it will definitely
   * cause problems with the test suite and autograder.
   *
   * # Implementation
   *
   * We will use this function to test if your buffer pool manager is managing pin counts correctly. Since the
   * `pin_count_` field in `FrameHeader` is an atomic type, you do not need to take the latch on the frame that holds
   * the page we want to look at. Instead, you can simply use an atomic `load` to safely load the value stored. You will
   * still need to take the buffer pool latch, however.
   *
   * Again, if you are unfamiliar with atomic types, see the official C++ docs
   * [here](https://en.cppreference.com/w/cpp/atomic/atomic).
   *
   * TODO(P1): Add implementation
   *
   * @param page_id The page ID of the page we want to get the pin count of.
   * @return std::optional<size_t> The pin count if the page exists; otherwise, `std::nullopt`.
   */
  auto GetPinCount(page_id_t page_id) -> std::optional<size_t> {
    std::scoped_lock lock(*bpm_latch_);
    const auto &page_table = page_table_;
    auto it = page_table.find(page_id);
    if (it == page_table_.end()) {
      return std::nullopt;
    }
    return frames_[it->second]->pin_count_.load();
  }

 private:
  /** @brief The number of frames in the buffer pool. */
  const size_t num_frames_;

  /** @brief The next page ID to be allocated.  */
  std::atomic<page_id_t> next_page_id_;

  /**
   * @brief The latch protecting the buffer pool's inner data structures.
   *
   * TODO(P1) We recommend replacing this comment with details about what this latch actually protects.
   */
  std::shared_ptr<std::mutex> bpm_latch_;

  /** @brief The frame headers of the frames that this buffer pool manages. */
  std::vector<std::shared_ptr<FrameHeader>> frames_;

  /** @brief The page table that keeps track of the mapping between pages and buffer pool frames. */
  std::unordered_map<page_id_t, frame_id_t> page_table_;

  /** @brief A list of free frames that do not hold any page's data. */
  std::list<frame_id_t> free_frames_;

  /** @brief The replacer to find unpinned / candidate pages for eviction. */
  std::shared_ptr<ArcReplacer> replacer_;

  /** @brief A pointer to the disk scheduler. Shared with the page guards for flushing. */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief A pointer to the log manager.
   *
   * Note: Please ignore this for P1.
   */
  LogManager *log_manager_ __attribute__((__unused__));

  /**
   * @brief Finds or creates a frame to hold `page_id`, pinning the resulting frame.
   *
   * The caller must hold `bpm_latch_`. If the page is already in the buffer pool, the corresponding frame is pinned
   * and returned. Otherwise a free frame is used, or an evictable frame is found via the replacer and its contents
   * (written back to disk first if dirty) are replaced. Returns `std::nullopt` if there is no memory available.
   */
  auto GetAvailableFrame(page_id_t page_id, AccessType access_type) -> std::optional<frame_id_t>;

  /** @brief Synchronously writes the data of `frame_id` to `page_id` on disk. Assumes the caller holds `bpm_latch_`. */
  void WriteFrameToDisk(frame_id_t frame_id, page_id_t page_id);

  /** @brief Synchronously reads `page_id` from disk into `frame_id`. Assumes the caller holds `bpm_latch_`. */
  void ReadFrameFromDisk(frame_id_t frame_id, page_id_t page_id);
};
}  // namespace bustub
