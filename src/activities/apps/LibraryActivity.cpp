#include "LibraryActivity.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

#include "../home/BookContextMenuActivity.h"
#include "../util/ConfirmationActivity.h"
#include "../util/KeyboardEntryActivity.h"
#include "util/BookFilter.h"
#include "util/CoverGenerator.h"
#include "util/StringUtils.h"
#include "CrossPointSettings.h"
#include "FavoritesStore.h"
#include "HiddenBooksStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SilentRestart.h"
#include "components/LibraryCache.h"
#include "components/LibraryIndex.h"
#include <Epub.h>
#include <Xtc.h>
#include <ZipFile.h>
#include "components/icons/bookshelf.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/cover.h"
#include "components/icons/heart.h"
#include "components/icons/heart24.h"
#include "components/icons/library.h"
#include "components/icons/library_new.h"
#include "components/icons/recentbooks.h"
#include "components/icons/search_plus.h"
#include "components/icons/search_minus.h"
#include "components/icons/sort_asc.h"
#include "components/icons/sort_desc.h"

bool LibraryActivity::forceScanOnNextOpen_ = false;
#include "components/icons/text24.h"
#include "components/icons/time_fast.h"
#include "components/icons/transfer.h"
#include "components/LibraryPopupOverlay.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "SilentRestart.h"

// Compile-time verification: the largest icon (32×32 1‑bpp) is exactly 128 B.
static_assert(sizeof(CoverIcon) == 128, "unexpected icon size, update kMaxIconBytes");

// ============================================================================
// SECTION 1: Anonymous namespace — drawing helpers and string utilities
// ============================================================================
namespace {

constexpr int COVER_CORNER_RADIUS = 2;

// ---- Geometric drawing helpers --------------------------------------------

static void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black) {
  for (int dy = 0; dy < leg; ++dy)
    r.fillRect(x + dy, y + dy, leg - dy, 1, black);
}

void drawCyberpunkSelectionBorder(const GfxRenderer& renderer, int x, int y, int w, int h, bool color = true) {
  constexpr int c = 4;
  constexpr int cl = 5;
  constexpr int cg = 2;
  const int bx = x - 5;
  const int by = y - 5;
  const int bw = w + 10;
  const int bh = h + 10;
  renderer.drawRect(bx, by, bw, bh, color);
  renderer.drawLine(bx + cg, by, bx + cg + cl, by, 1, color);
  renderer.drawLine(bx, by + cg, bx, by + cg + cl, 1, color);
  renderer.drawLine(bx + bw - cg - cl, by, bx + bw - cg, by, 1, color);
  renderer.drawLine(bx + bw, by + cg, bx + bw, by + cg + cl, 1, color);
  renderer.drawLine(bx + cg, by + bh, bx + cg + cl, by + bh, 1, color);
  renderer.drawLine(bx, by + bh - cg, bx, by + bh - cg - cl, 1, color);
  renderer.drawLine(bx + bw - cg - cl, by + bh, bx + bw - cg, by + bh, 1, color);
  renderer.drawLine(bx + bw, by + bh - cg, bx + bw, by + bh - cg - cl, 1, color);
}

void drawRibbonBadge(GfxRenderer& r, int cx, int cy, int cw, int ch,
                     bool completed, bool favorite, bool opened) {
  (void)ch;
  const int leg = std::max(20, std::min(cw * 2 / 5, 44));
  const int rx = cx + cw - leg;
  const int ry = cy;

  fillTopRightTri(r, rx - 3, ry - 3, leg + 6, false);
  fillTopRightTri(r, rx - 2, ry - 2, leg + 4, true);
  fillTopRightTri(r, rx - 1, ry - 1, leg + 2, false);
  fillTopRightTri(r, rx,     ry,     leg,     true);

  const int symCx = cx + cw - leg / 3;
  const int symCy = cy + leg / 3;
  const int symSz = std::max(8, leg * 22 / 100);

  if (completed) {
    r.drawLine(symCx - 5, symCy,     symCx - 1, symCy + 4, 2, false);
    r.drawLine(symCx - 1, symCy + 4, symCx + 6, symCy - 4, 2, false);
  } else if (favorite) {
    constexpr int kHeartSz = 24;
    if (leg >= kHeartSz) {
      int hx = symCx - kHeartSz / 2;
      int hy = symCy - kHeartSz / 2;
      r.drawIconInverted(::Heart24Icon, hx, hy, kHeartSz, kHeartSz);
    }
  } else if (opened) {
    const int dotR = std::max(1, symSz / 4);
    for (int y2 = -dotR; y2 <= dotR; ++y2)
      for (int x2 = -dotR; x2 <= dotR; ++x2)
        if (x2 * x2 + y2 * y2 <= dotR * dotR + dotR)
          r.drawLine(symCx + x2, symCy + y2, symCx + x2, symCy + y2, 1, false);
  }
}

