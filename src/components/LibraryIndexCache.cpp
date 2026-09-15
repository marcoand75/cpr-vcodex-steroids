#include "LibraryIndexCache.h"

#include "components/LibraryIndex.h"
#include "StoreManager.h"
#include "Logging.h"

#include <cstring>
#include <new>

namespace {

// Conservative heap ceiling for cache allocation on ESP32-C3.
// Keep well below reported 114676 max alloc / ~129 KB free to avoid OOM
// during later render/input allocations.
constexpr size_t kCacheHeapCeiling = 80 * 1024;  // 80 KB

// RAM cache state for idx_mixed.
static LibraryIndex::IndexRec* g_mixedData = nullptr;
static int g_mixedCount = 0;

}  // namespace

// ---- Mixed index cache ------------------------------------------------------

bool IndexCacheManager::hasMixedIndex() {
  return g_mixedData != nullptr && g_mixedCount > 0;
}

bool IndexCacheManager::loadMixedIndex() {
  if (hasMixedIndex()) {
    return true;
  }

  const char* kIdxMixed = "/.crosspoint/LIBRARY/idx_mixed.bin";
  HalFile f = Storage.open(kIdxMixed);
  if (!f) {
    LOG_DBG("LIB-IDX-CACHE", "loadMixedIndex: file missing");
    return false;
  }

  const size_t fileSize = static_cast<size_t>(f.size());
  const size_t recSize = sizeof(LibraryIndex::IndexRec);
  if (fileSize == 0 || fileSize % recSize != 0) {
    LOG_ERR("LIB-IDX-CACHE", "loadMixedIndex: bad size %u", (unsigned)fileSize);
    f.close();
    return false;
  }

  const int total = static_cast<int>(fileSize / recSize);
  if (total > kMaxMixedEntries) {
    LOG_ERR("LIB-IDX-CACHE", "loadMixedIndex: %d entries exceeds max %d", total, kMaxMixedEntries);
    f.close();
    return false;
  }

  if (!canAllocate(fileSize)) {
    LOG_ERR("LIB-IDX-CACHE", "loadMixedIndex: skipped, not enough heap (%u bytes)", (unsigned)fileSize);
    f.close();
    return false;
  }

  // Use a single heap allocation for the whole index. This avoids repeated
  // small allocations and keeps the cache compact.
  uint8_t* raw = nullptr;
  if (total > 0) {
    raw = new (std::nothrow) uint8_t[fileSize];
    if (!raw) {
      LOG_ERR("LIB-IDX-CACHE", "loadMixedIndex: alloc failed for %u bytes", (unsigned)fileSize);
      f.close();
      return false;
    }
    if (f.read(raw, static_cast<int>(fileSize)) != static_cast<int>(fileSize)) {
      LOG_ERR("LIB-IDX-CACHE", "loadMixedIndex: read failed");
      delete[] raw;
      f.close();
      return false;
    }
  }

  f.close();

  g_mixedData = reinterpret_cast<LibraryIndex::IndexRec*>(raw);
  g_mixedCount = total;
  LOG_INF("LIB-IDX-CACHE", "loadMixedIndex: cached %d entries (%u bytes)", total, (unsigned)fileSize);
  return true;
}

void IndexCacheManager::invalidateMixed() {
  if (g_mixedData) {
    delete[] reinterpret_cast<uint8_t*>(g_mixedData);
    g_mixedData = nullptr;
    g_mixedCount = 0;
    LOG_DBG("LIB-IDX-CACHE", "invalidateMixed: released");
  }
}

int IndexCacheManager::mixedIndexTotal() {
  return g_mixedCount;
}

const LibraryIndex::IndexRec* IndexCacheManager::mixedIndexData() {
  return g_mixedData;
}

int IndexCacheManager::mixedIndexFind(const char* sortKey) {
  if (!hasMixedIndex() || !sortKey) {
    return -1;
  }

  int lo = 0;
  int hi = g_mixedCount;
  while (lo < hi) {
    const int mid = lo + (hi - lo) / 2;
    const int cmp = std::memcmp(g_mixedData[mid].sortKey, sortKey, sizeof(g_mixedData[mid].sortKey));
    if (cmp < 0) {
      lo = mid + 1;
    } else if (cmp > 0) {
      hi = mid;
    } else {
      return mid;
    }
  }
  return -(lo + 1);
}

bool IndexCacheManager::canAllocate(size_t needed) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();

  // If we cannot even satisfy the allocation at the raw heap level, fail fast.
  if (freeHeap < needed) {
    return false;
  }

  // Reserve headroom so we do not consume the entire free pool.
  if (needed > kCacheHeapCeiling) {
    return false;
  }

  // Be conservative on fragmentation: if max alloc is already small, do not
  // risk making the heap unusable for later render buffers.
  if (maxAlloc < 32 * 1024 && needed > 16 * 1024) {
    return false;
  }

  return true;
}

void IndexCacheManager::releaseAll() {
  invalidateMixed();
}
