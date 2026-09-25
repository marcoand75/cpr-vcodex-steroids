#include "LibraryActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Xtc.h>
#include <ZipFile.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

#include "../home/BookContextMenuActivity.h"
#include "../util/ConfirmationActivity.h"
#include "../util/KeyboardEntryActivity.h"
#include "CollectionManageActivity.h"
#include "CollectionPickerActivity.h"
#include "CrossPointSettings.h"
#include "FavoritesStore.h"
#include "HiddenBooksStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "SilentRestart.h"
#include "UserCollectionsStore.h"
#include "activities/apps/util/LibraryNavigation.h"
#include "components/LibraryCache.h"
#include "components/LibraryIndex.h"
#include "components/LibraryIndexCache.h"
#include "components/icons/bookshelf.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/cover.h"
#include "components/icons/heart.h"
#include "components/icons/heart24.h"
#include "components/icons/library.h"
#include "components/icons/library_new.h"
#include "components/icons/recentbooks.h"
#include "components/icons/search_minus.h"
#include "components/icons/search_plus.h"
#include "components/icons/settings2.h"
#include "components/icons/sort_asc.h"
#include "components/icons/sort_desc.h"
#include "util/BookFilter.h"
#include "util/FsFileCompat.h"
#include "util/LibraryPerfLog.h"
#include "util/PopupUtils.h"
#include "util/StringUtils.h"

bool LibraryActivity::forceScanOnNextOpen_ = false;
bool LibraryActivity::forceRebuildOnNextOpen_ = false;
#include "../util/ListRenderHelper.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/apps/util/LibraryDrawHelpers.h"
#include "activities/apps/util/LibraryPageCache.h"
#include "components/LibraryPopupOverlay.h"
#include "components/UITheme.h"
#include "components/icons/text24.h"
#include "components/icons/time_fast.h"
#include "components/icons/transfer.h"
#include "fontIds.h"
#include "util/LibraryCoverHelper.h"

void LibraryActivity::deleteBookFile(const std::string& bookPath) {
  // Permanently delete the book file + its rendering cache + cover thumb.
  // Reading stats, bookmarks and clippings are NOT affected.
  if (bookPath.empty() || !Storage.exists(bookPath.c_str())) return;
  LOG_DBG("LIB", "DelBook: %s", bookPath.c_str());

  // 1. Remove the book file
  Storage.remove(bookPath.c_str());

  // 2. Remove the per-book cache directory (epub_<hash> or xtc_<hash>)
  if (FsHelpers::hasEpubExtension(bookPath) || FsHelpers::hasXtcExtension(bookPath)) {
    char cacheDir[64];
    if (FsHelpers::hasEpubExtension(bookPath)) {
      // EPUB caches under FNV-1a 64-bit (see Epub::cachePathForFilePath).
      const uint64_t hash = ZipFile::fnvHash64(bookPath.c_str(), bookPath.size());
      snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", static_cast<unsigned long long>(hash));
    } else {
      // Xtc caches under std::hash (see Xtc.h).
      const unsigned long long hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
      snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/xtc_%llu", hash);
    }
    if (Storage.exists(cacheDir)) {
      Storage.rmdir(cacheDir);
      LOG_DBG("LIB", "DelBook: removed cache dir %s", cacheDir);
    }
  }

  // 3. Remove cover thumbnail
  std::string thumbPath = LibraryIndex::thumbPathFor(bookPath, coverWidth_, coverHeight_);
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
  }

  // 4. Remove from hidden books, recents, favourites, and user collections
  HIDDEN_BOOKS.removeBook(bookPath);
  FAVORITES.removeBook(bookPath);
  RECENT_BOOKS.removeBook(bookPath);
  LibraryIndex::removeBookFromAllCollectionsByPath(bookPath.c_str());

  // 5. Refresh library view with feedback
  PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
  LibraryIndex::scan(renderer, Rect(), SETTINGS.libraryRootDir);
  LibraryIndex::buildIndices();
  applyFilterAndSort();
}