// ---- Filter predicate ------------------------------------------------------
//
// The 3 filter modes that don't need extra data plumbing (All / Favourites /
// LatestRead) are kept inline; the FAVOURITES / LATEST_READ check uses the
// process-global stores. CrossPointSettings is required for the enum.

static bool includeBookByFilter(const LibraryCache::Entry& e, CrossPointSettings::LIBRARY_FILTER filter) {
  switch (filter) {
    case CrossPointSettings::LIBRARY_FILTER_ALL: return true;
    case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: return FAVORITES.isFavorite(e.path);
    case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: {
      const auto& recent = RECENT_BOOKS.getBooks();
      for (const auto& rb : recent) {
        if (rb.path == e.path || (!rb.bookId.empty() && rb.bookId == e.path)) return true;
      }
      return false;
    }
  }
  return false;
}

}  // namespace

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

  // 4. Remove from hidden books, recents, and favourites
  HIDDEN_BOOKS.removeBook(bookPath);
  FAVORITES.removeBook(bookPath);
  RECENT_BOOKS.removeBook(bookPath);

  // 5. Re-scan the library index to remove the entry
  LibraryIndex::sync();
}

// ============================================================================
// SECTION 2: Layout & lifecycle
// ============================================================================

void LibraryActivity::applyLayoutFromSettings() {
  switch (SETTINGS.libraryLayout) {
    case CrossPointSettings::LIBRARY_LAYOUT_2X2:
      gridColumns_ = 2; coverWidth_ = 202; coverHeight_ = 306; gap_ = 13; break;
    case CrossPointSettings::LIBRARY_LAYOUT_3X3:
      gridColumns_ = 3; coverWidth_ = 130; coverHeight_ = 190; gap_ = 13; break;
    case CrossPointSettings::LIBRARY_LAYOUT_4X4:
    default:
      gridColumns_ = 4; coverWidth_ = 100; coverHeight_ = 150; gap_ = 7; break;
  }
  gridsPerPage_ = gridColumns_ * gridColumns_;
  rowPad_ = (gridColumns_ >= 4) ? 8 : 14;
  pageTitleCacheKey_ = -1;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("LIB", "onEnter: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  READING_STATS.ensureLoaded();

  applyLayoutFromSettings();
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

  scanSd();

  LOG_DBG("LIB", "onEnter: after scanSd heap=%u maxA=%u total=%d",
               ESP.getFreeHeap(), ESP.getMaxAllocHeap(), totalBooks_);
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
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;
  collectionsMode_ = (currentSort_ == CrossPointSettings::LIBRARY_SORT_COLLECTIONS);
  if (collectionsMode_) currentCollectionIdx_ = -1;

  // Init LibraryIndex if needed
  LibraryIndex::init();

  if (!LibraryIndex::exists()) {
    // Cold path: full scan with progress popup — always needed when no index exists
    renderer.clearScreen();
    Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
    GUI.fillPopupProgress(renderer, popupRect, 0);
    renderer.displayBuffer();

    LibraryIndex::scan(renderer, popupRect, SETTINGS.libraryRootDir);
    LibraryIndex::buildIndices();
    LibraryIndex::buildCollectionsIndex();
    totalBooks_ = collectionsMode_
        ? LibraryIndex::totalCollections()
        : LibraryIndex::totalMatching(currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
                                       static_cast<LibraryIndex::FilterMode>(currentFilter_));
    totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
    refreshPageCache();
    return;
  }

  // Fast path: existing library.dat
  // Decide whether to perform SD scan based on:
  //   - forceScanOnNextOpen_ (set by "Update & Open" popup)
  //   - libraryUpdateMode == AUTO
  const bool doScan = forceScanOnNextOpen_ ||
      SETTINGS.libraryUpdateMode == CrossPointSettings::LIBRARY_UPDATE_AUTO;
  forceScanOnNextOpen_ = false;

  if (doScan) {
    int added = 0, removed = 0;
    LibraryIndex::scan(renderer, Rect(), SETTINGS.libraryRootDir, &added, &removed);
    if (added > 0 || removed > 0) {
      renderer.clearScreen();
      GUI.drawPopup(renderer, tr(STR_UPDATING_LIBRARY));
      renderer.displayBuffer();
      LibraryIndex::buildIndices();
      LibraryIndex::buildCollectionsIndex();
    }
  }

  totalBooks_ = collectionsMode_
      ? LibraryIndex::totalCollections()
      : LibraryIndex::totalMatching(currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
                                     static_cast<LibraryIndex::FilterMode>(currentFilter_));
  totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
  LOG_DBG("LIB", "scanSd: existing index, doScan=%d total=%d collMode=%d",
          static_cast<int>(doScan), totalBooks_, collectionsMode_);
  refreshPageCache();
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  currentFilter_ = filter;
  totalBooks_ = LibraryIndex::totalMatching(nullptr, static_cast<LibraryIndex::FilterMode>(filter));
  totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
  selectorIndex_ = 0;
  refreshPageCache();
}

void LibraryActivity::refreshPageCache() {
  int curPage = selectorIndex_ / gridsPerPage_;
  int slotCount;
  if (collectionsMode_ && currentCollectionIdx_ < 0) {
    // Browsing list of collections
    slotCount = LibraryIndex::queryCollections(pageCache_, curPage, gridsPerPage_);
  } else if (collectionsMode_ && currentCollectionIdx_ >= 0) {
    // Browsing books within a collection
    slotCount = LibraryIndex::queryCollectionBooks(pageCache_, curPage, gridsPerPage_, currentCollectionIdx_);
    totalBooks_ = LibraryIndex::collectionBookCount(currentCollectionIdx_);
    totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
  } else {
    // Normal book browsing
    slotCount = LibraryIndex::queryPage(
        pageCache_, curPage, gridsPerPage_,
        static_cast<LibraryIndex::SortMode>(currentSort_),
        currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
        static_cast<LibraryIndex::FilterMode>(currentFilter_));
  }
  // If the page had fewer items than requested, update totalBooks_
  if (slotCount == 0 && curPage > 0) {
    totalBooks_ = (collectionsMode_ && currentCollectionIdx_ < 0)
        ? LibraryIndex::totalCollections()
        : LibraryIndex::totalBooks();
    totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
    int lastPage = std::max(0, totalPages_ - 1);
    selectorIndex_ = lastPage * gridsPerPage_;
    if (collectionsMode_ && currentCollectionIdx_ < 0)
      slotCount = LibraryIndex::queryCollections(pageCache_, lastPage, gridsPerPage_);
    else if (collectionsMode_ && currentCollectionIdx_ >= 0)
      slotCount = LibraryIndex::queryCollectionBooks(pageCache_, lastPage, gridsPerPage_, currentCollectionIdx_);
    else
      slotCount = LibraryIndex::queryPage(
          pageCache_, lastPage, gridsPerPage_,
          static_cast<LibraryIndex::SortMode>(currentSort_),
          currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
          static_cast<LibraryIndex::FilterMode>(currentFilter_));
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
  // Start cover generation for missing covers on the new page.
  // Works in normal mode AND inside a collection (where books have covers).
  // Skipped only when viewing the collections list.
  if (!collectionsMode_ || currentCollectionIdx_ >= 0) {
    coverGen_.active = true;
    coverGen_.slot = 0;
    coverGen_.done = 0;
    coverGen_.total = 0;
    coverGen_.pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  }
}

void LibraryActivity::applyFilterAndSort() {
  collectionsMode_ = (currentSort_ == CrossPointSettings::LIBRARY_SORT_COLLECTIONS);
  if (collectionsMode_) {
    currentCollectionIdx_ = -1;
  }
  totalBooks_ = collectionsMode_
      ? LibraryIndex::totalCollections()
      : LibraryIndex::totalMatching(currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
                                     static_cast<LibraryIndex::FilterMode>(currentFilter_));
  totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
  selectorIndex_ = 0;
  pageTitleCacheKey_ = -1;
  cachedRenderSelector_ = -1;
  cachedRenderPage_ = -1;
  cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
  cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
  cachedInfoSearch_.clear();
  cachedCollectionsMode_ = false;
  cachedCollectionIdx_ = -2;
  cachedCollectionName_.clear();
  refreshPageCache();
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
  if (file.size() == 0) { file.close(); Storage.remove(tp.c_str()); return false; }
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

  struct { StrId id; const uint8_t* icon; int iconW; int iconH; CrossPointSettings::LIBRARY_SORT sort; } sorts[] = {
    {StrId::STR_SORT_TITLE_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_ASC},
    {StrId::STR_SORT_TITLE_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_DESC},
    {StrId::STR_SORT_AUTHOR_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC},
    {StrId::STR_SORT_AUTHOR_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC},
    {StrId::STR_SORT_COLLECTIONS, LibraryNewIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_COLLECTIONS},
  };
  for (int i = 0; i < 5; ++i) {
    PopupItem item;
    item.label = I18N.get(sorts[i].id);
    item.icon = sorts[i].icon;
    item.iconW = sorts[i].iconW;
    item.iconH = sorts[i].iconH;
    item.selected = (currentSort_ == sorts[i].sort);
    popupOverlay_.items.push_back(item);
    if (item.selected) {
      popupOverlay_.selectedIndex = i;
      popupOverlay_.startIndex = std::max(0, i - PanelDrawHelper::kMaxVisibleRows / 2);
    }
  }
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

  PopupItem allItem; allItem.label = I18N.get(StrId::STR_ALL_BOOKS);
  allItem.icon = LibraryNewIcon; allItem.iconW = 32; allItem.iconH = 32;
  allItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL);
  popupOverlay_.items.push_back(allItem);

  PopupItem favItem; favItem.label = I18N.get(StrId::STR_FAVOURITES);
  favItem.icon = Heart24Icon; favItem.iconW = 24; favItem.iconH = 24;
  favItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_FAVOURITES);
  popupOverlay_.items.push_back(favItem);

  PopupItem recentItem; recentItem.label = I18N.get(StrId::STR_LATEST_READ);
  recentItem.icon = RecentBooksIcon32; recentItem.iconW = 32; recentItem.iconH = 32;
  recentItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_LATEST_READ);
  popupOverlay_.items.push_back(recentItem);

  PopupItem unreadItem; unreadItem.label = I18N.get(StrId::STR_UNREAD);
  unreadItem.icon = Text24Icon; unreadItem.iconW = 24; unreadItem.iconH = 24;
  unreadItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_UNREAD);
  popupOverlay_.items.push_back(unreadItem);

  PopupItem completedItem; completedItem.label = I18N.get(StrId::STR_COMPLETED);
  completedItem.icon = CleanMonitorIcon32; completedItem.iconW = 32; completedItem.iconH = 32;
  completedItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_COMPLETED);
  popupOverlay_.items.push_back(completedItem);

  PopupItem hiddenItem; hiddenItem.label = I18N.get(StrId::STR_HIDDEN_FILTER);
  hiddenItem.icon = LibraryIcon; hiddenItem.iconW = 32; hiddenItem.iconH = 32;
  hiddenItem.selected = (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_HIDDEN);
  popupOverlay_.items.push_back(hiddenItem);

  PopupItem searchItem; searchItem.label = I18N.get(StrId::STR_SEARCH_LIBRARY);
  searchItem.icon = SearchPlusIcon; searchItem.iconW = 32; searchItem.iconH = 32;
  searchItem.selected = false;
  popupOverlay_.items.push_back(searchItem);

  PopupItem clearItem; clearItem.label = I18N.get(StrId::STR_SEARCH_CLEAR);
  clearItem.icon = SearchMinusIcon; clearItem.iconW = 32; clearItem.iconH = 32;
  clearItem.selected = false;
  popupOverlay_.items.push_back(clearItem);

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
    CrossPointSettings::LIBRARY_SORT sorts[] = {
      CrossPointSettings::LIBRARY_SORT_TITLE_ASC, CrossPointSettings::LIBRARY_SORT_TITLE_DESC,
      CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC,
      CrossPointSettings::LIBRARY_SORT_COLLECTIONS,
    };
    if (idx < 5) {
      currentSort_ = sorts[idx];
      SETTINGS.librarySort = currentSort_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  } else if (popupMode_ == PopupMode::Filter) {
    // Popup order: 0=All, 1=Favourites, 2=Latest, 3=Unread, 4=Completed, 5=Search, 6=Clear
    if (idx == 0) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 1) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_FAVOURITES;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 2) {
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_LATEST_READ;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 3) {
      // Unread
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_UNREAD;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 4) {
      // Completed
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_COMPLETED;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 5) {
      // Hidden
      currentFilter_ = CrossPointSettings::LIBRARY_FILTER_HIDDEN;
      SETTINGS.libraryFilter = currentFilter_;
      SETTINGS.saveToFile();
      rebuildForFilter(currentFilter_);
    } else if (idx == 6) {
      closePopup();
      beginTextSearch();
      return;
    } else if (idx == 7) {
      currentSearchText_.clear();
      SETTINGS.librarySearchText[0] = '\0';
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  }
  closePopup();
}

