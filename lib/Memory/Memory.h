#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "esp_heap_caps.h"

// Fragmentation snapshot helpers. free - largest is only a loose upper bound on
// fragmentation (it counts every byte outside the largest free run). Use
// heapFragInfo() to also get the exact number of free blocks: many small free
// blocks (high count with comparatively small largest) = real fragmentation,
// whereas a low count with free ~= largest = the heap is simply in use.

struct HeapFragInfo {
  size_t freeBytes = 0;      // total free heap (8bit+default caps, internal)
  size_t largest = 0;        // largest contiguous free block
  size_t freeBlocks = 0;     // number of distinct free blocks
  size_t allocatedBytes = 0; // bytes currently in use
};

inline HeapFragInfo heapFragInfo() {
  HeapFragInfo info{};
  multi_heap_info_t hinfo{};
  heap_caps_get_info(&hinfo, MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  info.freeBytes = hinfo.total_free_bytes;
  info.largest = hinfo.largest_free_block;
  info.freeBlocks = hinfo.free_blocks;
  info.allocatedBytes = hinfo.total_allocated_bytes;
  return info;
}

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

// Helper struct to call a cleanup function on exit from any scope.
// Use with a lambda to avoid unnecessary allocations from std::function/std::bind:
// Example:
//   auto jpeg = makeUniqueNoThrow<JPEGDEC>();
//   ScopedCleanup cleanup{[&jpeg]{ jpeg->close(); }};

template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ScopedCleanup(ScopedCleanup&&) = delete;
  ScopedCleanup& operator=(ScopedCleanup&&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