// ============================================================================
// SECTION 2: Layout & lifecycle
// ============================================================================

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2;
      coverWidth_ = 202;
      coverHeight_ = 306;
      gap_ = 13;
      break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3;
      coverWidth_ = 130;
      coverHeight_ = 190;
      gap_ = 13;
      break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4;
      coverWidth_ = 100;
      coverHeight_ = 150;
      gap_ = 7;
      break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
  rowPad_ = (gridColumns_ >= 4) ? 8 : 14;
  pageTitleCacheKey_ = -1;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("LIB", "onEnter: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  LibraryPerf::ScopedTimer totalTimer("onEnter_total");

  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  USER_COLLECTIONS.ensureLoaded();
  LibraryPerf::logElapsed("onEnter_stores_ensureLoaded", totalTimer.start);

  // If we returned from collection management, rebuild indices now so the
  // library grid reflects added/removed/renamed collections before we render.
  if (pendingCollectionsRebuild_) {
    LibraryPerf::ScopedTimer rebuildTimer("onEnter_pendingRebuild");
    if (USER_COLLECTIONS.generation() != lastUserCollectionsGeneration_) {
      PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
      LibraryIndex::buildCollectionsIndex();
      LibraryIndex::buildMixedIndex(static_cast<LibraryIndex::SortMode>(currentSort_));
      IndexCacheManager::invalidateMixed();
      IndexCacheManager::invalidateCollections();
    }
    lastUserCollectionsGeneration_ = USER_COLLECTIONS.generation();
    pendingCollectionsRebuild_ = false;
  } else {
    lastUserCollectionsGeneration_ = USER_COLLECTIONS.generation();
  }
  LibraryPerf::logElapsed("onEnter_afterRebuild", totalTimer.start);

  // Preload the best installed SD CJK family so non-Latin titles render with
  // glyphs everywhere in the grid/header (no-op when no CJK font is installed).
  sdFontSystem.ensureCjkFontLoaded(renderer);
  LibraryPerf::logElapsed("onEnter_afterCjkFont", totalTimer.start);

  // Drop any page frames cached by a previous session (index/state may differ).
  clearPageFrameCache();
  bumpLibEpoch();
  lastRenderedPage_ = -1;
  lastRenderedSelectorIndex_ = -1;
  lastFrameHitPage_ = -1;
  LibraryPerf::logElapsed("onEnter_afterClearFrameCache", totalTimer.start);

  applyLayoutFromSettings();
  LibraryPerf::logElapsed("onEnter_afterLayout", totalTimer.start);
  selectorIndex_ = 0;
  lastRenderedPage_ = -1;
  forceRender_ = true;
  popupMode_ = PopupMode::None;
  upPress_.reset();
  downPress_.reset();
  popupSpawnButton_ = -1;
  lastLayoutSetting_ = SETTINGS.libraryLayout;
  prevBorderIdx_ = -1;

  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;

  {
    LibraryPerf::ScopedTimer scanTimer("onEnter_scanSd");
    scanSd();
  }
  LibraryPerf::logElapsed("onEnter_afterScanSd", totalTimer.start);
  IndexCacheManager::loadMixedIndex();
  IndexCacheManager::loadCollectionsIndex();
  LOG_DBG("LIB", "onEnter: mixedTotal=%d collTotal=%d totalBooks=%d", LibraryIndex::totalMixed(), LibraryIndex::totalCollections(), totalBooks_);

  // Restore saved UI state: selector position and opened collection.
  if (SETTINGS.librarySelectorIndex >= 0 && SETTINGS.librarySelectorIndex < totalBooks_) {
    selectorIndex_ = SETTINGS.librarySelectorIndex;
  } else {
    selectorIndex_ = 0;
  }
  if (SETTINGS.libraryCollectionIdx >= 0 && SETTINGS.libraryCollectionName[0] != '\0') {
    USER_COLLECTIONS.ensureLoaded();
    const UserCollection* uc = USER_COLLECTIONS.findCollectionByName(SETTINGS.libraryCollectionName);
    if (uc) {
      currentCollectionIdx_ = SETTINGS.libraryCollectionIdx;
      currentCollectionName_ = SETTINGS.libraryCollectionName;
      currentCollectionIsUser_ = true;
      selectorIndex_ = 0;
    } else {
      SETTINGS.libraryCollectionIdx = -1;
      SETTINGS.libraryCollectionName[0] = '\0';
    }
  }
  if (currentCollectionIdx_ >= 0) {
    refreshTotalCountsFromCurrentMode();
  }
  LibraryPerf::logElapsed("onEnter_afterRestoreState", totalTimer.start);

  // Ensure page cache matches the restored selector position.
  {
    LibraryPerf::ScopedTimer refreshTimer("onEnter_refreshPageCache_1");
    refreshPageCache();
  }
  LibraryPerf::logElapsed("onEnter_afterFirstRefresh", totalTimer.start);

  // If indices were rebuilt above, refresh totals and page cache now.
  if (pendingCollectionsRebuild_) {
    refreshTotalCountsFromCurrentMode();
    {
      LibraryPerf::ScopedTimer refreshTimer("onEnter_refreshPageCache_2");
      refreshPageCache();
    }
    LibraryPerf::logElapsed("onEnter_afterRebuildRefresh", totalTimer.start);
  }

  // If we came back from collection-management UI, try to return to the same
  // page/selection instead of always resetting to the first item.
  if (selectorBeforeManage_ >= 0) {
    selectorIndex_ = selectorBeforeManage_;
    if (selectorIndex_ >= totalBooks_) {
      selectorIndex_ = std::max(0, totalBooks_ - 1);
    }
    {
      LibraryPerf::ScopedTimer refreshTimer("onEnter_refreshPageCache_3");
      refreshPageCache();
    }
    LibraryPerf::logElapsed("onEnter_afterManageRefresh", totalTimer.start);
    selectorBeforeManage_ = -1;
  }

  LOG_DBG("LIB", "onEnter: after scanSd heap=%u maxA=%u total=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          totalBooks_);
  LOG_DBG("LIB", "onEnter: final selector=%d page=%d/%d view=%d filter=%d sort=%d collIdx=%d collName=%s", selectorIndex_, lastPage_, totalPages_, (int)viewMode_, (int)currentFilter_, (int)currentSort_, currentCollectionIdx_, currentCollectionName_.c_str());
  LOG_DBG("LIB", "onEnter: pageCache first page paths:");
  for (int i = 0; i < gridsPerPage_ && i < 12; ++i) {
    if (pageCache_[i].id == 0) break;
    const std::string thumb = LibraryIndex::thumbPathFor(pageCache_[i].path, coverWidth_, coverHeight_);
    LOG_DBG("LIB", "onEnter: slot=%d id=%u path=%s thumb=%s", i, (unsigned)pageCache_[i].id, pageCache_[i].path, thumb.c_str());
  }
  LOG_DBG("LIB", "onEnter: heap=%u maxA=%u total=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), totalBooks_);
  requestUpdate();
}

void LibraryActivity::ensureLayoutUpToDate() {
  if (SETTINGS.libraryLayout != lastLayoutSetting_) {
    applyLayoutFromSettings();
    lastLayoutSetting_ = SETTINGS.libraryLayout;
    forceRender_ = true;
  }
}

void LibraryActivity::onExit() {
  Activity::onExit();
  // Save current library UI state so it can be restored on next enter.
  SETTINGS.libraryViewMode = static_cast<uint8_t>(viewMode_);
  SETTINGS.libraryFilter = static_cast<uint8_t>(currentFilter_);
  SETTINGS.librarySort = static_cast<uint8_t>(currentSort_);
  StringUtils::copyToFixedBuffer(SETTINGS.librarySearchText, sizeof(SETTINGS.librarySearchText), currentSearchText_);
  SETTINGS.librarySelectorIndex = selectorIndex_;
  SETTINGS.libraryCollectionIdx = (currentCollectionIdx_ >= 0) ? currentCollectionIdx_ : -1;
  if (currentCollectionIdx_ >= 0 && !currentCollectionName_.empty()) {
    StringUtils::copyToFixedBuffer(SETTINGS.libraryCollectionName, sizeof(SETTINGS.libraryCollectionName),
                                   currentCollectionName_);
  } else {
    SETTINGS.libraryCollectionName[0] = '\0';
  }
  SETTINGS.saveToFile();
  pageTitleCache_.clear();
  pageTitleCacheKey_ = -1;
  cachedTotalBooks_ = -1;
}

void LibraryActivity::freeBackgroundMemory() {
  pageTitleCache_.clear();
  std::vector<std::vector<std::string>>().swap(pageTitleCache_);
  pageTitleCacheKey_ = -1;
  cachedRenderSelector_ = -1;
  cachedRenderPage_ = -1;
  cachedTotalBooks_ = -1;
  cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
  cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
  cachedInfoSearch_.clear();
  cachedCollectionsMode_ = false;
  cachedCollectionIdx_ = -2;
  cachedCollectionName_.clear();
}

// ============================================================================
// SECTION 3: Data pipeline — scan, filter, sort
// ============================================================================

void LibraryActivity::scanSd() {
  LibraryPerf::ScopedTimer totalTimer("scanSd_total");
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;
  viewMode_ = LibraryViewMode::Flat;
  if (SETTINGS.libraryViewMode == 1) viewMode_ = LibraryViewMode::Collections;
  if (SETTINGS.libraryViewMode == 2) viewMode_ = LibraryViewMode::Mixed;
  sortModeBeforeSearch_ = currentSort_;
  viewModeBeforeSearch_ = viewMode_;
  collectionsMode_ = (viewMode_ == LibraryViewMode::Collections);
  mixedMode_ = (viewMode_ == LibraryViewMode::Mixed);
  LOG_DBG("LIB", "scanSd: view=%d filter=%d sort=%d collMode=%d mixMode=%d collIdx=%d", (int)viewMode_, (int)currentFilter_, (int)currentSort_, collectionsMode_, mixedMode_, currentCollectionIdx_);
  if (collectionsMode_) {
    currentCollectionIdx_ = -1;
    currentCollectionIsUser_ = false;
  }
  LibraryPerf::logElapsed("scanSd_afterViewSetup", totalTimer.start);

  // Init LibraryIndex if needed
  LibraryIndex::init();

  if (!LibraryIndex::exists()) {
    // Cold path: full scan with progress popup — always needed when no index exists
    renderer.clearScreen();
    Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
    GUI.fillPopupProgress(renderer, popupRect, 0);
    renderer.displayBuffer();

    {
      LibraryPerf::ScopedTimer scanTimer("scanSd_cold_scan");
      LibraryIndex::scan(renderer, popupRect, SETTINGS.libraryRootDir);
    }
    LibraryPerf::logElapsed("scanSd_cold_afterScan", totalTimer.start);
    {
      LibraryPerf::ScopedTimer buildTimer("scanSd_cold_build");
      LibraryIndex::buildCollectionsIndex();
      LibraryIndex::buildIndices();
      IndexCacheManager::invalidateMixed();
      IndexCacheManager::invalidateCollections();
    }
    LibraryPerf::logElapsed("scanSd_cold_afterBuild", totalTimer.start);
    clearPageFrameCache();  // library contents changed -> all frames stale
    bumpLibEpoch();
    lastRenderedPage_ = -1;
    lastRenderedSelectorIndex_ = -1;
    lastFrameHitPage_ = -1;
    refreshTotalCountsFromCurrentMode();
    LOG_DBG("LIB", "scanSd:cold totalBooks=%d grids=%d collMode=%d mixMode=%d", totalBooks_, gridsPerPage_, collectionsMode_, mixedMode_);
    LibraryPerf::logElapsed("scanSd_cold_afterCounts", totalTimer.start);
    {
      LibraryPerf::ScopedTimer refreshTimer("scanSd_cold_refreshPageCache");
      refreshPageCache();
    }
    LibraryPerf::logElapsed("scanSd_cold_end", totalTimer.start);
    LOG_DBG("LIB", "scanSd:cold pageCache first page paths:");
    for (int i = 0; i < gridsPerPage_ && i < 12; ++i) {
      if (pageCache_[i].id == 0) break;
      const std::string thumb = LibraryIndex::thumbPathFor(pageCache_[i].path, coverWidth_, coverHeight_);
      LOG_DBG("LIB", "scanSd:cold slot=%d id=%u path=%s thumb=%s", i, (unsigned)pageCache_[i].id, pageCache_[i].path, thumb.c_str());
    }
    LOG_DBG("LIB", "scanSd:cold totalBooks=%d grids=%d collMode=%d mixMode=%d", totalBooks_, gridsPerPage_, collectionsMode_, mixedMode_);
    return;
  }

  // Fast path: existing library.dat
  // Decide whether to perform SD scan based on:
  //   - forceScanOnNextOpen_ (set by "Update & Open" popup)
  //   - libraryUpdateMode == AUTO
  // Decide whether to perform SD scan based on:
  //   - forceScanOnNextOpen_ (set by "Update & Open" popup)
  //   - libraryUpdateMode == AUTO
  //   - forceRebuildOnNextOpen_ (deferred low-heap rebuild must rescan)
  const bool doScan = forceScanOnNextOpen_ || forceRebuildOnNextOpen_ ||
                      SETTINGS.libraryUpdateMode == CrossPointSettings::LIBRARY_UPDATE_AUTO;
  const bool forceRebuild = forceScanOnNextOpen_ || forceRebuildOnNextOpen_;
  forceScanOnNextOpen_ = false;
  forceRebuildOnNextOpen_ = false;

  if (doScan) {
    int added = 0, removed = 0;
    // For deferred rebuild show the indexing popup with progress bar,
    // matching the cold-scan UX from master Steroids.
    const bool showScanProgress = forceRebuildOnNextOpen_ || !LibraryIndex::exists();
    Rect popupRect;
    if (showScanProgress) {
      renderer.clearScreen();
      popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
      GUI.fillPopupProgress(renderer, popupRect, 0);
      renderer.displayBuffer();
    }
    {
      LibraryPerf::ScopedTimer scanTimer("scanSd_fast_scan");
      LibraryIndex::scan(renderer, popupRect, SETTINGS.libraryRootDir, &added, &removed);
    }
    LibraryPerf::logElapsed("scanSd_fast_afterScan", totalTimer.start);
    if (added > 0 || removed > 0 || forceRebuild) {
      renderer.clearScreen();
      GUI.drawPopup(renderer, tr(STR_UPDATING_LIBRARY));
      renderer.displayBuffer();
      {
        LibraryPerf::ScopedTimer buildTimer("scanSd_fast_build");
        LibraryIndex::buildCollectionsIndex();
        LibraryIndex::buildIndices();
        IndexCacheManager::invalidateMixed();
        IndexCacheManager::invalidateCollections();
      }
      LibraryPerf::logElapsed("scanSd_fast_afterBuild", totalTimer.start);
      clearPageFrameCache();  // library contents changed -> all frames stale
      bumpLibEpoch();
      lastRenderedPage_ = -1;
      lastRenderedSelectorIndex_ = -1;
      lastFrameHitPage_ = -1;
    }
    LOG_DBG("LIB", "scanSd:fast added=%d removed=%d totalBooks=%d grids=%d collMode=%d mixMode=%d", added, removed, totalBooks_, gridsPerPage_, collectionsMode_, mixedMode_);
    LibraryPerf::logElapsed("scanSd_fast_afterCounts", totalTimer.start);
  }

  refreshTotalCountsFromCurrentMode();
  {
    LibraryPerf::ScopedTimer refreshTimer("scanSd_fast_refreshPageCache");
    refreshPageCache();
  }
  LibraryPerf::logElapsed("scanSd_fast_end", totalTimer.start);
  LOG_DBG("LIB", "scanSd:fast pageCache first page paths:");
  for (int i = 0; i < gridsPerPage_ && i < 12; ++i) {
    if (pageCache_[i].id == 0) break;
    const std::string thumb = LibraryIndex::thumbPathFor(pageCache_[i].path, coverWidth_, coverHeight_);
    LOG_DBG("LIB", "scanSd:fast slot=%d id=%u path=%s thumb=%s", i, (unsigned)pageCache_[i].id, pageCache_[i].path, thumb.c_str());
  }
  LOG_DBG("LIB", "scanSd: existing index, doScan=%d total=%d collMode=%d mixMode=%d", static_cast<int>(doScan),
          totalBooks_, collectionsMode_, mixedMode_);
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
  currentFilter_ = filter;
  refreshTotalCountsFromCurrentMode();
  selectorIndex_ = 0;
  refreshPageCache();
}

void LibraryActivity::refreshPageCache() {
  LibraryPerf::ScopedTimer totalTimer("refreshPageCache_total");
  int curPage = selectorIndex_ / gridsPerPage_;
  int slotCount;
  {
    LibraryPerf::ScopedTimer queryTimer("refreshPageCache_query");
    slotCount = LibraryPageCache::queryForCurrentMode(
        pageCache_, curPage, gridsPerPage_, currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
        static_cast<int>(currentFilter_), static_cast<int>(currentSort_), coverWidth_, coverHeight_, collectionsMode_,
        mixedMode_, currentCollectionIdx_);
  }
  LibraryPerf::logElapsed("refreshPageCache_afterQuery", totalTimer.start);
  // If the page had fewer items than requested, update totalBooks_ and
  // retry on the last available page.
  if (slotCount == 0 && curPage > 0) {
    LOG_DBG("LIB", "refreshPageCache: empty page, fallback to lastPage");
    LibraryPerf::ScopedTimer fallbackTimer("refreshPageCache_fallbackLastPage");
    refreshTotalCountsFromCurrentMode();
    int lastPage = std::max(0, totalPages_ - 1);
    selectorIndex_ = lastPage * gridsPerPage_;
    {
      LibraryPerf::ScopedTimer queryTimer("refreshPageCache_fallbackQuery");
      slotCount = LibraryPageCache::queryForCurrentModeFallback(
          pageCache_, gridsPerPage_, currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
          static_cast<int>(currentFilter_), static_cast<int>(currentSort_), coverWidth_, coverHeight_, collectionsMode_,
          mixedMode_, currentCollectionIdx_, totalBooks_);
    }
    LibraryPerf::logElapsed("refreshPageCache_afterFallback", totalTimer.start);
  }
  // Zero out remaining slots
  for (int i = slotCount; i < gridsPerPage_; ++i) {
    pageCache_[i].id = 0;
    pageCache_[i].title[0] = '\0';
    pageCache_[i].path[0] = '\0';
  }
  pageTitleCacheKey_ = -1;
  cachedTotalBooks_ = totalBooks_;
  forceRender_ = true;
  LOG_DBG("LIB", "refreshPageCache: curPage=%d slotCount=%d totalBooks=%d selector=%d", curPage, slotCount, totalBooks_, selectorIndex_);
  for (int i = 0; i < slotCount; ++i) {
    const std::string thumb = LibraryIndex::thumbPathFor(pageCache_[i].path, coverWidth_, coverHeight_);
    LOG_DBG("LIB", "refreshPageCache: slot=%d id=%u path=%s thumb=%s exists=%d", i, (unsigned)pageCache_[i].id, pageCache_[i].path, thumb.c_str(), !thumb.empty() && Storage.exists(thumb.c_str()) ? 1 : 0);
  }
  // Start cover generation for missing covers on the new page.
  coverGen_.active = true;
  coverGen_.slot = 0;
  coverGen_.done = 0;
  coverGen_.total = 0;
  LOG_DBG("LIB", "refreshPageCache: done slotCount=%d selectorIndex_=%d", slotCount, selectorIndex_);
}

void LibraryActivity::refreshTotalCountsFromCurrentMode() {
  totalBooks_ = LibraryPageCache::totalForMode(collectionsMode_, mixedMode_,
                                               currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
                                               static_cast<int>(currentFilter_), currentCollectionIdx_);
  totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
}

void LibraryActivity::applyFilterAndSort() {
  LibraryPerf::ScopedTimer totalTimer("applyFilterAndSort_total");
  collectionsMode_ = (viewMode_ == LibraryViewMode::Collections);
  mixedMode_ = (viewMode_ == LibraryViewMode::Mixed);
  if (!collectionsMode_ && !mixedMode_) lastFlatSort_ = currentSort_;  // remember flat ordering
  if (collectionsMode_ || mixedMode_) {
    currentCollectionIdx_ = -1;
    currentCollectionIsUser_ = false;
  }
  // Keep the search-restore target aligned with the current view/ordem mode.
  if (currentSearchText_.empty()) {
    sortModeBeforeSearch_ = currentSort_;
    viewModeBeforeSearch_ = viewMode_;
  }
  SETTINGS.libraryViewMode = static_cast<uint8_t>(viewMode_);
  SETTINGS.librarySort = static_cast<uint8_t>(currentSort_);
  SETTINGS.libraryFilter = static_cast<uint8_t>(currentFilter_);
  SETTINGS.saveToFile();
  LibraryPerf::logElapsed("applyFilterAndSort_afterSettingsSave", totalTimer.start);

  refreshTotalCountsFromCurrentMode();
  LibraryPerf::logElapsed("applyFilterAndSort_afterCounts", totalTimer.start);
  // Clamp selector to valid range after filter/sort changes
  if (selectorIndex_ >= totalBooks_) {
    selectorIndex_ = totalBooks_ > 0 ? totalBooks_ - 1 : 0;
  }
  pageTitleCacheKey_ = -1;
  cachedRenderSelector_ = -1;
  cachedRenderPage_ = -1;
  cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
  cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
  cachedInfoSearch_.clear();
  cachedCollectionsMode_ = false;
  cachedCollectionIdx_ = -2;
  cachedCollectionName_.clear();
  {
    LibraryPerf::ScopedTimer refreshTimer("applyFilterAndSort_refreshPageCache");
    refreshPageCache();
  }
  LibraryPerf::logElapsed("applyFilterAndSort_afterRefresh", totalTimer.start);
  forceRender_ = true;
  requestUpdate();
}

// ============================================================================
// SECTION 4: Cover generation — one thumb per frame, blocks input
// ============================================================================

bool LibraryActivity::isBookCoverReady(const std::string& path) const {
  const std::string tp = LibraryCache::thumbPathFor(path, coverWidth_, coverHeight_);
  if (tp.empty() || !Storage.exists(tp.c_str())) return false;
  FsFile file;
  if (!Storage.openFileForRead("LIB", tp, file)) {
    Storage.remove(tp.c_str());
    return false;
  }
  if (file.size() == 0) {
    file.close();
    Storage.remove(tp.c_str());
    return false;
  }
  Bitmap bmp(file);
  const auto err = bmp.parseHeaders();
  file.close();
  if (err != BmpReaderError::Ok || bmp.getWidth() <= 0 || bmp.getHeight() <= 0) {
    Storage.remove(tp.c_str());
    return false;
  }
  return true;
}

// ============================================================================
// SECTION 5: Popups — sort / filter / search
// ============================================================================

void LibraryActivity::openSortPopup() {
  popupMode_ = PopupMode::Sort;
  popupOverlay_.title = I18N.get(StrId::STR_LIBRARY_SORT);
  popupOverlay_.items.clear();
  popupOverlay_.selectedIndex = 0;
  popupOverlay_.startIndex = 0;
  upPress_.reset();
  downPress_.reset();

  struct {
    StrId id;
    const uint8_t* icon;
    int iconW;
    int iconH;
    CrossPointSettings::LIBRARY_SORT sort;
  } sorts[] = {
      {StrId::STR_SORT_TITLE_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_ASC},
      {StrId::STR_SORT_TITLE_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_DESC},
      {StrId::STR_SORT_AUTHOR_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC},
      {StrId::STR_SORT_AUTHOR_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC},
  };
  for (size_t i = 0; i < sizeof(sorts) / sizeof(sorts[0]); ++i) {
    PopupItem item;
    item.label = I18N.get(sorts[i].id);
    item.icon = sorts[i].icon;
    item.iconW = sorts[i].iconW;
    item.iconH = sorts[i].iconH;
    item.selected = (currentSort_ == sorts[i].sort);
    popupOverlay_.items.push_back(item);
    if (item.selected) {
      popupOverlay_.selectedIndex = i;
      popupOverlay_.startIndex = std::max(0, static_cast<int>(i) - PanelDrawHelper::kMaxVisibleRows / 2);
    }
  }

  // Search tools live in the sort popup.
  PopupItem searchItem;
  searchItem.label = I18N.get(StrId::STR_SEARCH_LIBRARY);
  searchItem.icon = SearchPlusIcon;
  searchItem.iconW = 32;
  searchItem.iconH = 32;
  searchItem.selected = false;
  popupOverlay_.items.push_back(searchItem);

  PopupItem clearItem;
  clearItem.label = I18N.get(StrId::STR_SEARCH_CLEAR);
  clearItem.icon = SearchMinusIcon;
  clearItem.iconW = 32;
  clearItem.iconH = 32;
  clearItem.selected = false;
  popupOverlay_.items.push_back(clearItem);

  requestUpdate();
}

void LibraryActivity::openFilterPopup() {
  popupMode_ = PopupMode::Filter;
  popupOverlay_.title = I18N.get(StrId::STR_LIBRARY_FILTER);
  popupOverlay_.items.clear();
  popupOverlay_.selectedIndex = 0;
  popupOverlay_.startIndex = 0;
  upPress_.reset();
  downPress_.reset();

  // The whole menu is one-of-many: book filters apply to the flat shelf only;
  // Serie and Serie+Libri are exclusive shelf modes (ring is never duplicated).
  const bool flatMode = (currentSort_ != CrossPointSettings::LIBRARY_SORT_COLLECTIONS &&
                         currentSort_ != CrossPointSettings::LIBRARY_SORT_MIXED);

  PopupItem allItem;
  allItem.label = I18N.get(StrId::STR_ALL_BOOKS);
  allItem.icon = LibraryNewIcon;
  allItem.iconW = 32;
  allItem.iconH = 32;
  allItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL);
  popupOverlay_.items.push_back(allItem);

  PopupItem favItem;
  favItem.label = I18N.get(StrId::STR_FAVOURITES);
  favItem.icon = Heart24Icon;
  favItem.iconW = 24;
  favItem.iconH = 24;
  favItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_FAVOURITES);
  popupOverlay_.items.push_back(favItem);

  PopupItem recentItem;
  recentItem.label = I18N.get(StrId::STR_LATEST_READ);
  recentItem.icon = RecentBooksIcon32;
  recentItem.iconW = 32;
  recentItem.iconH = 32;
  recentItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_LATEST_READ);
  popupOverlay_.items.push_back(recentItem);

  PopupItem unreadItem;
  unreadItem.label = I18N.get(StrId::STR_UNREAD);
  unreadItem.icon = Text24Icon;
  unreadItem.iconW = 24;
  unreadItem.iconH = 24;
  unreadItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_UNREAD);
  popupOverlay_.items.push_back(unreadItem);

  PopupItem completedItem;
  completedItem.label = I18N.get(StrId::STR_COMPLETED);
  completedItem.icon = CleanMonitorIcon32;
  completedItem.iconW = 32;
  completedItem.iconH = 32;
  completedItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_COMPLETED);
  popupOverlay_.items.push_back(completedItem);

  PopupItem hiddenItem;
  hiddenItem.label = I18N.get(StrId::STR_HIDDEN_FILTER);
  hiddenItem.icon = LibraryIcon;
  hiddenItem.iconW = 32;
  hiddenItem.iconH = 32;
  hiddenItem.selected = (flatMode && currentFilter_ == CrossPointSettings::LIBRARY_FILTER_HIDDEN);
  popupOverlay_.items.push_back(hiddenItem);

  // View modes: exclusive shelf grouping (labelled "Serie" / "Serie + Libri").
  PopupItem collItem;
  collItem.label = I18N.get(StrId::STR_SORT_COLLECTIONS);
  collItem.icon = LibraryNewIcon;
  collItem.iconW = 32;
  collItem.iconH = 32;
  collItem.selected = (currentSort_ == CrossPointSettings::LIBRARY_SORT_COLLECTIONS);
  popupOverlay_.items.push_back(collItem);

  PopupItem mixedItem;
  mixedItem.label = I18N.get(StrId::STR_SORT_MIXED);
  mixedItem.icon = LibraryNewIcon;
  mixedItem.iconW = 32;
  mixedItem.iconH = 32;
  mixedItem.selected = (currentSort_ == CrossPointSettings::LIBRARY_SORT_MIXED);
  popupOverlay_.items.push_back(mixedItem);

  PopupItem manageItem;
  manageItem.label = I18N.get(StrId::STR_COLLECTIONS_MANAGE);
  manageItem.icon = Settings2Icon;
  manageItem.iconW = 32;
  manageItem.iconH = 32;
  manageItem.selected = false;
  popupOverlay_.items.push_back(manageItem);

  requestUpdate();
}

