#include "LibraryPageCache.h"

#include "components/LibraryIndex.h"

#include <Logging.h>

namespace {

int queryCurrentPage(LibraryIndex::BookRef* pageCache, int curPage, int gridsPerPage, const char* searchText,
                     int filterMode, int sortMode, int coverWidth, int coverHeight, bool collectionsMode,
                     bool mixedMode, int currentCollectionIdx) {
  if (mixedMode && currentCollectionIdx < 0) {
    const int count = LibraryIndex::queryMixed(pageCache, curPage, gridsPerPage, searchText,
                                    static_cast<LibraryIndex::FilterMode>(filterMode), coverWidth, coverHeight,
                                    static_cast<LibraryIndex::SortMode>(sortMode));
    LOG_DBG("LIBIDX", "queryMixed: page=%d count=%d sort=%d filter=%d search=%s", curPage, count, sortMode, filterMode, searchText ? searchText : "");
    return count;
  }
  if (collectionsMode && currentCollectionIdx < 0) {
    const int count = LibraryIndex::queryCollections(pageCache, curPage, gridsPerPage, coverWidth, coverHeight);
    LOG_DBG("LIBIDX", "queryCollections: page=%d count=%d", curPage, count);
    return count;
  }
  if (currentCollectionIdx >= 0) {
    const int count = LibraryIndex::queryCollectionBooks(pageCache, curPage, gridsPerPage, currentCollectionIdx);
    LOG_DBG("LIBIDX", "queryCollectionBooks: page=%d count=%d collIdx=%d", curPage, count, currentCollectionIdx);
    return count;
  }
  const int count = LibraryIndex::queryPage(pageCache, curPage, gridsPerPage, static_cast<LibraryIndex::SortMode>(sortMode),
                                   searchText, static_cast<LibraryIndex::FilterMode>(filterMode), coverWidth,
                                   coverHeight);
  LOG_DBG("LIBIDX", "queryPage: page=%d count=%d sort=%d filter=%d search=%s", curPage, count, sortMode, filterMode, searchText ? searchText : "");
  return count;
}

int totalForMode(bool collectionsMode, bool mixedMode, const char* searchText, int filterMode,
                 int currentCollectionIdx) {
  int total = 0;
  if (currentCollectionIdx >= 0) {
    total = LibraryIndex::collectionBookCount(currentCollectionIdx);
  } else if (collectionsMode) {
    total = LibraryIndex::totalCollections();
  } else if (mixedMode) {
    total = LibraryIndex::totalMixedMatching(searchText, static_cast<LibraryIndex::FilterMode>(filterMode));
  } else {
    total = LibraryIndex::totalMatching(searchText, static_cast<LibraryIndex::FilterMode>(filterMode));
  }
  LOG_DBG("LIBIDX", "totalForMode: total=%d collMode=%d mixMode=%d filter=%d search=%s", total, collectionsMode ? 1 : 0, mixedMode ? 1 : 0, filterMode, searchText ? searchText : "");
  return total;
}

}  // namespace

int LibraryPageCache::queryForCurrentMode(LibraryIndex::BookRef* pageCache, int curPage, int gridsPerPage,
                                          const char* searchText, int filterMode, int sortMode, int coverWidth,
                                          int coverHeight, bool collectionsMode, bool mixedMode,
                                          int currentCollectionIdx) {
  return queryCurrentPage(pageCache, curPage, gridsPerPage, searchText, filterMode, sortMode, coverWidth, coverHeight,
                          collectionsMode, mixedMode, currentCollectionIdx);
}

int LibraryPageCache::queryForCurrentModeFallback(LibraryIndex::BookRef* pageCache, int gridsPerPage,
                                                  const char* searchText, int filterMode, int sortMode, int coverWidth,
                                                  int coverHeight, bool collectionsMode, bool mixedMode,
                                                  int currentCollectionIdx, int totalBooks) {
  const int lastPage = std::max(0, (totalBooks + gridsPerPage - 1) / gridsPerPage - 1);
  return queryCurrentPage(pageCache, lastPage, gridsPerPage, searchText, filterMode, sortMode, coverWidth, coverHeight,
                          collectionsMode, mixedMode, currentCollectionIdx);
}

int LibraryPageCache::totalForMode(bool collectionsMode, bool mixedMode, const char* searchText, int filterMode,
                                   int currentCollectionIdx) {
  if (currentCollectionIdx >= 0) {
    return LibraryIndex::collectionBookCount(currentCollectionIdx);
  }
  if (collectionsMode) {
    return LibraryIndex::totalCollections();
  }
  if (mixedMode) {
    return LibraryIndex::totalMixedMatching(searchText, static_cast<LibraryIndex::FilterMode>(filterMode));
  }
  return LibraryIndex::totalMatching(searchText, static_cast<LibraryIndex::FilterMode>(filterMode));
}
