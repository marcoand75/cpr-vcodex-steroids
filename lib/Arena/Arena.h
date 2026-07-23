#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

namespace mem {

class Arena {
 public:
  Arena() = default;
  ~Arena();

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  bool init(size_t capacity);
  void reset();
  void* alloc(size_t size, size_t alignment = alignof(std::max_align_t));

  size_t used() const;
  size_t capacity() const;
  size_t remaining() const;
  size_t peakUsed() const;
  bool exhausted() const;
  bool valid() const;
  const uint8_t* base() const { return base_; }

 private:
  uint8_t* base_ = nullptr;
  size_t capacity_ = 0;
  size_t offset_ = 0;
  size_t peakUsed_ = 0;
};

inline bool Arena::init(size_t capacity) {
  if (capacity == 0) {
    base_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
    return false;
  }

  uint8_t* base = static_cast<uint8_t*>(std::malloc(capacity));
  if (!base) {
    capacity_ = 0;
    offset_ = 0;
    return false;
  }

  base_ = base;
  capacity_ = capacity;
  offset_ = 0;
  return true;
}

inline Arena::~Arena() {
  std::free(base_);
}

inline void Arena::reset() {
  offset_ = 0;
  peakUsed_ = 0;
}

inline void* Arena::alloc(size_t size, size_t alignment) {
  if (!base_ || size == 0) {
    return nullptr;
  }

  if (offset_ >= capacity_) {
    return nullptr;
  }

  uint8_t* current = base_ + offset_;
  uintptr_t raw = reinterpret_cast<uintptr_t>(current);
  uintptr_t aligned = (raw + static_cast<uintptr_t>(alignment) - 1U) &
                      ~(static_cast<uintptr_t>(alignment) - 1U);
  size_t padding = static_cast<size_t>(aligned - raw);
  size_t total = padding + size;

  if (total > capacity_ - offset_) {
    return nullptr;
  }

  offset_ += total;
  if (offset_ > peakUsed_) {
    peakUsed_ = offset_;
  }
  return reinterpret_cast<void*>(aligned);
}

inline size_t Arena::used() const {
  return offset_;
}

inline size_t Arena::capacity() const {
  return capacity_;
}

inline size_t Arena::remaining() const {
  return capacity_ > offset_ ? capacity_ - offset_ : 0;
}

inline size_t Arena::peakUsed() const {
  return peakUsed_;
}

inline bool Arena::exhausted() const {
  return offset_ >= capacity_;
}

inline bool Arena::valid() const {
  return base_ != nullptr;
}

// Lightweight STL-compatible allocator backed by a `mem::Arena`.
//
// Ownership model:
// - Arena-backed pointers are not freed individually; they are reclaimed in
//   bulk by `Arena::reset()` or by destroying the arena itself.
// - If the arena is invalid or full, `allocate` falls back to `std::malloc`.
//   In that case `deallocate` frees with `std::free`, because it is outside
//   the arena buffer range.
template <typename T>
class ArenaAllocator {
 public:
  using value_type = T;

  ArenaAllocator() = default;
  explicit ArenaAllocator(mem::Arena& arena) : arena_(&arena) {}

  template <typename U>
  ArenaAllocator(const ArenaAllocator<U>& other) noexcept : arena_(other.arena_) {}

  T* allocate(size_t n) {
    if (arena_ && arena_->valid()) {
      if (n == 0) return nullptr;
      void* p = arena_->alloc(n * sizeof(T), alignof(T));
      if (p) return static_cast<T*>(p);
    }
    return static_cast<T*>(std::malloc(n * sizeof(T)));
  }

  void deallocate(T* p, size_t) noexcept {
    if (!p || !arena_ || !arena_->valid()) {
      std::free(p);
      return;
    }

    const uint8_t* base = arena_->base();
    const uint8_t* limit = base + arena_->capacity();
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(p);
    if (ptr >= base && ptr < limit) {
      return;
    }
    std::free(p);
  }

  template <typename U>
  struct rebind {
    using other = ArenaAllocator<U>;
  };

  bool operator==(const ArenaAllocator& other) const { return arena_ == other.arena_; }
  bool operator!=(const ArenaAllocator& other) const { return !(*this == other); }

 private:
  template <typename U> friend class ArenaAllocator;
  mem::Arena* arena_ = nullptr;
};

}  // namespace mem