void LibraryActivity::closePopup() {
  popupMode_ = PopupMode::None;
  popupSpawnButton_ = -1;
  forceRender_ = true;
  requestUpdate();
}

void LibraryActivity::selectPopupItem() {
  if (popupMode_ == PopupMode::None) return;
  int idx = popupOverlay_.selectedIndex;
  if (idx < 0 || idx >= static_cast<int>(popupOverlay_.items.size())) return;

  if (popupMode_ == PopupMode::Sort) {
    // Popup order: 0=TitleAZ, 1=TitleZA, 2=AuthorAZ, 3=AuthorZA,
    //              4=Search, 5=Clear search
    if (idx == 4) {
      closePopup();
      beginTextSearch();
      return;
    }
    if (idx == 5) {
      PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
      const bool hadSearch = !currentSearchText_.empty();
      currentSearchText_.clear();
      SETTINGS.librarySearchText[0] = '\0';
      if (hadSearch) {
        currentSort_ = sortModeBeforeSearch_;
        viewMode_ = viewModeBeforeSearch_;
        SETTINGS.librarySort = currentSort_;
      }
      SETTINGS.saveToFile();
      applyFilterAndSort();
    } else if (idx >= 0 && idx < 4) {
      PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
      CrossPointSettings::LIBRARY_SORT sorts[] = {
          CrossPointSettings::LIBRARY_SORT_TITLE_ASC,
          CrossPointSettings::LIBRARY_SORT_TITLE_DESC,
          CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC,
          CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC,
      };
      currentSort_ = sorts[idx];
      SETTINGS.librarySort = currentSort_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  } else if (popupMode_ == PopupMode::Filter) {
    // Popup order (one-of-many): 0=All, 1=Favourites, 2=Latest, 3=Unread,
    // 4=Completed, 5=Hidden, 6=Serie (grouped shelf), 7=Serie + Libri (mixed), 8=Manage collections.
    if (idx == 8) {
      closePopup();
      selectorBeforeManage_ = selectorIndex_;
      startActivityForResult(std::make_unique<CollectionManageActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               pendingCollectionsRebuild_ = true;
                               forceRender_ = true;
                               requestUpdate();
                             });
      return;
    } else if (idx == 6) {
      viewMode_ = LibraryViewMode::Collections;
      currentSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
      SETTINGS.librarySort = currentSort_;
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    } else if (idx == 7) {
      viewMode_ = LibraryViewMode::Mixed;
      // Keep current sort mode, default to TITLE_ASC if currently in MIXED
      if (currentSort_ == CrossPointSettings::LIBRARY_SORT_MIXED) {
        currentSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
      }
      SETTINGS.librarySort = currentSort_;
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    } else if (idx >= 0 && idx <= 5) {
      // Book filters only apply to flat view. Switch back to Flat so the
      // selected filter actually takes effect instead of staying hidden
      // inside Collections/Mixed mode.
      viewMode_ = LibraryViewMode::Flat;
      PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
      static const CrossPointSettings::LIBRARY_FILTER kFilters[6] = {
          CrossPointSettings::LIBRARY_FILTER_ALL,         CrossPointSettings::LIBRARY_FILTER_FAVOURITES,
          CrossPointSettings::LIBRARY_FILTER_LATEST_READ, CrossPointSettings::LIBRARY_FILTER_UNREAD,
          CrossPointSettings::LIBRARY_FILTER_COMPLETED,   CrossPointSettings::LIBRARY_FILTER_HIDDEN};
      currentFilter_ = kFilters[idx];
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  }
  closePopup();
}

void LibraryActivity::beginTextSearch() {
  LibraryPerf::ScopedTimer searchTimer("beginTextSearch_total");
  sortModeBeforeSearch_ = currentSort_;
  viewModeBeforeSearch_ = viewMode_;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_LIBRARY), currentSearchText_, 30),
      [this, searchTimer](const ActivityResult& result) {
        LibraryPerf::logElapsed("beginTextSearch_afterKeyboard", searchTimer.start);
        if (result.isCancelled) {
          forceRender_ = true;
          requestUpdate();
          return;
        }
        const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
        if (!kbResult) {
          forceRender_ = true;
          requestUpdate();
          return;
        }
        currentSearchText_ = kbResult->text;
        StringUtils::copyToFixedBuffer(SETTINGS.librarySearchText, sizeof(SETTINGS.librarySearchText),
                                       currentSearchText_);
        SETTINGS.saveToFile();
        PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
        applyFilterAndSort();
        LibraryPerf::logElapsed("beginTextSearch_afterApply", searchTimer.start);
        forceRender_ = true;
        requestUpdate();
      });
}

// ============================================================================
// SECTION 6: Input handling — main loop
// ============================================================================

