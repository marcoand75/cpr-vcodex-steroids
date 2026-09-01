// ArenaAllocator.h — Modern header-only memory management for ESP32-C3.
//
// Design goals:
//   - Zero heap fragmentation: one contiguous block, O(1) push, O(1) reset.
//   - Configurable alignment (4 or 8 bytes). 4 is the RV32IMC natural minimum;
//     8 is used when the arena stores double/uint64_t or aligned SIMD-like structs.
//   - No thread-safety by default: arenas are intended to be owned by a single
//     task/activity. Add a mutex only if the same arena is shared across
//     FreeRTOS tasks (not the case for per-Activity scratch arenas).
//   - Explicit overflow assert instead of silent wrap.
//   - Modern C++17 features: RAII markers, pool sub-allocator, debug telemetry.
//
// Usage:
//   Arena arena(buffer, capacity, ArenaAlign::_8);
//   void* p = arena.push(size);
//   arena.reset();  // O(1), no free() loops.
//
// The arena does NOT call destructors. Only POD/trivially-destructible types
// should live in an arena, or the caller must manually destroy them before
// reset().
//
// Thread-safety: NONE by default. Arena and PoolAllocator instances must
// only be accessed from a single FreeRTOS task (typically the UI/render
// task owning the Activity). Do not share an Arena instance across tasks
// without adding external synchronization.

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace util {

// -----------------------------------------------------------------------------
// Alignment
// -----------------------------------------------------------------------------
enum class ArenaAlign : uint8_t {
  _4 = 4,
  _8 = 8
};

// -----------------------------------------------------------------------------
// Arena — bump/linear allocator
// -----------------------------------------------------------------------------
class Arena {
 public:
  Arena() = default;
  Arena(uint8_t* buffer, size_t capacity, ArenaAlign align = ArenaAlign::_8)
      : buffer_(buffer),
        capacity_(capacity),
        used_(0),
        align_(static_cast<size_t>(align)),
        max_used_(0) {
    assert(buffer != nullptr || capacity == 0);
  }

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  Arena(Arena&& other) noexcept
      : buffer_(other.buffer_),
        capacity_(other.capacity_),
        used_(other.used_),
        align_(other.align_),
        max_used_(other.max_used_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.used_ = 0;
    other.max_used_ = 0;
  }

  Arena& operator=(Arena&& other) noexcept {
    if (this != &other) {
      buffer_ = other.buffer_;
      capacity_ = other.capacity_;
      used_ = other.used_;
      align_ = other.align_;
      max_used_ = other.max_used_;
      other.buffer_ = nullptr;
      other.capacity_ = 0;
      other.used_ = 0;
      other.max_used_ = 0;
    }
    return *this;
  }

  ~Arena() = default;

  void* push(size_t size) {
    if (size == 0) return nullptr;
    size_t aligned = alignUp(size);
    size_t new_used = used_ + aligned;
    assert(new_used <= capacity_ && "Arena overflow — increase capacity or reset sooner");
    if (new_used > capacity_) {
      return nullptr;
    }
    void* p = static_cast<void*>(buffer_ + used_);
    used_ = new_used;
    if (used_ > max_used_) max_used_ = used_;
    return p;
  }

  template <typename T, typename... Args>
  T* pushNew(Args&&... args) {
    static_assert(std::is_trivially_destructible<T>::value,
                  "Arena::pushNew requires trivially-destructible types. "
                  "For non-trivial types, use placement new + manual destroy.");
    void* p = push(sizeof(T));
    if (!p) return nullptr;
    return new (p) T(std::forward<Args>(args)...);
  }

  void rewind(size_t cursor) {
    assert(cursor <= used_ && "Arena rewind past start");
    used_ = cursor;
  }

  void reset() noexcept {
    used_ = 0;
  }

  size_t cursor() const noexcept { return used_; }

  size_t used() const noexcept { return used_; }
  size_t capacity() const noexcept { return capacity_; }
  size_t maxUsed() const noexcept { return max_used_; }
  size_t remaining() const noexcept { return (used_ < capacity_) ? (capacity_ - used_) : 0; }
  bool empty() const noexcept { return used_ == 0; }
  size_t align() const noexcept { return align_; }

  void setBuffer(uint8_t* buffer, size_t capacity) {
    assert(buffer != nullptr || capacity == 0);
    buffer_ = buffer;
    capacity_ = capacity;
    used_ = 0;
    max_used_ = 0;
  }

  size_t alignUp(size_t size) const noexcept {
    return (size + align_ - 1) & ~(align_ - 1);
  }

 private:
  uint8_t* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
  size_t align_ = static_cast<size_t>(ArenaAlign::_8);
  size_t max_used_ = 0;
};

// -----------------------------------------------------------------------------
// ScopedArenaReset — RAII marker that rewinds the arena on scope exit
// -----------------------------------------------------------------------------
class ScopedArenaReset {
 public:
  explicit ScopedArenaReset(Arena& arena) : arena_(arena), cursor_(arena.cursor()) {}
  ~ScopedArenaReset() { arena_.rewind(cursor_); }
  size_t cursor() const { return cursor_; }

 private:
  Arena& arena_;
  size_t cursor_;
};

// -----------------------------------------------------------------------------
// PoolAllocator — fixed-size object pool, O(1) alloc/free
// -----------------------------------------------------------------------------
template <typename T, size_t N>
class PoolAllocator {
 public:
  static_assert(std::is_trivially_destructible<T>::value,
                "PoolAllocator requires trivially-destructible types");

  PoolAllocator() { reset(); }

  T* alloc() {
    for (size_t i = 0; i < N; ++i) {
      if (!used_[i]) {
        used_[i] = true;
        ++count_;
        return &data_[i];
      }
    }
    assert(false && "PoolAllocator overflow");
    return nullptr;
  }

  void free(T* ptr) {
    if (!ptr) return;
    const size_t index = static_cast<size_t>(ptr - data_);
    assert(index < N && "PoolAllocator free out of bounds");
    used_[index] = false;
    if (count_ > 0) --count_;
  }

  size_t usedCount() const { return count_; }
  size_t capacity() const { return N; }
  bool empty() const { return count_ == 0; }

  void reset() {
    used_.fill(false);
    count_ = 0;
  }

 private:
  T data_[N]{};
  std::array<bool, N> used_{};
  size_t count_ = 0;
};

// -----------------------------------------------------------------------------
// StaticVector — fixed-capacity vector stored inline (no heap)
//
// Requires T to be default-constructible (data_[N] is value-initialized at
// construction time) and trivially copyable/movable/destructible if used
// with emplace_back, since emplace_back placement-news over already-live
// storage. Do not use with non-trivial T without revisiting this assumption.
// -----------------------------------------------------------------------------
template <typename T, size_t N>
class StaticVector {
 public:
  using value_type = T;
  using size_type = size_t;
  using iterator = T*;
  using const_iterator = const T*;

  StaticVector() = default;

  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return N; }
  bool empty() const noexcept { return size_ == 0; }

  T& operator[](size_type i) {
    assert(i < size_);
    return data_[i];
  }
  const T& operator[](size_type i) const {
    assert(i < size_);
    return data_[i];
  }

  T& front() { return data_[0]; }
  const T& front() const { return data_[0]; }
  T& back() { return data_[size_ - 1]; }
  const T& back() const { return data_[size_ - 1]; }

  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }

  iterator begin() noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator end() const noexcept { return data_ + size_; }
  const_iterator cbegin() const noexcept { return data_; }
  const_iterator cend() const noexcept { return data_ + size_; }

  void push_back(const T& value) {
    assert(size_ < N && "StaticVector overflow");
    data_[size_++] = value;
  }
  void push_back(T&& value) {
    assert(size_ < N && "StaticVector overflow");
    data_[size_++] = std::move(value);
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    assert(size_ < N && "StaticVector overflow");
    T* p = new (data_ + size_) T(std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  void pop_back() {
    assert(size_ > 0);
    --size_;
  }

  void clear() noexcept { size_ = 0; }

  void reserve(size_type) {
    // No-op: capacity is fixed at compile time.
  }

  void resize(size_type n) {
    assert(n <= N);
    size_ = n;
  }

  iterator erase(const_iterator pos) {
    assert(pos >= begin() && pos < end());
    size_type idx = static_cast<size_type>(pos - begin());
    std::move(data_ + idx + 1, data_ + size_, data_ + idx);
    --size_;
    return data_ + idx;
  }

  size_type max_size() const noexcept { return N; }

 private:
  T data_[N]{};
  size_type size_ = 0;
};

// Global scratch arena (definition in main.cpp)
extern Arena g_activityArena;

}  // namespace util