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
  if (valid()) {
    deinit();
  }

  if (poolCapacity == 0) {
    return false;
  }

  // Store target capacity but defer the actual malloc to first use (lazy init).
  // This leaves maximum contiguous heap available for the EPUB parser and other
  // early operations, while still reserving arena memory for decoder hot paths.
  targetCapacity_ = poolCapacity;
  poolCapacity_ = 0;
  poolBuffer_ = nullptr;
  offset_ = 0;
  peakUsed_ = 0;
  currentContext_ = ActivityContext::NONE;

  LOG_INF(kTag, "ArenaManager configured: target capacity=%u (lazy, not yet allocated)",
          static_cast<unsigned>(poolCapacity));
  return true;
}

bool ArenaManager::ensureAllocated() {
  if (valid()) {
    return true;  // already allocated
  }

  if (targetCapacity_ == 0) {
    LOG_ERR(kTag, "ensureAllocated: no target capacity configured, call init() first");
    return false;
  }

  uint8_t* pool = static_cast<uint8_t*>(std::malloc(targetCapacity_));
  if (!pool) {
    LOG_ERR(kTag, "OOM: lazy arena pool allocation failed (%u bytes)",
            static_cast<unsigned>(targetCapacity_));
    return false;
  }

  poolBuffer_ = pool;
  poolCapacity_ = targetCapacity_;
  offset_ = 0;
  peakUsed_ = 0;
  contextArena_.initWithBorrowed(poolBuffer_, poolCapacity_);

  LOG_INF(kTag, "Lazy arena pool allocated: capacity=%u, heap now free=%u",
          static_cast<unsigned>(poolCapacity_),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
  return true;
}

void ArenaManager::deinit() {
  if (poolBuffer_) {
    std::free(poolBuffer_);
    poolBuffer_ = nullptr;
  }
  poolCapacity_ = 0;
  targetCapacity_ = 0;
  offset_ = 0;
  peakUsed_ = 0;
  currentContext_ = ActivityContext::NONE;
  contextArena_.shutdown();
}

void ArenaManager::switch_context(const ActivityContext ctx) {
  if (!valid()) {
    // Pool not yet allocated. Defer actual allocation until a decoder asks
    // for memory via allocate(), so early activities and the EPUB parser see
    // maximum contiguous heap. We still record the requested context so the
    // first real allocation starts in the right activity scope.
    currentContext_ = ctx;
    LOG_DBG(kTag, "Deferred switch_context to %u until first allocation", static_cast<unsigned>(ctx));
    return;
  }

  if (locked_) {
    LOG_ERR(kTag, "CRITICAL: switch_context(%u) refused — arena is locked by decoder/renderer",
            static_cast<unsigned>(ctx));
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

  if (ArenaManager::instance().valid()) {
    LOG_INF(kTag, "Activity context=%u pool used=%u/%u peak=%u", static_cast<unsigned>(ctx),
            static_cast<unsigned>(ArenaManager::instance().used()),
            static_cast<unsigned>(ArenaManager::instance().capacity()),
            static_cast<unsigned>(ArenaManager::instance().peakUsed()));
  }
}

void* ArenaManager::allocate(const size_t bytes, const size_t alignment) {
  // Lazily allocate the pool on first use so EPUB parser and early boot path
  // see maximum contiguous heap before any decoder requests arena memory.
  if (!ensureAllocated()) {
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