void LibraryActivity::loop() {
  // ---- User collections rebuild (debounced) ----
  bool collectionsRebuilt = false;
  if (pendingCollectionsRebuild_) {
    USER_COLLECTIONS.ensureLoaded();
    if (USER_COLLECTIONS.generation() != lastUserCollectionsGeneration_) {
      LibraryPerf::ScopedTimer rebuildTimer("loop_collectionsRebuild");
      PopupUtils::showTransientPopup(*this, tr(STR_UPDATING_LIBRARY));
      LibraryIndex::buildCollectionsIndex();
      LibraryIndex::buildMixedIndex(static_cast<LibraryIndex::SortMode>(currentSort_));
      IndexCacheManager::invalidateMixed();
      IndexCacheManager::invalidateCollections();
      lastUserCollectionsGeneration_ = USER_COLLECTIONS.generation();
      clearPageFrameCache();
      bumpLibEpoch();
      lastRenderedPage_ = -1;
      lastRenderedSelectorIndex_ = -1;
      lastFrameHitPage_ = -1;
      collectionsRebuilt = true;
    }
    pendingCollectionsRebuild_ = false;
  }
  if (collectionsRebuilt && (collectionsMode_ || mixedMode_)) {
    refreshTotalCountsFromCurrentMode();
    if (selectorIndex_ >= totalBooks_) {
      selectorIndex_ = std::max(0, totalBooks_ - 1);
    }
    LibraryPerf::ScopedTimer refreshTimer("loop_collectionsRefresh");
    refreshPageCache();
    forceRender_ = true;
    requestUpdate();
  }

  // ---- Cover generation: one slot per frame, after grid is rendered -------
  if (coverGen_.pending) {
    coverGen_.pending = false;
    coverGen_.active = true;
    // Fall through to the generation loop below — coverGen_.slot/Done_/Total_
    // are already set by the callback.
  }
  if (coverGen_.active) {
    const int total = totalBooks_;
    const int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    LibraryPerf::ScopedTimer coverTimer("loop_coverGeneration");

    // First frame: count missing covers, let grid render first
    if (coverGen_.slot == 0 && coverGen_.total == 0) {
      unsigned long t_count = LibraryPerf::nowMs();
      for (int i = 0; i < gridsPerPage_ && (pageStart + i) < total; ++i) {
        if (pageCache_[i].id == 0) continue;
        // Validate the cached cover rather than only checking existence: stale
        // covers from an older cache version exist on disk but render blank, so
        // without this check they are never regenerated and the grid shows blank
        // tiles (no cover, no title). isBookCoverReady removes invalid files.
        if (!isBookCoverReady(std::string(pageCache_[i].path))) {
          ++coverGen_.total;
        }
      }
      LOG_DBG("LIB-PERF", "CovGen-count: total=%d items=%d countMs=%lu", (int)coverGen_.total, (int)gridsPerPage_,
              (unsigned long)(LibraryPerf::nowMs() - t_count));
      if (coverGen_.total == 0) {
        coverGen_.active = false;
        return;
      }
      LOG_DBG("LIB", "CovGen: start %d missing covers on page", coverGen_.total);
      coverGen_.slot = -1;
      requestUpdate();
      return;
    }

    // Second frame onward: process one slot
    if (coverGen_.slot == -1) coverGen_.slot = 0;  // first processing frame

    int slot = coverGen_.slot;
    if (slot < gridsPerPage_ && (pageStart + slot) < total && pageCache_[slot].id != 0) {
      unsigned long t_slot = LibraryPerf::nowMs();
      std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache_[slot].path), coverWidth_, coverHeight_);
      bool needsGenerate = !Storage.exists(thumbPath.c_str());
      LOG_DBG("LIB", "CovGen: slot=%d/%d path=%s thumb=%s exists=%d needsGen=%d", slot, coverGen_.total, pageCache_[slot].path, thumbPath.c_str(), !needsGenerate ? 1 : 0, needsGenerate ? 1 : 0);
      if (!needsGenerate) {
        if (isBookCoverReady(pageCache_[slot].path)) {
          ++coverGen_.slot;
          if (coverGen_.slot >= gridsPerPage_ || (pageStart + coverGen_.slot) >= total) {
            LOG_DBG("LIB", "CovGen: done %d/%d covers generated", coverGen_.done, coverGen_.total);
            coverGen_.active = false;
            coverGen_.slot = 0;
            coverGen_.done = 0;
            coverGen_.total = 0;
            LibraryPerf::logElapsed("loop_coverGeneration_done", coverTimer.start);
            forceRender_ = true;
            requestUpdate();
          }
          return;
        }
        Storage.remove(thumbPath.c_str());
        needsGenerate = true;
      }
      if (needsGenerate) {
        yield();
        esp_task_wdt_reset();
        LOG_DBG("LIB", "CovGen: %d/%d heap=%u maxA=%u", coverGen_.done + 1, coverGen_.total, ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());

        const int savedSelector = selectorIndex_;
        const std::string savedSelTitle = cachedSelTitle_;
        const std::string savedSelAuthor = cachedSelAuthor_;
        selectorIndex_ = pageStart + slot;
        cachedSelTitle_ = pageCache_[slot].title;
        cachedSelAuthor_ = pageCache_[slot].author[0] ? std::string(pageCache_[slot].author) : std::string{};

        forceRender_ = true;
        requestUpdate();

        unsigned long t_gen = LibraryPerf::nowMs();
        bool generated = false;
        // Tell the render task a cover file is being written so it does not
        // probe/remove a partial BMP through pageCoversComplete().
        coverGenWriting_ = true;
        const bool genOk =
            LibraryCoverHelper::generatePageCover(renderer, pageCache_[slot].path, coverWidth_, coverHeight_);
        coverGenWriting_ = false;
        if (genOk) {
          // Verify the generated BMP is actually readable (not partial/corrupt).
          // Without this check a bad flush can leave a corrupt file that
          // drawTile skips, making the cover invisible until a re-enter.
          const std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache_[slot].path), coverWidth_, coverHeight_);
          if (isBookCoverReady(pageCache_[slot].path)) {
            ++coverGen_.done;
            generated = true;
          } else {
            // Remove corrupt/partial file so the loop regenerates it.
            if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
              Storage.remove(thumbPath.c_str());
            }
            LOG_DBG("LIB-PERF", "CovGen-slot: slot=%d generated file invalid, removed for retry", (int)slot);
            generated = false;
          }
        }
        LOG_DBG("LIB-PERF", "CovGen-slot: slot=%d gen=%lums ok=%d", (int)slot,
                (unsigned long)(LibraryPerf::nowMs() - t_gen), (int)generated);

        selectorIndex_ = savedSelector;
        cachedSelTitle_ = savedSelTitle;
        cachedSelAuthor_ = savedSelAuthor;

        forceRender_ = true;
        requestUpdate();
      }
    }

    ++coverGen_.slot;
    if (coverGen_.slot >= gridsPerPage_ || (pageStart + coverGen_.slot) >= total) {
      LOG_DBG("LIB", "CovGen: done %d/%d covers generated pageStart=%d slot=%d total=%d", coverGen_.done, coverGen_.total, pageStart, coverGen_.slot, total);
      coverGen_.active = false;
      coverGen_.slot = 0;
      coverGen_.done = 0;
      coverGen_.total = 0;
      LibraryPerf::logElapsed("loop_coverGeneration_done", coverTimer.start);
      // Force a full render at finish to ensure:
      // - All generated covers appear on screen
      // - The progress text "X/Y Loading..." disappears
      // - Power-save wake renders don't leave stale frames
      forceRender_ = true;
      requestUpdate();
    }
    return;  // block input while generating covers
  }

  // ---- Popup input handling -----------------------------------------------
  if (popupMode_ != PopupMode::None) {
    if (popupSpawnButton_ >= 0 && mappedInput.wasReleased(static_cast<MappedInputManager::Button>(popupSpawnButton_))) {
      popupSpawnButton_ = -1;
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closePopup();
      return;
    }

    int itemCount = static_cast<int>(popupOverlay_.items.size());
    int& sel = popupOverlay_.selectedIndex;
    int& start = popupOverlay_.startIndex;
    int visible = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (sel > 0) {
        sel--;
        if (sel < start) start = sel;
      } else {
        sel = itemCount - 1;
        start = std::max(0, itemCount - visible);
      }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (sel < itemCount - 1) {
        sel++;
        if (sel >= start + visible) start = sel - visible + 1;
      } else {
        sel = 0;
        start = 0;
      }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectPopupItem();
      return;
    }
    return;
  }

  const int total = totalBooks_;
  ensureLayoutUpToDate();

  // ---- Cover generation (blocks input while running) ----------------------
  // Cover generation stays active in the library path: placeholders are shown
  // first, then covers are generated slot-by-slot so the grid remains
  // responsive.  Existing covers from the Home screen reader are still reused
  // when present.
  // --------------------------------------------------------------------------

  // ---- Empty library state ------------------------------------------------
  if (total <= 0) {
    upPress_.reset();
    downPress_.reset();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      upPress_.reset();
      downPress_.reset();
      // Silent restart to reclaim fragmented heap before returning.
      // Library browsing fragments the heap significantly (cache, thumbnails,
      // book index vectors). A full ESP.restart gives the system a clean slate.
      LOG_DBG("LIB", "Back at root: requesting seamless silent restart (free=%d maxA=%d)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      if (launchFromApps) {
        silentRestartToApps();
      } else {
        silentRestartToHome();
      }
      // Unreachable: ESP.restart() above resets the CPU.
      onGoHome();
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
      if (!upPress_.armed()) upPress_.arm();
      if (upPress_.fired(mappedInput.getHeldTime())) {
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Up);
        openSortPopup();
        return;
      }
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Down)) {
      if (!downPress_.armed()) downPress_.arm();
      if (downPress_.fired(mappedInput.getHeldTime())) {
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Down);
        openFilterPopup();
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      upPress_.reset();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      downPress_.reset();
    }
    return;
  }

  // ---- Confirm button — open book or context menu -------------------------
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (total > 0 && selectorIndex_ < total) {
      const unsigned long held = mappedInput.getHeldTime();
      // Long press: context menu (skip for collections/series tiles)
      if (held >= long_press::kDefaultMs) {
        const int idx = selectorIndex_;
        const int slot = idx % gridsPerPage_;
        const std::string path(pageCache_[slot].path);
        if (collectionsMode_ && currentCollectionIdx_ < 0) {
          // Long-press on collection tile: open manage collections
          selectorBeforeManage_ = selectorIndex_;
          startActivityForResult(std::make_unique<CollectionManageActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) {
                                   pendingCollectionsRebuild_ = true;
                                   forceRender_ = true;
                                   requestUpdate();
                                 });
          return;
        }
        if ((collectionsMode_ || mixedMode_) && currentCollectionIdx_ < 0 && (pageCache_[slot].id & 0x80000000u)) {
          // Collection/series tile: no book context menu
          return;
        }
        const std::string title =
            pageCache_[slot].title[0] ? pageCache_[slot].title : book_filter::filenameWithoutExtension(path);
        const bool isEpub = FsHelpers::hasEpubExtension(std::string_view{path.c_str()});
        const bool isFav = FAVORITES.isFavorite(path);
        // Lightweight summary path — does not force the full store into RAM.
        const auto* stats = READING_STATS.getHomeBookStatsForRender("", path);
        const bool isCompleted = stats && stats->completed;
        const bool isHidden = HIDDEN_BOOKS.isHidden(path);
        const bool isInUserCollection = (currentCollectionIdx_ >= 0 && currentCollectionIsUser_);

        startActivityForResult(
            std::make_unique<BookContextMenuActivity>(renderer, mappedInput, title, isFav, isCompleted, isEpub, true,
                                                      isHidden, isInUserCollection),
            [this, idx, slot, path, title, isEpub](const ActivityResult& result) {
              if (result.isCancelled) {
                forceRender_ = true;
                requestUpdate();
                return;
              }
              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) {
                forceRender_ = true;
                requestUpdate();
                return;
              }
              switch (static_cast<BookContextMenuActivity::MenuAction>(menuResult->action)) {
                case BookContextMenuActivity::MenuAction::OPEN_BOOK:
                  onSelectBook(path);
                  return;
                case BookContextMenuActivity::MenuAction::VIEW_STATS:
                  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) {
                                           forceRender_ = true;
                                           requestUpdate();
                                         });
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES:
                  FAVORITES.toggleBook(path);
                  bumpLibEpoch();
                  forceRender_ = true;
                  requestUpdate();
                  return;
                case BookContextMenuActivity::MenuAction::MARK_READ_UNREAD: {
                  const auto* s = READING_STATS.getHomeBookStatsForRender("", path);
                  const bool wasCompleted = s && s->completed;
                  READING_STATS.beginSession(path, title, pageCache_[slot].title[0] ? pageCache_[slot].title : "",
                                             LibraryIndex::thumbPathFor(path, coverWidth_, coverHeight_),
                                             wasCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  bumpLibEpoch();
                  forceRender_ = true;
                  requestUpdate();
                  return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  LibraryCoverHelper::deleteLibraryCovers(path, coverWidth_, coverHeight_);
                  bumpLibEpoch();
                  refreshPageCache();
                  coverGen_.active = false;
                  coverGen_.pending = true;
                  coverGen_.slot = 0;
                  coverGen_.done = 0;
                  coverGen_.total = 0;
                  forceRender_ = true;
                  requestUpdate();
                  return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_LIBRARY_DELETE_PAGE_COVERS),
                                                             tr(STR_LIBRARY_DELETE_PAGE_COVERS_CONFIRM)),
                      [this](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          LibraryCoverHelper::deletePageCovers(gridsPerPage_, pageCache_, coverWidth_, coverHeight_);
                        }
                        bumpLibEpoch();
                        refreshPageCache();
                        // Defer generation by one frame so the grid is drawn
                        // before the generation loop blocks the renderer.
                        coverGen_.active = false;
                        coverGen_.pending = true;
                        coverGen_.slot = 0;
                        coverGen_.done = 0;
                        coverGen_.total = 0;
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_LIBRARY_DELETE_ALL_COVERS),
                                                             tr(STR_LIBRARY_DELETE_ALL_COVERS_CONFIRM)),
                      [this](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          LibraryCoverHelper::deleteAllLibraryCovers(coverWidth_, coverHeight_);
                        }
                        bumpLibEpoch();
                        refreshPageCache();
                        coverGen_.active = false;
                        coverGen_.pending = true;
                        coverGen_.slot = 0;
                        coverGen_.done = 0;
                        coverGen_.total = 0;
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                case BookContextMenuActivity::MenuAction::HIDE_BOOK:
                  HIDDEN_BOOKS.toggleBook(path);
                  // Reload grid and counts: hidden books must disappear / reappear.
                  // Select first available book on the current page.
                  selectorIndex_ = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
                  refreshTotalCountsFromCurrentMode();
                  if (selectorIndex_ >= totalBooks_) selectorIndex_ = 0;
                  bumpLibEpoch();
                  refreshPageCache();
                  forceRender_ = true;
                  requestUpdate();
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_COLLECTION: {
                  const uint32_t bookId = static_cast<uint32_t>(pageCache_[slot].id);
                  selectorBeforeManage_ = selectorIndex_;
                  startActivityForResult(std::make_unique<CollectionPickerActivity>(renderer, mappedInput, bookId),
                                         [this](const ActivityResult&) {
                                           pendingCollectionsRebuild_ = true;
                                           forceRender_ = true;
                                           requestUpdate();
                                         });
                  return;
                }
                case BookContextMenuActivity::MenuAction::REMOVE_FROM_COLLECTION: {
                  if (currentCollectionIdx_ >= 0 && currentCollectionIsUser_) {
                    USER_COLLECTIONS.ensureLoaded();
                    const UserCollection* uc = USER_COLLECTIONS.findCollectionByName(currentCollectionName_.c_str());
                    if (uc) {
                      const uint32_t bookId = static_cast<uint32_t>(pageCache_[slot].id);
                      USER_COLLECTIONS.removeBook(uc->id, bookId);
                      pendingCollectionsRebuild_ = true;
                      forceRender_ = true;
                      requestUpdate();
                    }
                  }
                  return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_BOOK_FILE: {
                  const bool isManual = (SETTINGS.libraryUpdateMode != CrossPointSettings::LIBRARY_UPDATE_AUTO);
                  const char* confirmMsg =
                      isManual ? tr(STR_DELETE_BOOK_FILE_CONFIRM_MANUAL) : tr(STR_DELETE_BOOK_FILE_CONFIRM);
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK_FILE),
                                                             confirmMsg),
                      [this, path](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          deleteBookFile(path);
                          if (SETTINGS.libraryUpdateMode == CrossPointSettings::LIBRARY_UPDATE_AUTO) {
                            // Force re-scan now — the file is gone but the
                            // in-RAM index still has its entry.
                            forceScanOnNextOpen_ = true;
                            scanSd();

                            // Restore saved UI state: selector position and opened collection.
                            if (SETTINGS.librarySelectorIndex >= 0 && SETTINGS.librarySelectorIndex < totalBooks_) {
                              selectorIndex_ = SETTINGS.librarySelectorIndex;
                            } else {
                              selectorIndex_ = 0;
                            }
                            if (SETTINGS.libraryCollectionIdx >= 0 && SETTINGS.libraryCollectionName[0] != '\0') {
                              USER_COLLECTIONS.ensureLoaded();
                              const UserCollection* uc =
                                  USER_COLLECTIONS.findCollectionByName(SETTINGS.libraryCollectionName);
                              if (uc) {
                                currentCollectionIdx_ = SETTINGS.libraryCollectionIdx;
                                currentCollectionName_ = SETTINGS.libraryCollectionName;
                                currentCollectionIsUser_ = true;
                                selectorIndex_ = 0;
                                refreshTotalCountsFromCurrentMode();
                              } else {
                                SETTINGS.libraryCollectionIdx = -1;
                                SETTINGS.libraryCollectionName[0] = '\0';
                              }
                            }
                            selectorIndex_ = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
                            if (selectorIndex_ >= totalBooks_) selectorIndex_ = 0;
                          }
                        }
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                }
                case BookContextMenuActivity::MenuAction::RENAME_BOOK: {
                  const uint32_t bookId = static_cast<uint32_t>(pageCache_[slot].id);
                  const std::string dir = path.substr(0, path.find_last_of('/'));
                  const std::string filename = path.substr(path.find_last_of('/') + 1);
                  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                                             renderer, mappedInput, tr(STR_LIBRARY_RENAME_BOOK), filename, 64),
                                         [this, path, dir, bookId](const ActivityResult& result) {
                                           if (result.isCancelled) {
                                             forceRender_ = true;
                                             requestUpdate();
                                             return;
                                           }
                                           const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
                                           if (!kbResult || kbResult->text.empty()) {
                                             forceRender_ = true;
                                             requestUpdate();
                                             return;
                                           }

                                           // 1. Remove from all user collections before changing identity
                                           LibraryIndex::removeBookFromAllCollections(bookId);

                                           // 2. Build new path in the same directory
                                           std::string newPath = dir;
                                           if (!newPath.empty()) newPath.push_back('/');
                                           newPath.append(kbResult->text);

                                           // 3. Move file on disk
                                           if (Storage.rename(path.c_str(), newPath.c_str())) {
                                             // 4. Update library.dat path
                                             LibraryIndex::updateRecordPath(bookId, newPath.c_str());

                                             // 5. Update path-dependent stores
                                             HIDDEN_BOOKS.removeBook(path);
                                             HIDDEN_BOOKS.addBook(newPath);
                                             FAVORITES.updateBookPath(path, newPath);
                                             RECENT_BOOKS.updateBookPath(path, newPath);

                                             // 6. Refresh UI
                                             refreshPageCache();
                                             forceRender_ = true;
                                             requestUpdate();
                                           }
                                         });
                  return;
                }
                case BookContextMenuActivity::MenuAction::MOVE_BOOK: {
                  const uint32_t bookId = static_cast<uint32_t>(pageCache_[slot].id);
                  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                                                 tr(STR_LIBRARY_MOVE_BOOK), "", 128),
                                         [this, path, bookId](const ActivityResult& result) {
                                           if (result.isCancelled) {
                                             forceRender_ = true;
                                             requestUpdate();
                                             return;
                                           }
                                           const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
                                           if (!kbResult || kbResult->text.empty()) {
                                             forceRender_ = true;
                                             requestUpdate();
                                             return;
                                           }

                                           std::string newPath = kbResult->text;
                                           // If user entered a relative path, make it absolute under the current root
                                           if (!newPath.empty() && newPath[0] != '/') {
                                             newPath = std::string(SETTINGS.libraryRootDir) + "/" + newPath;
                                           }

                                           // 1. Remove from all user collections before changing identity
                                           LibraryIndex::removeBookFromAllCollections(bookId);

                                           // 2. Move file on disk
                                           if (Storage.rename(path.c_str(), newPath.c_str())) {
                                             // 3. Update library.dat path
                                             LibraryIndex::updateRecordPath(bookId, newPath.c_str());

                                             // 4. Update path-dependent stores
                                             HIDDEN_BOOKS.removeBook(path);
                                             HIDDEN_BOOKS.addBook(newPath);
                                             FAVORITES.updateBookPath(path, newPath);
                                             RECENT_BOOKS.updateBookPath(path, newPath);

                                             // 5. Refresh UI
                                             refreshPageCache();
                                             forceRender_ = true;
                                             requestUpdate();
                                           }
                                         });
                  return;
                }
                default:
                  forceRender_ = true;
                  requestUpdate();
                  return;
              }
            });
        return;
      }
      // Short press: open book or enter collection/series
      int slot = selectorIndex_ % gridsPerPage_;
      LibraryNavState navState;
      navState.collectionsMode = collectionsMode_;
      navState.mixedMode = mixedMode_;
      navState.currentCollectionIdx = currentCollectionIdx_;
      navState.currentCollectionName = currentCollectionName_;
      navState.currentCollectionIsUser = currentCollectionIsUser_;
      navState.selectorIndex = selectorIndex_;
      navState.prevSelectorBeforeCollection = prevSelectorBeforeCollection_;
      LibraryNavActionResult action;
      if (LibraryNavigation::handleConfirm(navState, pageCache_[slot], &action) && action.handled) {
        USER_COLLECTIONS.ensureLoaded();
        currentCollectionIsUser_ = (USER_COLLECTIONS.findCollectionByName(action.newCollectionName.c_str()) != nullptr);
        currentCollectionIdx_ = action.newCollectionIdx;
        currentCollectionName_ = std::move(action.newCollectionName);
        prevSelectorBeforeCollection_ = action.newPrevSelectorBeforeCollection;
        selectorIndex_ = action.newSelectorIndex;
        refreshTotalCountsFromCurrentMode();
        refreshPageCache();
        forceRender_ = true;
        requestUpdate();
        return;
      }
      // Guard: collection tiles have empty path; if handleConfirm didn't handle it,
      // don't pass empty path to reader (would open file browser).
      const std::string bookPath(pageCache_[slot].path);
      if (!bookPath.empty()) {
        onSelectBook(bookPath);
      }
      return;
    }
  }

  // ---- Back button --------------------------------------------------------
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    LibraryNavState navState;
    navState.collectionsMode = collectionsMode_;
    navState.mixedMode = mixedMode_;
    navState.currentCollectionIdx = currentCollectionIdx_;
    navState.currentCollectionName = currentCollectionName_;
    navState.currentCollectionIsUser = currentCollectionIsUser_;
    navState.selectorIndex = selectorIndex_;
    navState.prevSelectorBeforeCollection = prevSelectorBeforeCollection_;
    LibraryNavActionResult action;
    if (LibraryNavigation::handleBack(navState, launchFromApps, &action)) {
      if (action.handled) {
        bumpLibEpoch();
        currentCollectionIdx_ = action.newCollectionIdx;
        currentCollectionName_ = std::move(action.newCollectionName);
        currentCollectionIsUser_ = action.newCollectionIsUser;
        prevSelectorBeforeCollection_ = action.newPrevSelectorBeforeCollection;
        refreshTotalCountsFromCurrentMode();
        selectorIndex_ =
            (action.newSelectorIndex >= 0 && action.newSelectorIndex < totalBooks_) ? action.newSelectorIndex : 0;
        refreshPageCache();
        forceRender_ = true;
        requestUpdate();
      }
      return;
    }
    if (upPress_.wasPressed() || downPress_.wasPressed() || leftPress_.wasPressed() || rightPress_.wasPressed()) {
      upPress_.reset();
      downPress_.reset();
      leftPress_.reset();
      rightPress_.reset();
    } else {
      // Persist library UI state before silent restart so it is available on
      // next boot. The normal `onExit()` path is skipped by ESP.restart().
      SETTINGS.libraryViewMode = static_cast<uint8_t>(viewMode_);
      SETTINGS.libraryFilter = static_cast<uint8_t>(currentFilter_);
      SETTINGS.librarySort = static_cast<uint8_t>(currentSort_);
      StringUtils::copyToFixedBuffer(SETTINGS.librarySearchText, sizeof(SETTINGS.librarySearchText),
                                     currentSearchText_);
      if (currentCollectionIdx_ < 0) {
        SETTINGS.librarySelectorIndex = selectorIndex_;
      }
      SETTINGS.libraryCollectionIdx = (currentCollectionIdx_ >= 0) ? currentCollectionIdx_ : -1;
      if (currentCollectionIdx_ >= 0 && !currentCollectionName_.empty()) {
        StringUtils::copyToFixedBuffer(SETTINGS.libraryCollectionName, sizeof(SETTINGS.libraryCollectionName),
                                       currentCollectionName_);
      } else {
        SETTINGS.libraryCollectionName[0] = '\0';
      }
      SETTINGS.saveToFile();
      LOG_DBG("LIB", "Back at root: requesting seamless silent restart (free=%d maxA=%d)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      if (launchFromApps) {
        silentRestartToApps();
      } else {
        silentRestartToHome();
      }
      // Unreachable: ESP.restart() above resets the CPU.
      onGoHome();
    }
    return;
  }

  // ---- Long-press Up/Down to open sort/filter popups ----------------------
  if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
    if (!upPress_.armed()) upPress_.arm();
    if (upPress_.fired(mappedInput.getHeldTime())) {
      popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Up);
      openSortPopup();
      return;
    }
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Down)) {
    if (!downPress_.armed()) downPress_.arm();
    if (downPress_.fired(mappedInput.getHeldTime())) {
      popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Down);
      openFilterPopup();
      return;
    }
  }

  // ---- Long-press Left/Right for page turn ---------------------------------
  if (mappedInput.isPressed(MappedInputManager::Button::Left)) {
    if (!leftPress_.armed()) leftPress_.arm();
    leftPress_.fired(mappedInput.getHeldTime());  // update long-press state for release handler
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Right)) {
    if (!rightPress_.armed()) rightPress_.arm();
    rightPress_.fired(mappedInput.getHeldTime());  // update long-press state for release handler
  }

  // ---- Directional navigation / page turn on long-press release -----------
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upPress_.wasShortPress()) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int r = (selectorIndex_ - ps) / gridColumns_;
      if (r == 0) {
        int prev = ps - gridsPerPage_;
        if (prev < 0) prev = ((total + gridsPerPage_ - 1) / gridsPerPage_ - 1) * gridsPerPage_;
        int prevItems = std::min(gridsPerPage_, total - prev);
        // Align to the last row: find the last occupied row and go to its first column.
        int lastRowStart = prevItems - 1;
        // Clamp to same column if possible, otherwise last column of last row.
        int col = selectorIndex_ % gridColumns_;
        if (col < prevItems % gridColumns_ || prevItems % gridColumns_ == 0) {
          selectorIndex_ = prev + lastRowStart - (lastRowStart % gridColumns_) + col;
        } else {
          selectorIndex_ = prev + lastRowStart;
        }
        // Ensure selector never goes out of bounds
        if (selectorIndex_ >= total) selectorIndex_ = total - 1;
        if (selectorIndex_ < prev) selectorIndex_ = prev;
      } else {
        selectorIndex_ -= gridColumns_;
      }
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) {
        lastPage_ = curPage;
        LibraryPerf::ScopedTimer navTimer("nav_up_pageTurn");
        LOG_DBG("LIB", "Nav: UP page=%d->%d selector=%d total=%d", lastPage_, curPage, selectorIndex_, total);
        forceRender_ = true;
        refreshPageCache();
      }
      requestUpdate();
    }
    upPress_.reset();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downPress_.wasShortPress()) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int pageItems = std::min(gridsPerPage_, total - ps);
      // Ceiling division: 5 items / 4 cols = 2 rows (not 1)
      int rows = (pageItems + gridColumns_ - 1) / gridColumns_;
      int r = (selectorIndex_ - ps) / gridColumns_;
      int nr = selectorIndex_ + gridColumns_;
      if (r >= rows - 1 || nr >= total || nr >= ps + pageItems) {
        int ns = ps + gridsPerPage_;
        if (ns >= total) ns = 0;
        selectorIndex_ = ns;
      } else {
        selectorIndex_ = nr;
      }
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) {
        lastPage_ = curPage;
        LibraryPerf::ScopedTimer navTimer("nav_down_pageTurn");
        LOG_DBG("LIB", "Nav: DOWN page=%d->%d selector=%d total=%d", lastPage_, curPage, selectorIndex_, total);
        forceRender_ = true;
        refreshPageCache();
      }
      requestUpdate();
    }
    downPress_.reset();
  }

  bool moved = false;
  // Left: long-press = previous page, short-press = previous book
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (leftPress_.hasFired()) {
      int prevPage = (selectorIndex_ / gridsPerPage_) - 1;
      if (prevPage < 0) prevPage = (total + gridsPerPage_ - 1) / gridsPerPage_ - 1;
      selectorIndex_ = prevPage * gridsPerPage_;
      if (selectorIndex_ >= total) selectorIndex_ = 0;
      lastPage_ = prevPage;
      LibraryPerf::ScopedTimer navTimer("nav_left_pageTurn");
      LOG_DBG("LIB", "Nav: LEFT pageTurn prevPage=%d selector=%d total=%d", prevPage, selectorIndex_, total);
      forceRender_ = true;
      refreshPageCache();
      requestUpdate();
    } else {
      if (selectorIndex_ > 0) {
        selectorIndex_--;
      } else {
        selectorIndex_ = total - 1;
      }
      moved = true;
    }
    leftPress_.reset();
  }
  // Right: long-press = next page, short-press = next book
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (rightPress_.hasFired()) {
      int nextPage = (selectorIndex_ / gridsPerPage_) + 1;
      int totalPages = (total + gridsPerPage_ - 1) / gridsPerPage_;
      if (nextPage >= totalPages) nextPage = 0;
      selectorIndex_ = nextPage * gridsPerPage_;
      if (selectorIndex_ >= total) selectorIndex_ = 0;
      lastPage_ = nextPage;
      LibraryPerf::ScopedTimer navTimer("nav_right_pageTurn");
      LOG_DBG("LIB", "Nav: RIGHT pageTurn nextPage=%d selector=%d total=%d", nextPage, selectorIndex_, total);
      forceRender_ = true;
      refreshPageCache();
      requestUpdate();
    } else {
      if (selectorIndex_ < total - 1) {
        selectorIndex_++;
      } else {
        selectorIndex_ = 0;
      }
      moved = true;
    }
    rightPress_.reset();
  }
  if (moved) {
    int curPage = selectorIndex_ / gridsPerPage_;
    if (curPage != lastPage_) {
      lastPage_ = curPage;
      LibraryPerf::ScopedTimer navTimer("nav_move_pageTurn");
      LOG_DBG("LIB", "Nav: MOVE pageTurn page=%d selector=%d total=%d", curPage, selectorIndex_, total);
      forceRender_ = true;
      refreshPageCache();
    }
    requestUpdate();
  }
}

