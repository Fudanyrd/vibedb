//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.h
//
// Identification: src/include/storage/page/page_guard.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "buffer/arc_replacer.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"

namespace bustub {

class BufferPoolManager;
class FrameHeader;

/**
 * @brief A helper class for `BufferPoolManager` that manages a frame of memory and related metadata.
 *
 * This class represents headers for frames of memory that the `BufferPoolManager` stores pages of data into. Note that
 * the actual frames of memory are not stored directly inside a `FrameHeader`, rather the `FrameHeader`s store pointer
 * to the frames and are stored separately them.
 *
 * ---
 *
 * Something that may (or may not) be of interest to you is why the field `data_` is stored as a vector that is
 * allocated on the fly instead of as a direct pointer to some pre-allocated chunk of memory.
 *
 * In a traditional production buffer pool manager, all memory that the buffer pool is intended to manage is allocated
 * in one large contiguous array (think of a very large `malloc` call that allocates several gigabytes of memory up
 * front). This large contiguous block of memory is then divided into contiguous frames. In other words, frames are
 * defined by an offset from the base of the array in page-sized (4 KB) intervals.
 *
 * In BusTub, we instead allocate each frame on its own (via a `std::vector<char>`) in order to easily detect buffer
 * overflow with address sanitizer. Since C++ has no notion of memory safety, it would be very easy to cast a page's
 * data pointer into some large data type and start overwriting other pages of data if they were all contiguous.
 *
 * If you would like to attempt to use more efficient data structures for your buffer pool manager, you are free to do
 * so. However, you will likely benefit significantly from detecting buffer overflow in future projects (especially
 * project 2).
 */
class FrameHeader {
  friend class BufferPoolManager;
  friend class ReadPageGuard;
  friend class WritePageGuard;

 public:
  explicit FrameHeader(frame_id_t frame_id);

 private:
  auto GetData() const -> const char *;
  auto GetDataMut() -> char *;
  void Reset();

  /** @brief The frame ID / index of the frame this header represents. */
  const frame_id_t frame_id_;

  /** @brief The readers / writer latch for this frame. */
  std::shared_mutex rwlatch_;

  /** @brief The number of pins on this frame keeping the page in memory. */
  std::atomic<size_t> pin_count_;

  /** @brief The dirty flag. */
  bool is_dirty_;

  /**
   * @brief A pointer to the data of the page that this frame holds.
   *
   * If the frame does not hold any page data, the frame contains all null bytes.
   */
  std::vector<char> data_;

  /**
   * @brief The page ID of the page currently stored in this frame, or `INVALID_PAGE_ID` when the frame is empty.
   */
  page_id_t page_id_{INVALID_PAGE_ID};
};

/**
 * @brief An RAII object that grants thread-safe read access to a page of data.
 *
 * The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
 * `ReadPageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
 *
 * With `ReadPageGuard`s, there can be multiple threads that share read access to a page's data. However, the existence
 * of any `ReadPageGuard` on a page implies that no thread can be mutating the page's data.
 */
class ReadPageGuard {
  /** @brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` */
  friend class BufferPoolManager;

 public:
  /**
   * @brief The default constructor for a `ReadPageGuard`.
   *
   * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
   * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
   * via `=`.
   *
   * **Use of an uninitialized page guard is undefined behavior.**
   *
   * In other words, the only way to get a valid `ReadPageGuard` is through the buffer pool manager.
   */
  ReadPageGuard() = default;

  ReadPageGuard(const ReadPageGuard &) = delete;
  auto operator=(const ReadPageGuard &) -> ReadPageGuard & = delete;
  ReadPageGuard(ReadPageGuard &&that) noexcept;
  auto operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard &;

  /**
   * @brief Gets the page ID of the page this guard is protecting.
   */
  auto GetPageId() const -> page_id_t { return page_id_; }

  /**
   * @brief Gets a `const` pointer to the page of data this guard is protecting.
   */
  auto GetData() const -> const char * {
    BUSTUB_ASSERT(is_valid_, "tried to use an invalid read guard");
    return frame_->GetData();
  }
  template <class T>
  auto As() const -> const T * {
    return reinterpret_cast<const T *>(GetData());
  }

  /**
   * @brief Returns whether the page is dirty (modified but not flushed to the disk).
   */
  auto IsDirty() const -> bool {
    BUSTUB_ASSERT(is_valid_, "tried to use an invalid read guard");
    return frame_->is_dirty_;
  }
  void Flush();
  void Drop();
  /** @brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. */
  ~ReadPageGuard() { Drop(); }

 private:
  /** @brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` */
  explicit ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame, std::shared_ptr<ArcReplacer> replacer,
                         std::shared_ptr<std::mutex> bpm_latch, std::shared_ptr<DiskScheduler> disk_scheduler);

  /** @brief The page ID of the page we are guarding. */
  page_id_t page_id_;

