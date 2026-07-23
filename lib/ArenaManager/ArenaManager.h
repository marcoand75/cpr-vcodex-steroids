#pragma once

#include <cstddef>
#include <cstdint>

#include <Arena.h>

enum class ActivityContext : uint8_t {
  NONE = 0,
  HOME,
  LIBRARY,
  READER,
  SCREENSAVER,
  SLEEP,
  VIEWER,
  STATS,
};

class ArenaManager {
 public:
  static ArenaManager& instance();

  bool init(size_t poolCapacity);
  void deinit();

  /// Ensure the arena pool is physically allocated.  Called automatically by
  /// allocate() on first use; may also be called explicitly to pre-warm.
  /// Returns true if the pool is ready after the call (was already ready or
  /// was successfully allocated now).
  bool ensureAllocated();

  void switch_context(ActivityContext ctx);
  ActivityContext current_context() const { return currentContext_; }

  /// Lock the arena to prevent switch_context() while a decoder or renderer
  /// holds pointers into the arena pool.  While locked, switch_context() logs
  /// an error and returns without resetting the pool.
  void lock() { locked_ = true; }
  void unlock() { locked_ = false; }
  bool is_locked() const { return locked_; }

  /// RAII guard: locks the arena on construction, unlocks on destruction.
  struct LockGuard {
    LockGuard() { ArenaManager::instance().lock(); }
    ~LockGuard() { ArenaManager::instance().unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
  };

  void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));
  mem::Arena* arena_for(ActivityContext ctx);

  size_t used() const { return offset_; }
  size_t capacity() const { return poolCapacity_; }
  size_t remaining() const { return capacity() > offset_ ? capacity() - offset_ : 0; }
  size_t peakUsed() const { return peakUsed_; }
  bool exhausted() const { return offset_ >= poolCapacity_; }
  bool valid() const { return poolBuffer_ != nullptr; }

 private:
  ArenaManager();
  ArenaManager(const ArenaManager&) = delete;
  ArenaManager& operator=(const ArenaManager&) = delete;

  uint8_t* poolBuffer_ = nullptr;
  size_t targetCapacity_ = 0;  // desired pool size, set by init()
  size_t poolCapacity_ = 0;    // actual allocated size, 0 until poolBuffer_ is allocated
  size_t offset_ = 0;
  size_t peakUsed_ = 0;
  ActivityContext currentContext_ = ActivityContext::NONE;
  mem::Arena contextArena_;
  bool locked_ = false;
};
