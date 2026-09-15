#pragma once

#include <cstdint>
#include <cstddef>

#include "components/LibraryIndex.h"

// =============================================================================
// IndexCacheManager: RAM-backed cache for LibraryIndex binary files.
//
// Goal: remove repeated SD seeks/reads from hot paths like queryMixed() and
// totalMixedMatching(). The first index migrated is idx_mixed because it is
// the most expensive and already the default view in current logs.
//
// Strategy:
//   - One cache slot per index file.
//   - Read-only after load; invalidate on scan/rebuild/mutation.
//   - Small fixed overhead + exact payload size; caller-visible contract is:
//       hasXxx() ? use cached data : fall back to SD path.
// =============================================================================

class IndexCacheManager {
 public:
  // Maximum cached entries per index. Chosen conservatively for ESP32-C3 RAM.
  static constexpr int kMaxMixedEntries = 4000;  // 4000 * 28 byte = 112 KB
  static constexpr int kMaxCollectionsEntries = 2000;  // 2000 * 92 byte = 184 KB

  // ---- idx_mixed cache ------------------------------------------------------
  static bool loadMixedIndex();
  static void invalidateMixed();
  static bool hasMixedIndex();
  static int mixedIndexTotal();
  static const LibraryIndex::IndexRec* mixedIndexData();
  static int mixedIndexFind(const char* sortKey);
  static size_t mixedIndexBytes();

  // ---- idx_collections cache ------------------------------------------------
  static bool loadCollectionsIndex();
  static void invalidateCollections();
  static bool hasCollectionsIndex();
  static int collectionsIndexTotal();
  static const LibraryIndex::CollectionIndexRec* collectionsIndexData();
  static size_t collectionsIndexBytes();

  // RAM accounting.
  static size_t totalCachedBytes();

  // Generic memory check: true if we can still allocate `needed` bytes without
  // exceeding a conservative heap ceiling below the ESP32-C3 safe limit.
  static bool canAllocate(size_t needed);

  // Release all cached indices regardless of state.
  static void releaseAll();
};
