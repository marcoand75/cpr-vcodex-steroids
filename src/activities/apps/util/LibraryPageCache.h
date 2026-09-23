#pragma once

#include "components/LibraryIndex.h"

class LibraryPageCache {
 public:
  // Query the current page from LibraryIndex using the active mode.
  // Returns the number of valid slots written into pageCache.
  static int queryForCurrentMode(LibraryIndex::BookRef* pageCache, int curPage, int gridsPerPage,
                                 const char* searchText, int filterMode, int sortMode, int coverWidth, int coverHeight,
                                 bool collectionsMode, bool mixedMode, int currentCollectionIdx);

  // Same as queryForCurrentMode but for the last available page.
  // Used as a fallback when the current page returns zero slots.
  static int queryForCurrentModeFallback(LibraryIndex::BookRef* pageCache, int gridsPerPage, const char* searchText,
                                         int filterMode, int sortMode, int coverWidth, int coverHeight,
                                         bool collectionsMode, bool mixedMode, int currentCollectionIdx,
                                         int totalBooks);

  // Total matching items for the active mode.
  static int totalForMode(bool collectionsMode, bool mixedMode, const char* searchText, int filterMode,
                          int currentCollectionIdx);
};
