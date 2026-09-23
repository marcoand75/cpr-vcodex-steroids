#include "LibraryPageCache.h"

#include "components/LibraryIndex.h"

namespace {

int queryCurrentPage(LibraryIndex::BookRef* pageCache, int curPage, int gridsPerPage, const char* searchText,
                     int filterMode, int sortMode, int coverWidth, int coverHeight, bool collectionsMode,
                     bool mixedMode, int currentCollectionIdx) {
  if (mixedMode && currentCollectionIdx < 0) {
    return LibraryIndex::queryMixed(pageCache, curPage, gridsPerPage, searchText,
                                    static_cast<LibraryIndex::FilterMode>(filterMode), coverWidth, coverHeight,
                                    static_cast<LibraryIndex::SortMode>(sortMode));
  }
  if (collectionsMode && currentCollectionIdx < 0) {
    return LibraryIndex::queryCollections(pageCache, curPage, gridsPerPage, coverWidth, coverHeight);
  }
  if (currentCollectionIdx >= 0) {
    return LibraryIndex::queryCollectionBooks(pageCache, curPage, gridsPerPage, currentCollectionIdx);
  }
  return LibraryIndex::queryPage(pageCache, curPage, gridsPerPage, static_cast<LibraryIndex::SortMode>(sortMode),
                                 searchText, static_cast<LibraryIndex::FilterMode>(filterMode), coverWidth,
                                 coverHeight);
}

int totalForMode(bool collectionsMode, bool mixedMode, const char* searchText, int filterMode,
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
