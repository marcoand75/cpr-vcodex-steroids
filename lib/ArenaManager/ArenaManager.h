#pragma once

#include <cstddef>
#include <cstdint>

#include <Arena.h>

enum class ActivityContext : uint8_t {
  NONE = 0,
  HOME,
  LIBRARY,
  READER,
};

class ArenaManager {
 public:
  static ArenaManager& instance();

  bool init(size_t poolCapacity);
  void deinit();

  void switch_context(ActivityContext ctx);
  ActivityContext current_context() const { return currentContext_; }

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
  size_t poolCapacity_ = 0;
  size_t offset_ = 0;
  size_t peakUsed_ = 0;
  ActivityContext currentContext_ = ActivityContext::NONE;
  mem::Arena contextArena_;
};