// ============================================================================
// SECTION 7: Rendering — full and partial
// ============================================================================

// Common info-line builder used by both the partial and full render paths.
// The 50+ lines of switch/cachedInfo_/cachedSelTitle_/cachedSelAuthor_
// rebuild used to live twice in render(); this is the single source.
bool LibraryActivity::rebuildInfoCacheIfChanged(int curPageRaw, int total) {
  const bool infoKeyChanged = cachedRenderSelector_ != selectorIndex_ || cachedRenderPage_ != curPageRaw ||
                              cachedInfoFilter_ != currentFilter_ || cachedInfoSort_ != currentSort_ ||
                              cachedInfoSearch_ != currentSearchText_ || cachedTotalBooks_ != totalBooks_ ||
                              cachedCollectionsMode_ != collectionsMode_ || cachedMixedMode_ != mixedMode_ ||
                              cachedCollectionIdx_ != currentCollectionIdx_ ||
                              cachedCollectionName_ != currentCollectionName_;
  if (!infoKeyChanged) return false;

  cachedCollectionsMode_ = collectionsMode_;
  cachedMixedMode_ = mixedMode_;
  cachedCollectionIdx_ = currentCollectionIdx_;
  cachedCollectionName_ = currentCollectionName_;

  cachedInfo_.clear();
  switch (currentFilter_) {
    case CrossPointSettings::LIBRARY_FILTER_FAVOURITES:
      cachedInfo_ = tr(STR_FAVOURITES);
      break;
    case CrossPointSettings::LIBRARY_FILTER_LATEST_READ:
      cachedInfo_ = tr(STR_LATEST_READ);
      break;
    case CrossPointSettings::LIBRARY_FILTER_UNREAD:
      cachedInfo_ = tr(STR_UNREAD);
      break;
    case CrossPointSettings::LIBRARY_FILTER_COMPLETED:
      cachedInfo_ = tr(STR_COMPLETED);
      break;
    case CrossPointSettings::LIBRARY_FILTER_HIDDEN:
      cachedInfo_ = tr(STR_HIDDEN_FILTER);
      break;
    default:
      if (collectionsMode_) {
        cachedInfo_ = tr(STR_SORT_COLLECTIONS);
      } else if (mixedMode_) {
        cachedInfo_ = tr(STR_SORT_MIXED);
      } else {
        cachedInfo_ = tr(STR_ALL_BOOKS);
      }
      break;
  }
  if ((collectionsMode_ || mixedMode_) && currentCollectionIdx_ >= 0 && !currentCollectionName_.empty()) {
    cachedInfo_ = currentCollectionName_;
  }
  const char* sortLabel = nullptr;
  if (collectionsMode_ || mixedMode_) {
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:
        sortLabel = tr(STR_SORT_TITLE_ASC);
        break;
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC:
        sortLabel = tr(STR_SORT_TITLE_DESC);
        break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC:
        sortLabel = tr(STR_SORT_AUTHOR_ASC);
        break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC:
        sortLabel = tr(STR_SORT_AUTHOR_DESC);
        break;
      default:
        break;
    }
  } else {
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:
        sortLabel = tr(STR_SORT_TITLE_ASC);
        break;
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC:
        sortLabel = tr(STR_SORT_TITLE_DESC);
        break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC:
        sortLabel = tr(STR_SORT_AUTHOR_ASC);
        break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC:
        sortLabel = tr(STR_SORT_AUTHOR_DESC);
        break;
      default:
        break;
    }
  }
  if (sortLabel && sortLabel[0]) {
    cachedInfo_ += " / ";
    cachedInfo_ += sortLabel;
  }
  if (!currentSearchText_.empty()) {
    cachedInfo_ += " [";
    cachedInfo_ += currentSearchText_.size() > 20 ? currentSearchText_.substr(0, 20) + "..." : currentSearchText_;
    cachedInfo_ += "]";
  }

  const int pageWidth = renderer.getScreenWidth();
  refreshSelectedTitleAuthor(selectorIndex_, total, pageWidth);

  cachedInfoFilter_ = currentFilter_;
  cachedInfoSort_ = currentSort_;
  cachedInfoSearch_ = currentSearchText_;
  cachedRenderSelector_ = selectorIndex_;
  cachedRenderPage_ = curPageRaw;
  cachedCollectionsMode_ = collectionsMode_;
  cachedCollectionIdx_ = currentCollectionIdx_;
  cachedCollectionName_ = currentCollectionName_;
  return true;
}