void LibraryActivity::beginTextSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_LIBRARY), currentSearchText_, 30),
      [this](const ActivityResult& result) {
        if (result.isCancelled) { forceRender_ = true; requestUpdate(); return; }
        const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
        if (!kbResult) { forceRender_ = true; requestUpdate(); return; }
        currentSearchText_ = kbResult->text;
        StringUtils::copyToFixedBuffer(SETTINGS.librarySearchText, sizeof(SETTINGS.librarySearchText), currentSearchText_);
        SETTINGS.saveToFile();
        applyFilterAndSort();
        forceRender_ = true;
        requestUpdate();
      });
}


// ============================================================================
// SECTION 6: Input handling — main loop
// ============================================================================

void LibraryActivity::loop() {
  // ---- Cover generation: one slot per frame, non-blocking -------------------
  if (coverGen_.pending) {
    coverGen_.pending = false;
    coverGen_.active = true;
  }
  if (coverGen_.active) {
    const int total = totalBooks_;
    const int currentPage = total > 0 ? (selectorIndex_ / gridsPerPage_) : 0;

    // If the user changed page during generation, cancel current work
    // and restart on the new page on the next frame.
    if (coverGen_.pageStart != currentPage) {
      coverGen_.active = false;
      coverGen_.pending = true;
      coverGen_.slot = 0;
      coverGen_.done = 0;
      coverGen_.total = 0;
      coverGen_.pageStart = -1;
    } else {
      // First frame: count missing covers, let grid render first
      if (coverGen_.slot == 0 && coverGen_.total == 0) {
        for (int i = 0; i < gridsPerPage_ && (coverGen_.pageStart + i) < total; ++i) {
          if (pageCache_[i].id == 0) continue;
          std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache_[i].path), coverWidth_, coverHeight_);
          if (!Storage.exists(thumbPath.c_str())) {
            ++coverGen_.total;
          } else {
            if (!isBookCoverReady(pageCache_[i].path)) {
              Storage.remove(thumbPath.c_str());
              ++coverGen_.total;
            }
          }
        }
        if (coverGen_.total == 0) {
          coverGen_.active = false;
          coverGen_.pageStart = -1;
        } else {
          LOG_DBG("LIB", "CovGen: start %d missing covers on page", coverGen_.total);
          coverGen_.slot = -1;
          requestUpdate();
        }
      }
      // Second frame onward: process one slot
      else if (coverGen_.slot != 0 || coverGen_.total != 0) {
        if (coverGen_.slot == -1) coverGen_.slot = 0;

        int slot = coverGen_.slot;
        if (slot < gridsPerPage_ && (coverGen_.pageStart + slot) < total && pageCache_[slot].id != 0) {
          std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache_[slot].path), coverWidth_, coverHeight_);
          if (!Storage.exists(thumbPath.c_str())) {
            yield(); esp_task_wdt_reset();
            LOG_DBG("LIB", "CovGen: %d/%d %s heap=%u maxA=%u",
                    coverGen_.done + 1, coverGen_.total, pageCache_[slot].path,
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // Generate cover using CoverGenerator utility
            if (generatePageCover(pageCache_[slot].path)) {
              ++coverGen_.done;
            }
          }
        }

        ++coverGen_.slot;
        if (coverGen_.slot >= gridsPerPage_ || (coverGen_.pageStart + coverGen_.slot) >= total) {
          LOG_DBG("LIB", "CovGen: done %d/%d covers generated", coverGen_.done, coverGen_.total);
          coverGen_.active = false;
          coverGen_.slot = 0;
          coverGen_.done = 0;
          coverGen_.total = 0;
          coverGen_.pageStart = -1;
          forceRender_ = true;
          requestUpdate();
        }
      }
    }
  }

  // ---- Popup input handling -----------------------------------------------
  if (popupMode_ != PopupMode::None) {
    if (popupSpawnButton_ >= 0 &&
        mappedInput.wasReleased(static_cast<MappedInputManager::Button>(popupSpawnButton_))) {
      popupSpawnButton_ = -1;
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { closePopup(); return; }

    int itemCount = static_cast<int>(popupOverlay_.items.size());
    int& sel = popupOverlay_.selectedIndex;
    int& start = popupOverlay_.startIndex;
    int visible = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (sel > 0) { sel--; if (sel < start) start = sel; }
      else { sel = itemCount - 1; start = std::max(0, itemCount - visible); }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (sel < itemCount - 1) { sel++; if (sel >= start + visible) start = sel - visible + 1; }
      else { sel = 0; start = 0; }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { selectPopupItem(); return; }
    return;
  }

  const int total = totalBooks_;
  ensureLayoutUpToDate();

  // ---- Cover generation (blocks input while running) ----------------------
  // *** DISABLED: cover generation has been removed from the library path.
  // *** Only placeholders are shown. Covers generated by the Home screen
  // *** reader (Epub::generateThumbBmp) are still picked up if they exist.
  // *** This avoids memory pressure, corrupt-ZIP crashes, and O(n) heap
  // *** fragmentation during library browsing.
  // --------------------------------------------------------------------------

  // ---- Empty library state ------------------------------------------------
  if (total <= 0) {
    upPress_.reset();
    downPress_.reset();
    coverGen_.active = false;
    coverGen_.pending = false;
    coverGen_.slot = 0;
    coverGen_.done = 0;
    coverGen_.total = 0;
    coverGen_.pageStart = -1;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      upPress_.reset();
      downPress_.reset();
      // Silent restart to reclaim fragmented heap before returning.
      // Library browsing fragments the heap significantly (cache, thumbnails,
      // book index vectors). A full ESP.restart gives the system a clean slate.
      LOG_DBG("LIB", "Back at root: requesting seamless silent restart (free=%d maxA=%d)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
      // Long press: context menu (skip for collections list items)
      if (held >= long_press::kDefaultMs) {
        const int idx = selectorIndex_;
        const int slot = idx % gridsPerPage_;
        const std::string path(pageCache_[slot].path);
        if (collectionsMode_ && currentCollectionIdx_ < 0) {
          return;  // no context menu on collections list items
        }
        const std::string title = pageCache_[slot].title[0] ? pageCache_[slot].title : book_filter::filenameWithoutExtension(path);
        const bool isEpub = FsHelpers::hasEpubExtension(std::string_view{path.c_str()});
        const bool isFav = FAVORITES.isFavorite(path);
        const auto* stats = READING_STATS.findBook(path);
        const bool isCompleted = stats && stats->completed;
        const bool isHidden = HIDDEN_BOOKS.isHidden(path);

        startActivityForResult(
            std::make_unique<BookContextMenuActivity>(renderer, mappedInput, title, isFav, isCompleted, isEpub, true, isHidden),
            [this, idx, slot, path, title, isEpub](const ActivityResult& result) {
              if (result.isCancelled) { forceRender_ = true; requestUpdate(); return; }
              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) { forceRender_ = true; requestUpdate(); return; }
              switch (static_cast<BookContextMenuActivity::MenuAction>(menuResult->action)) {
                case BookContextMenuActivity::MenuAction::OPEN_BOOK: onSelectBook(path); return;
                case BookContextMenuActivity::MenuAction::VIEW_STATS:
                  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { forceRender_ = true; requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES:
                  FAVORITES.toggleBook(path); forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::MARK_READ_UNREAD: {
                  const auto* s = READING_STATS.findBook(path);
                  const bool wasCompleted = s && s->completed;
                  READING_STATS.beginSession(path, title,
                                              pageCache_[slot].title[0] ? pageCache_[slot].title : "",
                                              LibraryIndex::thumbPathFor(path, coverWidth_, coverHeight_),
                                            wasCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  forceRender_ = true; requestUpdate(); return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  deleteLibraryCovers(path);
                  refreshPageCache();
                  forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                          tr(STR_LIBRARY_DELETE_PAGE_COVERS), tr(STR_LIBRARY_DELETE_PAGE_COVERS_CONFIRM)),
                      [this](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          deletePageCovers();
                        }
                        refreshPageCache();
                        // Defer generation by one frame so the grid is drawn
                        // before the generation loop blocks the renderer.
                        coverGen_.active = false;
                        coverGen_.pending = true;
                        coverGen_.slot = 0;
                        coverGen_.done = 0;
                        coverGen_.total = 0;
                        coverGen_.pageStart = -1;
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                          tr(STR_LIBRARY_DELETE_ALL_COVERS), tr(STR_LIBRARY_DELETE_ALL_COVERS_CONFIRM)),
                      [this](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          deleteAllLibraryCovers();
                        }
                        refreshPageCache();
                        coverGen_.active = false;
                        coverGen_.pending = true;
                        coverGen_.slot = 0;
                        coverGen_.done = 0;
                        coverGen_.total = 0;
                        coverGen_.pageStart = -1;
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                case BookContextMenuActivity::MenuAction::HIDE_BOOK:
                  HIDDEN_BOOKS.toggleBook(path);
                  // Reload grid and counts: hidden books must disappear / reappear.
                  // Select first available book on the current page.
                  selectorIndex_ = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
                  totalBooks_ = LibraryIndex::totalMatching(
                      currentSearchText_.empty() ? nullptr : currentSearchText_.c_str(),
                      static_cast<LibraryIndex::FilterMode>(currentFilter_));
                  totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
                  if (selectorIndex_ >= totalBooks_) selectorIndex_ = 0;
                  refreshPageCache();
                  forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_BOOK_FILE: {
                  const bool isManual = (SETTINGS.libraryUpdateMode != CrossPointSettings::LIBRARY_UPDATE_AUTO);
                  const char* confirmMsg = isManual ? tr(STR_DELETE_BOOK_FILE_CONFIRM_MANUAL)
                                                    : tr(STR_DELETE_BOOK_FILE_CONFIRM);
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                          tr(STR_DELETE_BOOK_FILE), confirmMsg),
                      [this, path](const ActivityResult& r) {
                        if (!r.isCancelled) {
                          deleteBookFile(path);
                          if (SETTINGS.libraryUpdateMode == CrossPointSettings::LIBRARY_UPDATE_AUTO) {
                            // Force re-scan now — the file is gone but the
                            // in-RAM index still has its entry.
                            forceScanOnNextOpen_ = true;
                            scanSd();
                            selectorIndex_ = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
                            if (selectorIndex_ >= totalBooks_) selectorIndex_ = 0;
                          }
                        }
                        forceRender_ = true;
                        requestUpdate();
                      });
                  return;
                }
                default: forceRender_ = true; requestUpdate(); return;
              }
            });
        return;
      }
      // Short press: open book or enter collection
      if (collectionsMode_ && currentCollectionIdx_ < 0) {
        // Enter the selected collection
        int slot = selectorIndex_ % gridsPerPage_;
        currentCollectionIdx_ = static_cast<int>(pageCache_[slot].id);
        currentCollectionName_ = pageCache_[slot].title;
        selectorIndex_ = 0;
        refreshPageCache();
        forceRender_ = true;
        requestUpdate();
        return;
      }
      onSelectBook(std::string(pageCache_[selectorIndex_ % gridsPerPage_].path));
      return;
    }
  }

  // ---- Back button --------------------------------------------------------
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // In collections mode: go back to collections list from a specific collection
    if (collectionsMode_ && currentCollectionIdx_ >= 0) {
      currentCollectionIdx_ = -1;
      currentCollectionName_.clear();
      totalBooks_ = LibraryIndex::totalCollections();
      totalPages_ = (totalBooks_ + gridsPerPage_ - 1) / gridsPerPage_;
      selectorIndex_ = 0;
      refreshPageCache();
      forceRender_ = true;
      requestUpdate();
      return;
    }
    if (upPress_.wasPressed() || downPress_.wasPressed() || leftPress_.wasPressed() || rightPress_.wasPressed()) {
      upPress_.reset();
      downPress_.reset();
      leftPress_.reset();
      rightPress_.reset();
    } else {
      LOG_DBG("LIB", "Back at root: requesting seamless silent restart (free=%d maxA=%d)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
  const bool infoKeyChanged =
      cachedRenderSelector_ != selectorIndex_ || cachedRenderPage_ != curPageRaw ||
      cachedInfoFilter_ != currentFilter_ || cachedInfoSort_ != currentSort_ ||
      cachedInfoSearch_ != currentSearchText_ || cachedTotalBooks_ != totalBooks_ ||
      cachedCollectionsMode_ != collectionsMode_ || cachedCollectionIdx_ != currentCollectionIdx_ ||
      cachedCollectionName_ != currentCollectionName_;
  if (!infoKeyChanged) return false;

  cachedInfo_.clear();
  switch (currentFilter_) {
    case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: cachedInfo_ = tr(STR_FAVOURITES); break;
    case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: cachedInfo_ = tr(STR_LATEST_READ); break;
    case CrossPointSettings::LIBRARY_FILTER_UNREAD:     cachedInfo_ = tr(STR_UNREAD); break;
    case CrossPointSettings::LIBRARY_FILTER_COMPLETED:  cachedInfo_ = tr(STR_COMPLETED); break;
    case CrossPointSettings::LIBRARY_FILTER_HIDDEN:     cachedInfo_ = tr(STR_HIDDEN_FILTER); break;
    default: cachedInfo_ = collectionsMode_ ? tr(STR_SORT_COLLECTIONS) : tr(STR_ALL_BOOKS); break;
  }
  if (collectionsMode_ && currentCollectionIdx_ >= 0 && !currentCollectionName_.empty()) {
    cachedInfo_ = currentCollectionName_;
  }
  const char* sortLabel = nullptr;
  // Don't show sort label when in collections mode — the info line
  // already says "Collections" or "Collections / Name".
  if (!collectionsMode_) {
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:  sortLabel = tr(STR_SORT_TITLE_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC: sortLabel = tr(STR_SORT_TITLE_DESC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: sortLabel = tr(STR_SORT_AUTHOR_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: sortLabel = tr(STR_SORT_AUTHOR_DESC); break;
      default: break;
    }
  }
  if (sortLabel && sortLabel[0]) { cachedInfo_ += " / "; cachedInfo_ += sortLabel; }
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
    cachedSelTitle_ = pageCache_[slot].title[0] ? pageCache_[slot].title : book_filter::filenameWithoutExtension(pageCache_[slot].path);
    cachedSelAuthor_ = pageCache_[slot].author;
    const int maxSelW = pageWidth - 16;  // 8px margin each side
    cachedSelTitle_ = renderer.truncatedText(UI_10_FONT_ID, cachedSelTitle_.c_str(), maxSelW, EpdFontFamily::BOLD);
  } else {
    cachedSelTitle_.clear();
    cachedSelAuthor_.clear();
  }
}

void LibraryActivity::render(RenderLock&&) {
  esp_task_wdt_reset();
  const int total = totalBooks_;
  const int curPageRaw = total > 0 ? selectorIndex_ / gridsPerPage_ : 0;

  // ---- Early-out guard: nothing changed -----------------------------------
  if (!forceRender_ && popupMode_ == PopupMode::None &&
      curPageRaw == lastRenderedPage_ && selectorIndex_ == lastRenderedSelectorIndex_) {
    return;
  }

  // ---- PARTIAL RENDER: selection moved within same page -------------------
  // Only erases old border + title/author, draws new ones. No clearScreen().
  if (!forceRender_ && popupMode_ == PopupMode::None &&
      curPageRaw == lastRenderedPage_ &&
      selectorIndex_ != lastRenderedSelectorIndex_ && total > 0) {

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
              std::string author = renderer.truncatedText(UI_10_FONT_ID, cachedSelAuthor_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
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
  const bool forcedRender = forceRender_;
  forceRender_ = false;

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = total > 0 ? curPageRaw + 1 : 0;

  LOG_DBG("LIB", "Render: start free=%u maxA=%u total=%d page=%d",
          ESP.getFreeHeap(), ESP.getMaxAllocHeap(), total, curPage);

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
      std::string author = renderer.truncatedText(UI_10_FONT_ID, cachedSelAuthor_.c_str(), pageWidth - 16, EpdFontFamily::REGULAR);
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
        pageTitleCache_.push_back(renderer.wrappedText(SMALL_FONT_ID, t.c_str(),
                                                       coverWidth_ - 2 * kCoverTextPad, 3, EpdFontFamily::BOLD));
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
        if (p == curPage - 1) renderer.fillRect(dx, sy, DS, DS, true);
        else renderer.drawRect(dx, sy, DS, DS, true);
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

  lastRenderedSelectorIndex_ = selectorIndex_;
  lastRenderedPage_ = curPageRaw;
  prevBorderIdx_ = selectorIndex_;

  renderer.displayBuffer();
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
  const std::string thumbPath = LibraryIndex::thumbPathFor(path, coverWidth_, coverHeight_);
  const bool hasThumb = !thumbPath.empty() && Storage.exists(thumbPath.c_str());

  if (hasThumb) {
    FsFile file;
    if (Storage.openFileForRead("LIB", thumbPath, file)) {
      Bitmap bmp(file);
      if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
        const float bmpRatio = static_cast<float>(bmp.getWidth()) / static_cast<float>(bmp.getHeight());
        const float tileRatio = static_cast<float>(coverWidth_) / static_cast<float>(coverHeight_);
        const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
        const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
        renderer.fillRoundedRect(x, y, coverWidth_, coverHeight_, COVER_CORNER_RADIUS, Color::White);
        renderer.drawBitmap(bmp, x, y, coverWidth_, coverHeight_, cropX, cropY);
        drawn = true;
      }
      file.close();
    }
  }

  if (!drawn) {
    renderer.drawRoundedRect(x, y, coverWidth_, coverHeight_, 1, COVER_CORNER_RADIUS, true);
    renderer.fillRoundedRect(x, y + coverHeight_ / 3, coverWidth_, 2 * coverHeight_ / 3 + 1,
                             COVER_CORNER_RADIUS, false, false, true, true, Color::Black);
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

  // Ribbon badge — shows on both covers AND placeholders
  {
    const bool isFav = pageCache_[i].isFavorite;
    const bool isComplete = pageCache_[i].isCompleted;
    const bool isOpened = pageCache_[i].isOpened && !isComplete;
    if (isComplete || isFav || isOpened)
      drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
  }
}


// ============================================================================
// SECTION 9: Cover deletion helpers
// ============================================================================

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  std::string thumbPath = LibraryCache::thumbPathFor(bookPath, coverWidth_, coverHeight_);
  LOG_DBG("LIB", "DelCovers: single path=%s thumb=%s exists=%d", bookPath.c_str(), thumbPath.c_str(),
          !thumbPath.empty() && Storage.exists(thumbPath.c_str()));
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
    LOG_DBG("LIB", "DelCovers: removed %s", thumbPath.c_str());
  }
}

void LibraryActivity::deletePageCovers() {
  int slotCount = gridsPerPage_;
  LOG_DBG("LIB", "DelCovers: page range [0..%d) total=%d sel=%d grids=%d", slotCount, totalBooks_, selectorIndex_, gridsPerPage_);
  for (int i = 0; i < slotCount && pageCache_[i].id != 0; ++i) {
    std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache_[i].path), coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
      LOG_DBG("LIB", "DelCovers: removed [%d] %s -> %s", i, pageCache_[i].path, thumbPath.c_str());
    }
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  LOG_DBG("LIB", "DelCovers: ALL total=%d", totalBooks_);
  // Walk all pages and delete thumbs (slow but thorough)
  int pg = 0;
  LibraryIndex::BookRef buf[kMaxPageSlots];
  while (true) {
    int n = LibraryIndex::queryPage(buf, pg, kMaxPageSlots, static_cast<LibraryIndex::SortMode>(currentSort_));
    if (n == 0) break;
    for (int i = 0; i < n; ++i) {
      std::string thumbPath = LibraryIndex::thumbPathFor(std::string(buf[i].path), coverWidth_, coverHeight_);
      if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
        Storage.remove(thumbPath.c_str());
      }
    }
    ++pg;
  }
}

bool LibraryActivity::generatePageCover(const std::string& path) {
  // Delegates to CoverGenerator utility for consistent cover generation
  return CoverGenerator::generateCover(path, coverWidth_, coverHeight_);
}