  /**
   * @brief The frame that holds the page this guard is protecting.
   *
   * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
   */
  std::shared_ptr<FrameHeader> frame_;

  /**
   * @brief A shared pointer to the buffer pool's replacer.
   *
   * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's replacer in order to set the frame as evictable on destruction.
   */
  std::shared_ptr<ArcReplacer> replacer_;

  /**
   * @brief A shared pointer to the buffer pool's latch.
   *
   * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
   */
  std::shared_ptr<std::mutex> bpm_latch_;

  /**
   * @brief A shared pointer to the buffer pool's disk scheduler.
   *
   * Used when flushing pages to disk.
   */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief The validity flag for this `ReadPageGuard`.
   *
   * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
   * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
   * automatically set this field to `false`.
   *
   * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
   * or `Drop()` invalid members, causing a segmentation fault.
   */
  bool is_valid_{false};

  /**
   * TODO(P1): You may add any fields under here that you think are necessary.
   *
   * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
   * `std::shared_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
   */
};

/**
 * @brief An RAII object that grants thread-safe write access to a page of data.
 *
 * The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
 * `WritePageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
 *
 * With a `WritePageGuard`, there can be only be one thread that has exclusive ownership over the page's data. This
 * means that the owner of the `WritePageGuard` can mutate the page's data as much as they want. However, the existence
 * of a `WritePageGuard` implies that no other `WritePageGuard` or any `ReadPageGuard`s for the same page can exist at
 * the same time.
 */
class WritePageGuard {
  /** @brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` */
  friend class BufferPoolManager;

 public:
  /**
   * @brief The default constructor for a `WritePageGuard`.
   *
   * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
   * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
   * via `=`.
   *
   * **Use of an uninitialized page guard is undefined behavior.**
   *
   * In other words, the only way to get a valid `WritePageGuard` is through the buffer pool manager.
   */
  WritePageGuard() = default;

  WritePageGuard(const WritePageGuard &) = delete;
  auto operator=(const WritePageGuard &) -> WritePageGuard & = delete;
  WritePageGuard(WritePageGuard &&that) noexcept;
  auto operator=(WritePageGuard &&that) noexcept -> WritePageGuard &;

  /**
   * @brief Gets the page ID of the page this guard is protecting.
   */
  auto GetPageId() const -> page_id_t { return page_id_; }

  /**
   * @brief Gets a `const` pointer to the page of data this guard is protecting.
   */
  auto GetData() const -> const char * {
    BUSTUB_ASSERT(is_valid_, "tried to use an invalid write guard");
    return frame_->GetData();
  }
  template <class T>
  auto As() const -> const T * {
    return reinterpret_cast<const T *>(GetData());
  }

  /**
   * @brief Gets a mutable pointer to the page of data this guard is protecting.
   */
  auto GetDataMut() -> char * {
    BUSTUB_ASSERT(is_valid_, "tried to use an invalid write guard");
    return frame_->GetDataMut();
  }
  template <class T>
  auto AsMut() -> T * {
    return reinterpret_cast<T *>(GetDataMut());
  }

  /**
   * @brief Returns whether the page is dirty (modified but not flushed to the disk).
   */
  auto IsDirty() const -> bool {
    BUSTUB_ASSERT(is_valid_, "tried to use an invalid write guard");
    return frame_->is_dirty_;
  }

  void Flush();
  void Drop();

  /** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
  ~WritePageGuard() { Drop(); }

 private:
  /** @brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` */
  explicit WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame, std::shared_ptr<ArcReplacer> replacer,
                          std::shared_ptr<std::mutex> bpm_latch, std::shared_ptr<DiskScheduler> disk_scheduler);

  /** @brief The page ID of the page we are guarding. */
  page_id_t page_id_;

  /**
   * @brief The frame that holds the page this guard is protecting.
   *
   * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
   */
  std::shared_ptr<FrameHeader> frame_;

  /**
   * @brief A shared pointer to the buffer pool's replacer.
   *
   * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's replacer in order to set the frame as evictable on destruction.
   */
  std::shared_ptr<ArcReplacer> replacer_;

  /**
   * @brief A shared pointer to the buffer pool's latch.
   *
   * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
   * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
   */
  std::shared_ptr<std::mutex> bpm_latch_;

  /**
   * @brief A shared pointer to the buffer pool's disk scheduler.
   *
   * Used when flushing pages to disk.
   */
  std::shared_ptr<DiskScheduler> disk_scheduler_;

  /**
   * @brief The validity flag for this `WritePageGuard`.
   *
   * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
   * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
   * automatically set this field to `false`.
   *
   * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
   * or `Drop()` invalid members, causing a segmentation fault.
   */
  bool is_valid_{false};

  /**
   * TODO(P1): You may add any fields under here that you think are necessary.
   *
   * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
   * `std::unique_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
   */
};

}  // namespace bustub