void LibraryActivity::refreshSelectedTitleAuthor(int selectorIndex, int total, int pageWidth) {
  if (selectorIndex < total) {
    int slot = selectorIndex % gridsPerPage_;
    cachedSelTitle_ = pageCache_[slot].title[0] ? pageCache_[slot].title
                                                : book_filter::filenameWithoutExtension(pageCache_[slot].path);
    cachedSelAuthor_ = pageCache_[slot].author;
    const int maxSelW = pageWidth - 16;  // 8px margin each side
    cachedSelTitle_ = renderer.truncatedText(UI_10_FONT_ID, cachedSelTitle_.c_str(), maxSelW, EpdFontFamily::BOLD);
  } else {
    cachedSelTitle_.clear();
    cachedSelAuthor_.clear();
  }
}

// ============================================================================
// Page-frame cache — full 1-bit framebuffer per rendered page.
// Returning to a page that is already fully rendered (all covers present) loads
// the saved frame instead of re-decoding every cover BMP from SD.
// ============================================================================
namespace {
constexpr const char* kLibFrameDir = "/.crosspoint/libframes";
constexpr uint32_t kFrameMagic = 0x4C46524Du;  // "LFRM"
constexpr int COVER_CORNER_RADIUS = 2;

uint32_t fnv1aByte(uint32_t h, uint8_t v) {
  h ^= v;
  h *= 16777619u;
  return h;
}
}  // namespace

uint32_t LibraryActivity::frameSignature() const {
  uint32_t h = 2166136261u;
  h = fnv1aByte(h, static_cast<uint8_t>(currentSort_));
  h = fnv1aByte(h, static_cast<uint8_t>(currentFilter_));
  h = fnv1aByte(h, collectionsMode_ ? 1u : 0u);
  h = fnv1aByte(h, mixedMode_ ? 1u : 0u);
  const int collByte = (currentCollectionIdx_ < 0 || currentCollectionIdx_ > 255) ? 255 : currentCollectionIdx_;
  h = fnv1aByte(h, static_cast<uint8_t>(collByte));
  for (char c : currentSearchText_) h = fnv1aByte(h, static_cast<uint8_t>(c));
  h = fnv1aByte(h, static_cast<uint8_t>(gridColumns_));
  h = fnv1aByte(h, static_cast<uint8_t>(gridsPerPage_));
  h = fnv1aByte(h, static_cast<uint8_t>(coverWidth_ & 0xFF));
  h = fnv1aByte(h, static_cast<uint8_t>(coverHeight_ & 0xFF));
  h = fnv1aByte(h, static_cast<uint8_t>(libEpoch_ & 0xFF));
  h = fnv1aByte(h, static_cast<uint8_t>((libEpoch_ >> 8) & 0xFF));
  h = fnv1aByte(h, static_cast<uint8_t>((libEpoch_ >> 16) & 0xFF));
  return h;
}

bool LibraryActivity::pageCoversComplete(int pageStart, int pageCount) const {
  // The main task is mid-write on a cover file: isBookCoverReady() below would
  // see a partial BMP and remove the file the writer is using. Treat the page
  // as incomplete so neither frame load nor save touches it.
  if (coverGenWriting_) return false;
  for (int i = 0; i < pageCount; ++i) {
    const LibraryIndex::BookRef& r = pageCache_[i];
    if (r.id == 0) continue;          // empty slot
    if (r.path[0] == '\0') continue;  // collection tile without cover -> placeholder
    const std::string tp = LibraryIndex::thumbPathFor(std::string(r.path), coverWidth_, coverHeight_);
    if (tp.empty()) continue;
    if (!Storage.exists(tp.c_str())) return false;
    if (!isBookCoverReady(std::string(r.path))) return false;
  }
  (void)pageStart;
  return true;
}

std::string LibraryActivity::pageFrameCachePath(int pageStart, uint32_t sig) const {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s/fr_%08x_%06d.bin", kLibFrameDir, sig, pageStart);
  return std::string(buf);
}

void LibraryActivity::clearPageFrameCache() {
  auto d = Storage.open(kLibFrameDir);
  if (!d || !d.isDirectory()) return;
  d.rewindDirectory();
  char nb[96];
  for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    if (f.getName(nb, sizeof(nb))) {
      char full[128];
      snprintf(full, sizeof(full), "%s/%s", kLibFrameDir, nb);
      f.close();
      Storage.remove(full);
    } else {
      f.close();
    }
  }
  d.close();
}

void LibraryActivity::savePageFrame(int pageStart, int pageCount, int savedSelector) {
  if (popupMode_ != PopupMode::None) return;
  if (coverGen_.active || coverGen_.pending) return;
  if (pageCount <= 0) return;
  if (!pageCoversComplete(pageStart, pageCount)) return;

  uint8_t* fb = renderer.getFrameBuffer();
  const size_t bufSize = renderer.getBufferSize();
  if (!fb || bufSize == 0) return;

  const uint32_t sig = frameSignature();
  const std::string path = pageFrameCachePath(pageStart, sig);
  Storage.mkdir(kLibFrameDir);

  FsFile file;
  if (!Storage.openFileForWrite("LIB", path, file)) return;
  const uint32_t hdr[3] = {kFrameMagic, static_cast<uint32_t>(pageStart), static_cast<uint32_t>(savedSelector)};
  file.write(reinterpret_cast<const uint8_t*>(hdr), sizeof(hdr));
  const size_t written = file.write(fb, bufSize);
  file.close();
  if (written != bufSize) {
    Storage.remove(path.c_str());
    return;
  }
  LOG_DBG("LIB", "FrameSave: page=%d sig=%08x selector=%d %zu B", pageStart, sig, savedSelector, bufSize);
}

