#include "ArenaManager.h"

#include <cstdlib>

#include <Logging.h>

namespace {

constexpr const char* kTag = "ARENA_MGR";

}  // namespace

ArenaManager::ArenaManager() = default;

ArenaManager& ArenaManager::instance() {
  static ArenaManager manager;
  return manager;
}

bool ArenaManager::init(const size_t poolCapacity) {
  if (poolBuffer_) {
    deinit();
  }

  if (poolCapacity == 0) {
    return false;
  }

  uint8_t* pool = static_cast<uint8_t*>(std::malloc(poolCapacity));
  if (!pool) {
    LOG_ERR(kTag, "OOM: global arena pool allocation failed (%u bytes)", static_cast<unsigned>(poolCapacity));
    return false;
  }

  poolBuffer_ = pool;
  poolCapacity_ = poolCapacity;
  offset_ = 0;
  peakUsed_ = 0;
  currentContext_ = ActivityContext::NONE;
  contextArena_.initWithBorrowed(poolBuffer_, poolCapacity_);

  LOG_INF(kTag, "Global arena pool initialized: capacity=%u", static_cast<unsigned>(poolCapacity));
  return true;
}

void ArenaManager::deinit() {
  if (poolBuffer_) {
    std::free(poolBuffer_);
    poolBuffer_ = nullptr;
    poolCapacity_ = 0;
    offset_ = 0;
    peakUsed_ = 0;
    currentContext_ = ActivityContext::NONE;
    contextArena_.shutdown();
  }
}

void ArenaManager::switch_context(const ActivityContext ctx) {
  if (!valid()) {
    LOG_ERR(kTag, "switch_context failed: arena manager not initialized");
    return;
  }

  if (currentContext_ == ctx) {
    return;
  }

  const auto prev = currentContext_;
  currentContext_ = ctx;
  offset_ = 0;
  peakUsed_ = 0;
  contextArena_.reset();
  contextArena_.initWithBorrowed(poolBuffer_, poolCapacity_);

  LOG_DBG(kTag, "Switched context: %u -> %u, offset=%u, peak=%u, capacity=%u",
          static_cast<unsigned>(prev), static_cast<unsigned>(ctx),
          static_cast<unsigned>(offset_), static_cast<unsigned>(peakUsed_),
          static_cast<unsigned>(poolCapacity_));
}

void* ArenaManager::allocate(const size_t bytes, const size_t alignment) {
  if (!valid()) {
    return nullptr;
  }

  if (bytes == 0) {
    return nullptr;
  }

  uint8_t* current = poolBuffer_ + offset_;
  uintptr_t raw = reinterpret_cast<uintptr_t>(current);
  uintptr_t aligned = (raw + static_cast<uintptr_t>(alignment) - 1U) & ~(static_cast<uintptr_t>(alignment) - 1U);
  const size_t padding = static_cast<size_t>(aligned - raw);
  const size_t total = padding + bytes;

  if (total > poolCapacity_ - offset_) {
    LOG_ERR(kTag, "OOM: arena allocation failed, requested=%u, remaining=%u, capacity=%u",
            static_cast<unsigned>(bytes), static_cast<unsigned>(poolCapacity_ - offset_),
            static_cast<unsigned>(poolCapacity_));
    return nullptr;
  }

  offset_ += total;
  if (offset_ > peakUsed_) {
    peakUsed_ = offset_;
  }

  return reinterpret_cast<void*>(aligned);
}

mem::Arena* ArenaManager::arena_for(const ActivityContext ctx) {
  if (!valid()) {
    return nullptr;
  }

  if (currentContext_ != ctx) {
    LOG_ERR(kTag, "arena_for: context mismatch, requested=%u, current=%u",
            static_cast<unsigned>(ctx), static_cast<unsigned>(currentContext_));
    return nullptr;
  }

  return &contextArena_;
}