bool LibraryActivity::tryLoadPageFrame(int pageStart, int pageCount) {
  if (popupMode_ != PopupMode::None) return false;
  if (pageCount <= 0) return false;
  // Safe to serve a cached frame whenever this page's covers are already all
  // present. coverGen_ may still be flagged active right after a page flip
  // (refreshPageCache enables it), but with a complete page it will find zero
  // missing covers, so loading the frame is correct.
  if (!pageCoversComplete(pageStart, pageCount)) return false;

  const uint32_t sig = frameSignature();
  const std::string path = pageFrameCachePath(pageStart, sig);
  if (!Storage.exists(path.c_str())) return false;
  FsFile file;
  if (!Storage.openFileForRead("LIB", path, file)) return false;

  const size_t bufSize = renderer.getBufferSize();
  if (bufSize == 0) {
    file.close();
    return false;
  }
  if (file.size() != static_cast<int>(sizeof(uint32_t) * 3 + bufSize)) {
    file.close();
    Storage.remove(path.c_str());
    return false;
  }

  uint32_t hdr[3] = {0, 0, 0};
  if (file.read(reinterpret_cast<uint8_t*>(hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    file.close();
    Storage.remove(path.c_str());
    return false;
  }
  if (hdr[0] != kFrameMagic || static_cast<int>(hdr[1]) != pageStart) {
    file.close();
    Storage.remove(path.c_str());
    return false;
  }

  uint8_t* fb = renderer.getFrameBuffer();
  if (!fb) {
    file.close();
    return false;
  }
  const size_t got = file.read(fb, bufSize);
  file.close();
  if (got != bufSize) {
    Storage.remove(path.c_str());
    return false;
  }

  // --- Overlay the dynamic parts over the restored static frame ---
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int total = totalBooks_;
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = pageStart / gridsPerPage_ + 1;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int headerY = metrics.topPadding + 8;
  const int selTitleY = headerY + lh + 2;
  const int rowH = coverHeight_ + rowPad_;

  // Erase the top band (old header/info/title) in WHITE - grid below stays
  // intact. (state=false = white pixels on the e-ink buffer)
  renderer.fillRect(0, 0, pageWidth, contentTop, false);

  // Header bar + page number
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr, nullptr);
  if (total > 0) {
    char hdrBuf[32] = {};
    snprintf(hdrBuf, sizeof(hdrBuf), "%d/%d (%d)", curPage, totalPages, total);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 6, hdrBuf, true,
                      EpdFontFamily::REGULAR);
  }

  // Info line + selected title/author
  const int curPageRaw = pageStart / gridsPerPage_;
  rebuildInfoCacheIfChanged(curPageRaw, total);
  cachedInfo_ = renderer.truncatedText(UI_10_FONT_ID, cachedInfo_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
  const int lblW = renderer.getTextWidth(UI_10_FONT_ID, cachedInfo_.c_str(), EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, (pageWidth - lblW) / 2, headerY, cachedInfo_.c_str(), true, EpdFontFamily::REGULAR);
  if (selectorIndex_ < total && !cachedSelTitle_.empty()) {
    const int selTitleW = renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::BOLD);
    const int selTitleX = std::max(8, (pageWidth - selTitleW) / 2);
    renderer.drawText(UI_10_FONT_ID, selTitleX, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);
    if (!cachedSelAuthor_.empty()) {
      std::string author =
          renderer.truncatedText(UI_10_FONT_ID, cachedSelAuthor_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
      const int authorY = selTitleY + lh + 1;
      const int authorW = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, std::max(8, (pageWidth - authorW) / 2), authorY, author.c_str(), true,
                        EpdFontFamily::REGULAR);
    }
  }

  // Selection border: erase the saved tile's border, draw the current one.
  auto tileXY = [&](int idx, int* outX, int* outY) {
    const int ts = (idx / gridsPerPage_) * gridsPerPage_;
    const int ti = idx - ts;
    const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap_;
    const int x0 = (pageWidth - gridW) / 2;
    *outX = x0 + (ti % gridColumns_) * (coverWidth_ + gap_);
    *outY = contentTop + (ti / gridColumns_) * rowH;
  };
  const int savedSelector = static_cast<int>(hdr[2]);
  if (savedSelector >= 0 && savedSelector < total && savedSelector != selectorIndex_) {
    int ex = 0, ey = 0;
    tileXY(savedSelector, &ex, &ey);
    drawCyberpunkSelectionBorder(renderer, ex, ey, coverWidth_, coverHeight_, false);
  }
  int nx = 0, ny = 0;
  tileXY(selectorIndex_, &nx, &ny);
  drawCyberpunkSelectionBorder(renderer, nx, ny, coverWidth_, coverHeight_, true);

  // Button hints (frame already contains them; redraw for correctness)
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_LIBRARY_DIR_LEFT_PAGE),
                              tr(STR_LIBRARY_DIR_RIGHT_PAGE));
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));

  lastRenderedSelectorIndex_ = selectorIndex_;
  lastRenderedPage_ = curPageRaw;
  prevBorderIdx_ = selectorIndex_;
  lastFrameHitPage_ = pageStart;

  renderer.displayBuffer();
  LOG_DBG("LIB", "FrameHit: page=%d sig=%08x", pageStart, sig);
  return true;
}

void LibraryActivity::render(RenderLock&&) {
  esp_task_wdt_reset();
  const int total = totalBooks_;
  const int curPageRaw = total > 0 ? selectorIndex_ / gridsPerPage_ : 0;
  LibraryPerf::ScopedTimer renderTimer("render_total");

  // ---- Early-out guard: nothing changed -----------------------------------
  if (!forceRender_ && popupMode_ == PopupMode::None && curPageRaw == lastRenderedPage_ &&
      selectorIndex_ == lastRenderedSelectorIndex_) {
    return;
  }

  // ---- PARTIAL RENDER: selection moved within same page -------------------
  // Only erases old border + title/author, draws new ones. No clearScreen().
  if (!forceRender_ && popupMode_ == PopupMode::None && curPageRaw == lastRenderedPage_ &&
      selectorIndex_ != lastRenderedSelectorIndex_ && total > 0) {
    LibraryPerf::logElapsed("render_partial_start", renderTimer.start);

    const auto pageWidth = renderer.getScreenWidth();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int headerY = metrics.topPadding + 8;
    const int selTitleY = headerY + lh + 2;
    const int rowH = coverHeight_ + rowPad_;

    // 1. Erase old border (drawn in white).
    if (prevBorderIdx_ >= 0 && prevBorderIdx_ < total) {
      const int lastPageStart = (prevBorderIdx_ / gridsPerPage_) * gridsPerPage_;
      const int lastTileIdx = prevBorderIdx_ - lastPageStart;
      const int lastCol = lastTileIdx % gridColumns_;
      const int lastRow = lastTileIdx / gridColumns_;
      const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap_;
      const int x0 = (pageWidth - gridW) / 2;
      const int lastX = x0 + lastCol * (coverWidth_ + gap_);
      const int lastY = contentTop + lastRow * rowH;
      drawCyberpunkSelectionBorder(renderer, lastX, lastY, coverWidth_, coverHeight_, false);
    }

    // 2. Erase old title/author area.
    renderer.fillRect(0, selTitleY, pageWidth, lh * 2 + 1, false);

    // 3. Update cached text strings if selection/filter/sort/search changed.
    rebuildInfoCacheIfChanged(curPageRaw, total);

    // 4. Draw new border (black).
    const int pageStart = curPageRaw * gridsPerPage_;
    const int tileIdx = selectorIndex_ - pageStart;
    const int col = tileIdx % gridColumns_;
    const int row = tileIdx / gridColumns_;
    const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap_;
    const int x0_new = (pageWidth - gridW) / 2;
    const int newX = x0_new + col * (coverWidth_ + gap_);
    const int newY = contentTop + row * rowH;
    drawCyberpunkSelectionBorder(renderer, newX, newY, coverWidth_, coverHeight_, true);

    // 5. Draw new title/author.
    if (selectorIndex_ < total && !cachedSelTitle_.empty()) {
      const int selTitleW = renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::BOLD);
      const int selTitleX = std::max(8, (pageWidth - selTitleW) / 2);
      renderer.drawText(UI_10_FONT_ID, selTitleX, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);

      if (!cachedSelAuthor_.empty()) {
        std::string author =
            renderer.truncatedText(UI_10_FONT_ID, cachedSelAuthor_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
        const int authorY = selTitleY + lh + 1;
        const int authorW = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
        const int authorX = std::max(8, (pageWidth - authorW) / 2);
        renderer.drawText(UI_10_FONT_ID, authorX, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
      }
    }

    // 6. Commit state and flush display.
    lastRenderedSelectorIndex_ = selectorIndex_;
    prevBorderIdx_ = selectorIndex_;
    renderer.displayBuffer();
    return;
  }

  // ---- FULL RENDER --------------------------------------------------------
  forceRender_ = false;

  // Reset the per-render placeholder tracker (see header). pageCoversComplete()
  // refuses to touch covers while the main task is writing one, so the frame
  // cache is neither served nor persisted mid-generation.
  renderSawPlaceholder_ = false;

  // Frame-cache fast path: if this page was already fully rendered (all covers
  // present) and nothing grid-affecting changed, restore its frame instead of
  // re-decoding every cover BMP.
  if (total > 0) {
    const int pageStartForLoad = curPageRaw * gridsPerPage_;
    const int pageCountForLoad = std::min(gridsPerPage_, total - pageStartForLoad);
    if (tryLoadPageFrame(pageStartForLoad, pageCountForLoad)) {
      LibraryPerf::logElapsed("render_frameCacheHit", renderTimer.start);
      return;
    }
  }
  lastFrameHitPage_ = -1;
  LibraryPerf::logElapsed("render_afterFrameCacheMiss", renderTimer.start);

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = total > 0 ? curPageRaw + 1 : 0;

  LOG_DBG("LIB", "Render: start free=%u maxA=%u total=%d page=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), total,
          curPage);

  // Header bar
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr, nullptr);

  if (total > 0) {
    char hdrBuf[32] = {};
    snprintf(hdrBuf, sizeof(hdrBuf), "%d/%d (%d)", curPage, totalPages, total);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 6, hdrBuf, true,
                      EpdFontFamily::REGULAR);
  }

  // Rebuild cached header/title strings only when inputs change.
  rebuildInfoCacheIfChanged(curPageRaw, total);

  // Info line (filter/sort/search)
  cachedInfo_ = renderer.truncatedText(UI_10_FONT_ID, cachedInfo_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
  int lblW = renderer.getTextWidth(UI_10_FONT_ID, cachedInfo_.c_str(), EpdFontFamily::REGULAR);
  int centerX = (pageWidth - lblW) / 2;
  int headerY = metrics.topPadding + 8;
  renderer.drawText(UI_10_FONT_ID, centerX, headerY, cachedInfo_.c_str(), true, EpdFontFamily::REGULAR);

  // Selected book title + author
  if (total > 0 && selectorIndex_ < total && !cachedSelTitle_.empty()) {
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int selTitleY = headerY + lh + 2;
    renderer.fillRect(0, selTitleY, pageWidth, lh * 2 + 1, false);
    const int selTitleW = renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::BOLD);
    const int selTitleX = std::max(8, (pageWidth - selTitleW) / 2);  // min 8px left margin
    renderer.drawText(UI_10_FONT_ID, selTitleX, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);

    if (!cachedSelAuthor_.empty()) {
      std::string author =
          renderer.truncatedText(UI_10_FONT_ID, cachedSelAuthor_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
      const int authorY = selTitleY + lh + 1;
      const int authorW = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
      const int authorX = std::max(8, (pageWidth - authorW) / 2);  // min 8px left margin
      renderer.drawText(UI_10_FONT_ID, authorX, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
    }

    // Cover generation progress text centered below author
    if ((coverGen_.active || coverGen_.pending) && coverGen_.total > 0) {
      char covBuf[48];
      snprintf(covBuf, sizeof(covBuf), "%d/%d %s", coverGen_.done + 1, coverGen_.total, tr(STR_LOADING_POPUP));
      const int covW = renderer.getTextWidth(SMALL_FONT_ID, covBuf, EpdFontFamily::REGULAR);
      const int covY = selTitleY + lh * 2 - 4;  // moved up 8px to avoid grid overlap
      renderer.drawText(SMALL_FONT_ID, (pageWidth - covW) / 2, covY, covBuf, true, EpdFontFamily::BOLD);
    }
  }

  // Content area
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_LIBRARY_EMPTY));
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_LIBRARY_DIR_LEFT_PAGE),
                                tr(STR_LIBRARY_DIR_RIGHT_PAGE));
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
  }

  if (total > 0) {
    const int pageStart = (curPage - 1) * gridsPerPage_;
    const int pageCount = std::min(gridsPerPage_, total - pageStart);
    const int gap = gap_;
    const int rowH = coverHeight_ + rowPad_;
    const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
    const int x0 = (pageWidth - gridW) / 2;

    // Build wrapped cover-title cache for this page once.
    if (pageTitleCacheKey_ != pageStart) {
      pageTitleCache_.clear();
      pageTitleCache_.reserve(pageCount);
      constexpr int kCoverTextPad = 4;
      for (int i = 0; i < pageCount; ++i) {
        const int idx = pageStart + i;
        std::string t(pageCache_[i].title);
        if (t.empty()) t = book_filter::filenameWithoutExtension(pageCache_[i].path);
        pageTitleCache_.push_back(
            renderer.wrappedText(SMALL_FONT_ID, t.c_str(), coverWidth_ - 2 * kCoverTextPad, 3, EpdFontFamily::BOLD));
      }
      pageTitleCacheKey_ = pageStart;
    }

    for (int i = 0; i < pageCount; ++i) {
      const int idx = pageStart + i;
      // Skip empty slots (zeroed out after collections query leaves fewer items)
      if (pageCache_[i].id == 0 && pageCache_[i].path[0] == '\0') continue;
      const int col = i % gridColumns_;
      const int row = i / gridColumns_;
      const int x = x0 + col * (coverWidth_ + gap);
      const int y = contentTop + row * rowH;
      drawTileContent(i, x, y);
      if (idx == selectorIndex_) {
        drawCyberpunkSelectionBorder(renderer, x, y, coverWidth_, coverHeight_);
      }
    }

    // Pagination dots (wrap onto multiple rows when there are many pages)
    if (totalPages > 1) {
      constexpr int DS = 8, DSp = 6, rowGap = 4;
      const int maxDotW = pageWidth - 16;  // keep dots inside the panel with a margin
      const int dotsPerRow = std::max(1, (maxDotW + DSp) / (DS + DSp));
      const int numRows = (totalPages + dotsPerRow - 1) / dotsPerRow;
      // Bottom of the dot block sits above the button-hints bar; taller blocks
      // (extra rows) push the block up so it never overlaps the hints.
      const int bottomY = pageHeight - metrics.buttonHintsHeight - 14;
      const int blockTop = bottomY - numRows * DS - (numRows - 1) * rowGap;
      for (int p = 0; p < totalPages; ++p) {
        const int r = p / dotsPerRow;
        const int c = p % dotsPerRow;
        const int rowDots = (r == numRows - 1) ? (totalPages - r * dotsPerRow) : dotsPerRow;
        const int rowW = rowDots * DS + (rowDots - 1) * DSp;
        const int sx = (pageWidth - rowW) / 2;
        const int sy = blockTop + r * (DS + rowGap);
        const int dx = sx + c * (DS + DSp);
        if (p == curPage - 1)
          renderer.fillRect(dx, sy, DS, DS, true);
        else
          renderer.drawRect(dx, sy, DS, DS, true);
      }
    }
  }

  // Button hints + popup overlay
  if (popupMode_ != PopupMode::None) {
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  } else {
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_LIBRARY_DIR_LEFT_PAGE),
                                tr(STR_LIBRARY_DIR_RIGHT_PAGE));
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
  }

  if (popupMode_ != PopupMode::None) popupOverlay_.render(renderer, pageWidth, pageHeight);

  // Cache the finished page frame (only when covers are complete) so returning
  // to this page can skip the per-cover BMP decode. Never persist a frame that
  // drew a placeholder: the cover can become ready right after the tile was
  // drawn (generation overlaps this render task), and saving that frame would
  // make the placeholder stick until the activity is re-entered.
  if (total > 0 && popupMode_ == PopupMode::None && !renderSawPlaceholder_) {
    const int pgStart = curPageRaw * gridsPerPage_;
    const int pgCount = std::min(gridsPerPage_, total - pgStart);
    savePageFrame(pgStart, pgCount, selectorIndex_);
  }

  lastRenderedSelectorIndex_ = selectorIndex_;
  lastRenderedPage_ = curPageRaw;
  prevBorderIdx_ = selectorIndex_;

  renderer.displayBuffer();
  LibraryPerf::logElapsed("render_displayBuffer", renderTimer.start);
  LOG_DBG("LIB", "Render: end selector=%d page=%d total=%d", selectorIndex_, curPageRaw, total);
}

void LibraryActivity::reloadPageCovers() {
  // Page cache is already refreshed on navigation.  No-op for now.
  refreshPageCache();
}

// ============================================================================
// SECTION 8: Tile drawing
// ============================================================================

void LibraryActivity::drawTileContent(int i, int x, int y) const {
  bool drawn = false;
  const std::string path(pageCache_[i].path);
  const bool isSeriesTile = (pageCache_[i].id & 0x80000000u) != 0;
  // A tile represents a collection/series when we are at a root listing
  // (collections mode or the Series+Books root), never inside a collection.
  const bool isCollectionTile = isSeriesTile && currentCollectionIdx_ < 0;
  const bool isUserCollection = isCollectionTile && pageCache_[i].isCollection;
  const std::string thumbPath = LibraryIndex::thumbPathFor(path, coverWidth_, coverHeight_);
  const bool hasThumb = !thumbPath.empty() && Storage.exists(thumbPath.c_str());
  LOG_DBG("LIB", "drawTile: idx=%d path=%s thumb=%s hasThumb=%d collection=%d userColl=%d", i, path.c_str(), thumbPath.c_str(), hasThumb ? 1 : 0, isCollectionTile ? 1 : 0, isUserCollection ? 1 : 0);

  if (hasThumb) {
    FsFile file;
    if (Storage.openFileForRead("LIB", thumbPath, file)) {
      Bitmap bmp(file);
      if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
        renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
        if (bmp.is1Bit()) {
          // Generated thumbs are 1-bit BMPs. drawBitmap only handles 1-bit when
          // cropX==cropY==0; with any crop it falls into the 2-bit path and
          // renders a blank tile (and suppresses the title). Use the 1-bit
          // path (scales, no crop) so covers always show.
          renderer.drawBitmap1Bit(bmp, x, y, coverWidth_, coverHeight_);
        } else {
          const float bmpRatio = static_cast<float>(bmp.getWidth()) / static_cast<float>(bmp.getHeight());
          const float tileRatio = static_cast<float>(coverWidth_) / static_cast<float>(coverHeight_);
          const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
          const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
          renderer.drawBitmap(bmp, x, y, coverWidth_, coverHeight_, cropX, cropY);
        }
        drawn = true;
      }
      file.close();
    }
  }

  if (!drawn) {
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      // File exists but BMP parse failed. Only remove it when no cover
      // generation is active; removing during generation deletes the file
      // descriptor that generatePageCover is writing to, which leaves a
      // zero-size/ghost file and makes the cover invisible on re-enter.
      if (!coverGen_.active) {
        LOG_DBG("LIB", "drawTile: idx=%d thumb exists but bmp parse failed removing=%s thumb=%s", i,
                thumbPath.c_str(), thumbPath.c_str());
        Storage.remove(thumbPath.c_str());
      } else {
        LOG_DBG("LIB", "drawTile: idx=%d thumb exists but bmp parse failed (gen active) skipping remove thumb=%s",
                i, thumbPath.c_str());
      }
    } else if (thumbPath.empty()) {
      LOG_DBG("LIB", "drawTile: idx=%d empty thumbPath path=%s", i, path.c_str());
    } else {
      LOG_DBG("LIB", "drawTile: idx=%d thumb missing path=%s thumb=%s", i, path.c_str(), thumbPath.c_str());
    }
    if (isUserCollection) {
      // User collection placeholder: distinct visual style with folder icon
      const int stackOffset = 4;
      renderer.drawRoundedRect(x + stackOffset, y + stackOffset, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS,
                               true);
      renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, false, false, true, true,
                               Color::Black);
      const int iconSize = std::min(28, std::min(coverWidth_ - 4, coverHeight_ / 3 - 4));
      const int iconX = x + (coverWidth_ - iconSize) / 2;
      const int iconY = y + std::max(4, (coverHeight_ / 3 - iconSize) / 2);
      renderer.drawIcon(::LibraryIcon, iconX, iconY, iconSize, iconSize);

      const int textAreaH = 2 * coverHeight_ / 3 - 8;
      if (i < static_cast<int>(pageTitleCache_.size())) {
        const auto& lines = pageTitleCache_[i];
        int lh = renderer.getLineHeight(SMALL_FONT_ID);
        int ty = y + coverHeight_ / 3 + (textAreaH - static_cast<int>(lines.size()) * lh) / 2;
        for (auto& ln : lines) {
          int tw = renderer.getTextWidth(SMALL_FONT_ID, ln.c_str(), EpdFontFamily::BOLD);
          renderer.drawText(SMALL_FONT_ID, x + (coverWidth_ - tw) / 2, ty, ln.c_str(), false, EpdFontFamily::BOLD);
          ty += lh;
        }
      }
    } else if (isCollectionTile) {
      // Auto series placeholder: stacked outlines + centered title, no icon
      const int stackOffset = 6;
      renderer.drawRoundedRect(x + stackOffset, y + stackOffset, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS,
                               true);
      renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, false, false, true, true,
                               Color::Black);

      const int textAreaH = 2 * coverHeight_ / 3 - 8;
      if (i < static_cast<int>(pageTitleCache_.size())) {
        const auto& lines = pageTitleCache_[i];
        int lh = renderer.getLineHeight(SMALL_FONT_ID);
        int ty = y + coverHeight_ / 3 + (textAreaH - static_cast<int>(lines.size()) * lh) / 2;
        for (auto& ln : lines) {
          int tw = renderer.getTextWidth(SMALL_FONT_ID, ln.c_str(), EpdFontFamily::BOLD);
          renderer.drawText(SMALL_FONT_ID, x + (coverWidth_ - tw) / 2, ty, ln.c_str(), false, EpdFontFamily::BOLD);
          ty += lh;
        }
      }
    } else {
      // Book placeholder. Record that this render drew a placeholder so the
      // frame cache is not persisted for this page (the cover may become ready
      // immediately after, and a cached placeholder frame would stick).
      renderSawPlaceholder_ = true;
      renderer.drawRoundedRect(x, y, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS, true);
      renderer.fillRoundedRect(x, y + coverHeight_ / 3, coverWidth_, 2 * coverHeight_ / 3 + 1, COVER_CORNER_RADIUS,
                               false, false, true, true, Color::Black);
      const int iconSize = std::min(32, std::min(coverWidth_ - 4, coverHeight_ / 3 - 4));
      const int iconX = x + (coverWidth_ - iconSize) / 2;
      const int iconY = y + std::max(4, (coverHeight_ / 3 - iconSize) / 2);
      renderer.drawIcon(::CoverIcon, iconX, iconY, iconSize, iconSize);

      const int textAreaH = 2 * coverHeight_ / 3 - 8;
      if (i < static_cast<int>(pageTitleCache_.size())) {
        const auto& lines = pageTitleCache_[i];
        int lh = renderer.getLineHeight(SMALL_FONT_ID);
        int ty = y + coverHeight_ / 3 + (textAreaH - static_cast<int>(lines.size()) * lh) / 2;
        for (auto& ln : lines) {
          int tw = renderer.getTextWidth(SMALL_FONT_ID, ln.c_str(), EpdFontFamily::BOLD);
          renderer.drawText(SMALL_FONT_ID, x + (coverWidth_ - tw) / 2, ty, ln.c_str(), false, EpdFontFamily::BOLD);
          ty += lh;
        }
      }

      // Progress bar for cover generation: drawn in WHITE on the black placeholder.
      // The bar is only visible when coverGen_.active is true and this tile
      // corresponds to a slot that is being or has been processed.
      if (coverGen_.active || coverGen_.pending) {
        const int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
        const int slot = (pageStart + i) - pageStart;  // local slot index
        if (slot >= 0 && slot < gridsPerPage_ && slot <= coverGen_.slot && coverGen_.total > 0) {
          constexpr int kBarH = 8;
          const int barY = y + coverHeight_ - kBarH - 4;
          const int maxBarW = coverWidth_ - 6;
          // White outline
          renderer.drawRect(x + 3, barY, maxBarW, kBarH, false);
          // White fill: proportional to done / total
          const int barW = (coverGen_.done * maxBarW) / coverGen_.total;
          if (barW > 0) {
            renderer.fillRect(x + 3, barY, barW, kBarH, false);
          }
        }
      }
    }
  }

  // Collection title ribbon: bottom overlay on cover or placeholder
  if (isCollectionTile) {
    constexpr int ribbonH = 36;
    const int ribbonY = y + coverHeight_ - ribbonH;
    renderer.fillRect(x + 4, ribbonY, coverWidth_ - 8, ribbonH, Color::Black);
    const char* title = pageCache_[i].title;
    const int titleFont = SMALL_FONT_ID;
    const int maxTitleW = coverWidth_ - 12;
    std::string displayTitle = title;
    if (renderer.getTextWidth(titleFont, displayTitle.c_str(), EpdFontFamily::BOLD) > maxTitleW) {
      displayTitle = renderer.truncatedText(titleFont, displayTitle.c_str(), maxTitleW, EpdFontFamily::BOLD);
    }
    const int titleW = renderer.getTextWidth(titleFont, displayTitle.c_str(), EpdFontFamily::BOLD);
    const int titleX = x + (coverWidth_ - titleW) / 2;
    const int titleY = ribbonY + (ribbonH - renderer.getLineHeight(titleFont)) / 2;
    // black=false -> white text on the black ribbon.
    renderer.drawText(titleFont, titleX, titleY, displayTitle.c_str(), false, EpdFontFamily::BOLD);
  }

  // Series badge — shows on both covers AND placeholders
  if (isSeriesTile && !isCollectionTile) {
    const char* author = pageCache_[i].author;
    int count = 0;
    if (author && author[0]) {
      sscanf(author, "%d books", &count);
    }
    if (count <= 0) count = 1;
    constexpr int badgePad = 4;
    const int badgeMaxW = coverWidth_ - 2 * badgePad;
    char badgeBuf[16];
    snprintf(badgeBuf, sizeof(badgeBuf), "%d", count);
    const int badgeFont = SMALL_FONT_ID;
    const int badgeW = renderer.getTextWidth(badgeFont, badgeBuf, EpdFontFamily::BOLD) + badgePad * 2;
    const int badgeH = std::max(14, renderer.getLineHeight(badgeFont) + 4);
    const int bx = x + coverWidth_ - badgeW - badgePad;
    const int by = y + badgePad;
    renderer.fillRoundedRect(bx, by, badgeW, badgeH, 4, Color::Black);
    renderer.drawRoundedRect(bx, by, badgeW, badgeH, 1, 4, true);
    const int textX = bx + (badgeW - renderer.getTextWidth(badgeFont, badgeBuf, EpdFontFamily::BOLD)) / 2;
    const int textY = by + (badgeH - renderer.getLineHeight(badgeFont)) / 2;
    renderer.drawText(badgeFont, textX, textY, badgeBuf, true, EpdFontFamily::BOLD);
  }

  // Ribbon badge — shows on both covers AND placeholders
  {
    const bool isFav = pageCache_[i].isFavorite;
    const bool isComplete = pageCache_[i].isCompleted;
    const bool isOpened = pageCache_[i].isOpened && !isComplete;
    if (!isSeriesTile && (isComplete || isFav || isOpened))
      drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
  }
}
