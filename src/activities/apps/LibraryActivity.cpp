#include "LibraryActivity.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <CoverDebugLog.h>
#include <HomepageDebugLog.h>
#include <FontCacheManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numeric>
#include <vector>

#include "../home/BookContextMenuActivity.h"
#include "../home/BookMetadataActivity.h"
#include "../util/ConfirmationActivity.h"
#include "../util/KeyboardEntryActivity.h"
#include "CrossPointSettings.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/LibraryCache.h"
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
#include "components/icons/text24.h"
#include "components/icons/time_fast.h"
#include "components/icons/transfer.h"
#include "components/LibraryPopupOverlay.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Compile-time verification: the largest icon (32×32 1‑bpp) is exactly 128 B.
static_assert(sizeof(CoverIcon) == 128, "unexpected icon size, update kMaxIconBytes");

namespace {
constexpr int COVER_CORNER_RADIUS = 2;

static void fillTopRightTri(GfxRenderer& r, int x, int y, int leg, bool black) {
  for (int dy = 0; dy < leg; ++dy)
    r.fillRect(x + dy, y + dy, leg - dy, 1, black);
}

void drawCyberpunkSelectionBorder(const GfxRenderer& renderer, int x, int y, int w, int h) {
  constexpr int c = 4;
  constexpr int cl = 5;
  constexpr int cg = 2;
  const int bx = x - 5;
  const int by = y - 5;
  const int bw = w + 10;
  const int bh = h + 10;
  renderer.drawRect(bx, by, bw, bh, true);
  renderer.drawLine(bx + cg, by, bx + cg + cl, by, 1, true);
  renderer.drawLine(bx, by + cg, bx, by + cg + cl, 1, true);
  renderer.drawLine(bx + bw - cg - cl, by, bx + bw - cg, by, 1, true);
  renderer.drawLine(bx + bw, by + cg, bx + bw, by + cg + cl, 1, true);
  renderer.drawLine(bx + cg, by + bh, bx + cg + cl, by + bh, 1, true);
  renderer.drawLine(bx, by + bh - cg, bx, by + bh - cg - cl, 1, true);
  renderer.drawLine(bx + bw - cg - cl, by + bh, bx + bw - cg, by + bh, 1, true);
  renderer.drawLine(bx + bw, by + bh - cg, bx + bw, by + bh - cg - cl, 1, true);
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
    constexpr int kHeartNativeSz = 32;
    if (leg >= kHeartNativeSz) {
      int hx = symCx - kHeartNativeSz / 2;
      int hy = symCy - kHeartNativeSz / 2;
      r.drawIconInverted(::HeartIcon, hx, hy, kHeartNativeSz, kHeartNativeSz);
    }
  } else if (opened) {
    const int dotR = std::max(1, symSz / 4);
    for (int y2 = -dotR; y2 <= dotR; ++y2)
      for (int x2 = -dotR; x2 <= dotR; ++x2)
        if (x2 * x2 + y2 * y2 <= dotR * dotR + dotR)
          r.drawLine(symCx + x2, symCy + y2, symCx + x2, symCy + y2, 1, false);
  }
}

// Normalize a single char for sort comparison: strip accents then lowercase.
char normalizeChar(unsigned char c) {
  switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'a';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'e';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'i';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'o';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'u';
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD1: case 0xF1: return 'n';
    case 0xC7: case 0xE7: return 'c';
    default: break;
  }
  return static_cast<char>(std::tolower(c));
}

std::string normalizeForSort(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    out.push_back(normalizeChar(static_cast<unsigned char>(s[i])));
  }
  return out;
}

/// Compare two strings using the same normalisation as normalizeForSort,
/// but without allocating any heap memory — works on a on-stack buffer.
/// Returns -1, 0, or +1 (like strcmp).
static int compareNormalized(const std::string& a, const std::string& b) {
  const size_t na = a.size();
  const size_t nb = b.size();
  const size_t n = std::min(na, nb);
  for (size_t i = 0; i < n; ++i) {
    const char ca = normalizeChar(static_cast<unsigned char>(a[i]));
    const char cb = normalizeChar(static_cast<unsigned char>(b[i]));
    if (ca != cb) return (ca < cb) ? -1 : 1;
  }
  if (na != nb) return (na < nb) ? -1 : 1;
  return 0;
}

std::string filenameWithoutExtension(const std::string& path) {
  std::string name = path;
  const size_t lastSlash = name.find_last_of('/');
  if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);
  const size_t lastDot = name.find_last_of('.');
  if (lastDot != std::string::npos && lastDot > 0) name = name.substr(0, lastDot);
  return name;
}

// Helper: check if a book passes a filter criterion.
// Loads only the entry's path from cache for efficiency.
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

void LibraryActivity::ensureWindowForIndex(int index) {
  if (index < 0 || index >= totalBooks_) {
    windowEntries_.clear();
    windowStart_ = 0;
    windowCount_ = 0;
    return;
  }
  // If the index is already within the window, no reload needed.
  if (index >= windowStart_ && index < windowStart_ + windowCount_) return;

  // Load a new window centered on the requested index.
  // Align windowStart to a multiple of gridsPerPage_ for consistent page boundaries.
  const int newStart = (index / gridsPerPage_) * gridsPerPage_;
  const int loadCount = std::min(LibraryCache::kEntryWindow, totalBooks_ - newStart);

  std::vector<LibraryCache::Entry> fresh;
  const int got = LibraryCache::loadPage(fresh, newStart, loadCount);
  if (got > 0) {
    windowEntries_.swap(fresh);
    windowStart_ = newStart;
    windowCount_ = got;
  } else {
    // loadPage failed — keep current window, force re-sync
    LOG_ERR("LIB", "ensureWindowForIndex(%d): loadPage failed, window intact", index);
  }
}

bool LibraryActivity::isBookCoverReady(const std::string& path, size_t slot) const {
  const std::string tp = LibraryCache::thumbPathFor(path, coverWidth_, coverHeight_);
  if (!tp.empty() && Storage.exists(tp.c_str())) return true;
  if (slot < 64 && (coverGeneratedMask_ & (uint64_t{1} << slot))) return true;
  return false;
}

// ── Filter / Sort ────────────────────────────────────────────────────────

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  // We can't filter without loading entries.  Strategy: scan the cache
  // sequentially, check filter for each entry, collect matching global indices.
  // Then load the first page of matching books.
  //
  // For large libraries this is O(n) on SD read.  Filter changes are user-triggered
  // and infrequent (not in a hot loop), so sequential scan is acceptable.

  const int total = LibraryCache::getCount();
  if (total <= 0) {
    // No cache — must do a full scan
    renderer.clearScreen();
    Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
    GUI.fillPopupProgress(renderer, popupRect, 0);
    renderer.displayBuffer();
    LibraryCache::scan(renderer, popupRect, SETTINGS.libraryRootDir);
    totalBooks_ = LibraryCache::getCount();
    currentFilter_ = filter;
    selectorIndex_ = 0;
    coversComplete_ = false;
    coverGenIndex_ = -1;
    forceRender_ = true;
    pageTitleCacheKey_ = -1;
    cachedRenderSelector_ = -1;
    cachedRenderPage_ = -1;
    cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
    cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
    cachedInfoSearch_.clear();
    ensureWindowForIndex(0);
    return;
  }

  // For ALL filter: just set totalBooks_ and load the first page.
  if (filter == CrossPointSettings::LIBRARY_FILTER_ALL) {
    totalBooks_ = LibraryCache::getCount();
  } else {
    // For FAVOURITES and LATEST_READ filters, we need to count matching books.
    // We scan the cache sequentially, loading batches of entries.
    int matchingCount = 0;
    int batchStart = 0;
    constexpr int kBatchSize = 64;
    while (batchStart < total) {
      std::vector<LibraryCache::Entry> batch;
      const int got = LibraryCache::loadPage(batch, batchStart, kBatchSize);
      if (got <= 0) break;
      for (int i = 0; i < got; ++i) {
        if (includeBookByFilter(batch[i], filter)) ++matchingCount;
      }
      batchStart += got;
    }
    totalBooks_ = matchingCount;

    // For filter display, we load ALL matching entries into windowEntries_.
    // This is the one case where we load more than kEntryWindow entries.
    // But filters are subsets (favorites, recent) — typically small.
    // If too large, we fall back to showing only the first page.
    if (totalBooks_ > LibraryCache::kEntryWindow) {
      // Just load the first page worth of entries (from the sorted cache,
      // filtered: we need to scan and collect matching ones).
      std::vector<LibraryCache::Entry> filtered;
      filtered.reserve(LibraryCache::kEntryWindow);
      int batchStart2 = 0;
      while (batchStart2 < total && static_cast<int>(filtered.size()) < LibraryCache::kEntryWindow) {
        std::vector<LibraryCache::Entry> batch;
        const int got = LibraryCache::loadPage(batch, batchStart2, kBatchSize);
        if (got <= 0) break;
        for (int i = 0; i < got && static_cast<int>(filtered.size()) < LibraryCache::kEntryWindow; ++i) {
          if (includeBookByFilter(batch[i], filter)) {
            filtered.push_back(std::move(batch[i]));
          }
        }
        batchStart2 += got;
      }
      windowEntries_.swap(filtered);
      windowStart_ = 0;
      windowCount_ = static_cast<int>(windowEntries_.size());
    } else {
      // Load all matching entries
      std::vector<LibraryCache::Entry> filtered;
      filtered.reserve(totalBooks_);
      int batchStart2 = 0;
      while (batchStart2 < total) {
        std::vector<LibraryCache::Entry> batch;
        const int got = LibraryCache::loadPage(batch, batchStart2, kBatchSize);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i) {
          if (includeBookByFilter(batch[i], filter)) {
            filtered.push_back(std::move(batch[i]));
          }
        }
        batchStart2 += got;
      }
      windowEntries_.swap(filtered);
      windowStart_ = 0;
      windowCount_ = static_cast<int>(windowEntries_.size());
    }
  }

  currentFilter_ = filter;
  applyFilterAndSort();
}

void LibraryActivity::applyFilterAndSort() {
  // If search text was cleared but windowEntries_ was previously filtered
  // (e.g. after a search), reload the first page from the cache file.
  if (currentSearchText_.empty() && windowStart_ == 0 && windowCount_ < totalBooks_ &&
      currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL) {
    // Reload fresh page from cache — windowEntries_ may be a stale filtered subset.
    ensureWindowForIndex(0);
  }

  // For search filtering, we also filter here.
  if (currentSearchText_.empty()) {
    // No search: windowEntries_ is already the right set.
    // totalBooks_ was set by the caller (scanSd, rebuildForFilter).
  } else {
    // Search text: filter windowEntries_ by title/author/path.
    const std::string searchLower = normalizeForSort(currentSearchText_);
    std::vector<LibraryCache::Entry> filtered;
    filtered.reserve(windowEntries_.size());
    for (const auto& e : windowEntries_) {
      if (normalizeForSort(e.title).find(searchLower) != std::string::npos) {
        filtered.push_back(e);
        continue;
      }
      if (!e.author.empty() && normalizeForSort(e.author).find(searchLower) != std::string::npos) {
        filtered.push_back(e);
        continue;
      }
      if (normalizeForSort(e.path).find(searchLower) != std::string::npos) {
        filtered.push_back(e);
      }
    }
    windowEntries_.swap(filtered);
    windowStart_ = 0;
    windowCount_ = static_cast<int>(windowEntries_.size());
    totalBooks_ = windowCount_;
  }

  const int n = static_cast<int>(windowEntries_.size());
  HOMEPAGE_LOG("LIB", "filter+sort: n=%d heap=%u maxA=%u", n, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (n > 1) {
    struct SortMeta {
      uint32_t lastReadAt = 0;
      uint8_t progress = 0;
      bool completed = false;
    };
    std::vector<SortMeta> meta(n);
    for (int i = 0; i < n; ++i) {
      const auto* s = READING_STATS.findBook(windowEntries_[i].path);
      if (s) {
        meta[i].lastReadAt = s->lastReadAt;
        meta[i].progress = s->lastProgressPercent;
        meta[i].completed = s->completed;
      }
    }

    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    yield();
    esp_task_wdt_reset();
    std::sort(idx.begin(), idx.end(), [&](int a, int b) -> bool {
      const auto& ea = windowEntries_[a];
      const auto& eb = windowEntries_[b];
      const auto& ma = meta[a];
      const auto& mb = meta[b];
      switch (currentSort_) {
        case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:
          return compareNormalized(ea.title, eb.title) < 0;
        case CrossPointSettings::LIBRARY_SORT_TITLE_DESC:
          return compareNormalized(ea.title, eb.title) > 0;
        case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: {
          const int cmp = compareNormalized(ea.author.empty() ? "zzz" : ea.author,
                                            eb.author.empty() ? "zzz" : eb.author);
          if (cmp != 0) return cmp < 0;
          return compareNormalized(ea.title, eb.title) < 0;
        }
        case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: {
          const int cmp = compareNormalized(ea.author.empty() ? "zzz" : ea.author,
                                            eb.author.empty() ? "zzz" : eb.author);
          if (cmp != 0) return cmp > 0;
          return compareNormalized(ea.title, eb.title) > 0;
        }
        case CrossPointSettings::LIBRARY_SORT_RECENT:
          if (ma.lastReadAt != mb.lastReadAt) return ma.lastReadAt > mb.lastReadAt;
          return compareNormalized(ea.title, eb.title) < 0;
        case CrossPointSettings::LIBRARY_SORT_PROGRESS:
          if (ma.completed != mb.completed) return ma.completed;
          if (ma.progress != mb.progress) return ma.progress > mb.progress;
          return compareNormalized(ea.title, eb.title) < 0;
      }
      return a < b;
    });

    // Reorder windowEntries_ according to sorted index.
    std::vector<LibraryCache::Entry> reordered;
    reordered.reserve(n);
    for (int i : idx)
      reordered.push_back(std::move(windowEntries_[i]));
    windowEntries_.swap(reordered);
  }

  selectorIndex_ = 0;
  coversComplete_ = false;
  coverGenIndex_ = -1;
  lastPage_ = 0;
  pageTitleCacheKey_ = -1;
  cachedRenderSelector_ = -1;
  cachedRenderPage_ = -1;
  cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
  cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
  cachedInfoSearch_.clear();
  forceRender_ = true;
}

// ── Scan SD ──────────────────────────────────────────────────────────────

void LibraryActivity::scanSd() {
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;

  // Try incremental sync first; falls back to full scan when cache is missing.
  if (LibraryCache::sync(&renderer, nullptr, SETTINGS.libraryRootDir)) {
    HOMEPAGE_LOG("LIB", "scanSd: sync ok, getCount=%d", LibraryCache::getCount());
    totalBooks_ = LibraryCache::getCount();

    // Apply filter: if ALL, just load the first page.
    if (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL) {
      ensureWindowForIndex(0);
      applyFilterAndSort();
    } else {
      // For non-ALL filters, rebuildForFilter does the filtering.
      rebuildForFilter(currentFilter_);
      return; // rebuildForFilter calls applyFilterAndSort internally
    }

    HOMEPAGE_LOG("LIB", "scanSd: after filter+sort window=%zu heap=%u maxA=%u",
                 windowEntries_.size(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    LOG_DBG("LIB", "Synced library cache (root=%s), total=%d", SETTINGS.libraryRootDir, totalBooks_);
    return;
  }

  // Cache unavailable – full scan with progress popup.
  renderer.clearScreen();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
  GUI.fillPopupProgress(renderer, popupRect, 0);
  renderer.displayBuffer();

  LibraryCache::scan(renderer, popupRect, SETTINGS.libraryRootDir);
  totalBooks_ = LibraryCache::getCount();

  if (currentFilter_ == CrossPointSettings::LIBRARY_FILTER_ALL) {
    ensureWindowForIndex(0);
    applyFilterAndSort();
  } else {
    rebuildForFilter(currentFilter_);
  }
}

// ---- Popup Methods ----

void LibraryActivity::openSortPopup() {
  popupMode_ = PopupMode::Sort;
  popupOverlay_.title = I18N.get(StrId::STR_LIBRARY_SORT);
  popupOverlay_.items.clear();
  popupOverlay_.selectedIndex = 0;
  popupOverlay_.startIndex = 0;
  upHeld_ = downHeld_ = false;
  upLongTriggered_ = downLongTriggered_ = false;

  struct { StrId id; const uint8_t* icon; int iconW; int iconH; CrossPointSettings::LIBRARY_SORT sort; } sorts[] = {
    {StrId::STR_SORT_TITLE_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_ASC},
    {StrId::STR_SORT_TITLE_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_TITLE_DESC},
    {StrId::STR_SORT_AUTHOR_ASC, SortAscIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC},
    {StrId::STR_SORT_AUTHOR_DESC, SortDescIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC},
    {StrId::STR_SORT_RECENT, RecentBooksIcon32, 32, 32, CrossPointSettings::LIBRARY_SORT_RECENT},
    {StrId::STR_SORT_PROGRESS, TimeFastIcon, 32, 32, CrossPointSettings::LIBRARY_SORT_PROGRESS},
  };
  for (int i = 0; i < 6; ++i) {
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
  upHeld_ = downHeld_ = false;
  upLongTriggered_ = downLongTriggered_ = false;

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
      CrossPointSettings::LIBRARY_SORT_RECENT, CrossPointSettings::LIBRARY_SORT_PROGRESS,
    };
    if (idx < 6) {
      currentSort_ = sorts[idx];
      SETTINGS.librarySort = currentSort_;
      SETTINGS.saveToFile();
      applyFilterAndSort();
    }
  } else if (popupMode_ == PopupMode::Filter) {
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
      closePopup();
      beginTextSearch();
      return;
    } else if (idx == 4) {
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
        strncpy(SETTINGS.librarySearchText, currentSearchText_.c_str(), sizeof(SETTINGS.librarySearchText) - 1);
        SETTINGS.librarySearchText[sizeof(SETTINGS.librarySearchText) - 1] = '\0';
        SETTINGS.saveToFile();
        applyFilterAndSort();
        forceRender_ = true;
        requestUpdate();
      });
}

// ---- Lifecycle ----

void LibraryActivity::onEnter() {
  Activity::onEnter();
  HOMEPAGE_LOG("LIB", "onEnter: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  applyLayoutFromSettings();
  selectorIndex_ = 0; coverGenIndex_ = -1; coversComplete_ = false;
  lastPage_ = 0;
  lastRenderedPage_ = -1; forceRender_ = true;
  popupMode_ = PopupMode::None;
  upHeld_ = false; upLongTriggered_ = false;
  downHeld_ = false; downLongTriggered_ = false;
  popupSpawnButton_ = -1;
  lastLayoutSetting_ = SETTINGS.libraryLayout;
  windowEntries_.clear();
  windowStart_ = 0; windowCount_ = 0; totalBooks_ = 0;
  HOMEPAGE_LOG("LIB", "onEnter: before scanSd heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  scanSd();
  HOMEPAGE_LOG("LIB", "onEnter: after scanSd window=%zu total=%d heap=%u maxA=%u",
               windowEntries_.size(), totalBooks_, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  requestUpdate();
}

void LibraryActivity::ensureLayoutUpToDate() {
  if (SETTINGS.libraryLayout != lastLayoutSetting_) {
    applyLayoutFromSettings();
    lastLayoutSetting_ = SETTINGS.libraryLayout;
    coversComplete_ = false;
    coverGenIndex_ = -1;
    lastPage_ = selectorIndex_ / gridsPerPage_;
    forceRender_ = true;
  }
}

void LibraryActivity::onExit() {
  Activity::onExit();
  windowEntries_.clear();
  pageTitleCache_.clear();
  pageTitleCacheKey_ = -1;
}

void LibraryActivity::loop() {
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

  // ---- Cover generation: one thumb per frame, block input ----
  if (!coversComplete_ && total > 0 && windowCount_ > 0) {
    const int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    const int pageCount = std::min(gridsPerPage_, total - pageStart);

    if (coverGenIndex_ < 0) {
      coverGeneratedMask_ = 0;
      coverPassCount_ = 0;
      coverGenRenderBatch_ = 0;
      bool allReady = true;
      for (int s = 0; s < pageCount && allReady; ++s) {
        const int idx = pageStart + s;
        const int winIdx = idx - windowStart_;
        if (winIdx < 0 || winIdx >= windowCount_) {
          // Entry outside the window — load it.
          ensureWindowForIndex(idx);
          // After ensureWindowForIndex, re-check
          const int winIdx2 = idx - windowStart_;
          if (winIdx2 < 0 || winIdx2 >= windowCount_) { allReady = false; break; }
        }
        if (!isBookCoverReady(windowEntries_[idx - windowStart_].path, static_cast<size_t>(s))) allReady = false;
      }
      if (allReady) {
        COVER_LOG("LIB", "Covers: ALL READY pageStart=%d pageCount=%d", pageStart, pageCount);
        coversComplete_ = true;
        coverGenIndex_ = -1;
        coverGeneratedMask_ = 0;
        coverGenRenderBatch_ = 0;
        forceRender_ = true;
        requestUpdate();
        return;
      }
      forceRender_ = true;
      requestUpdate();
      coverGenIndex_ = 0;
      return;
    }

    bool generatedOne = false;
    for (int attempt = 0; attempt < pageCount && !generatedOne; ++attempt) {
      const int slot = (coverGenIndex_ + attempt) % pageCount;
      const int idx = pageStart + slot;
      const int winIdx = idx - windowStart_;
      if (winIdx < 0 || winIdx >= windowCount_) {
        ensureWindowForIndex(idx);
      }
      const int winIdx2 = idx - windowStart_;
      if (winIdx2 < 0 || winIdx2 >= windowCount_) continue;

      if (!isBookCoverReady(windowEntries_[winIdx2].path, static_cast<size_t>(slot))) {
        yield();
        esp_task_wdt_reset();
        COVER_LOG("LIB", "Cover: att#%d slot=%d idx=%d gen... path=%s free=%u maxA=%u",
                  attempt, slot, idx, windowEntries_[winIdx2].path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (auto* fcm = renderer.getFontCacheManager()) {
          fcm->clearCache();
        }
        cachedInfo_.clear();
        cachedInfo_.shrink_to_fit();
        cachedSelTitle_.clear();
        cachedSelTitle_.shrink_to_fit();
        pageTitleCache_.clear();
        pageTitleCache_.shrink_to_fit();
        pageTitleCacheKey_ = -1;
        const bool genOk = LibraryCache::generateCoverForBook(windowEntries_[winIdx2].path, coverWidth_, coverHeight_);
        const bool slotOk = slot < 64;
        if (genOk && slotOk) {
          coverGeneratedMask_ |= (uint64_t{1} << slot);
          generatedOne = true;
        }
        forceRender_ = true;
        coverGenRenderBatch_++;
        if (coverGenRenderBatch_ >= kCoverRenderBatchEvery) {
          coverGenRenderBatch_ = 0;
          requestUpdate();
        }
        if (generatedOne) return;
      }
    }

    coverGenIndex_++;
    if (coverGenIndex_ >= pageCount) {
      bool stillMissing = false;
      for (int slot = 0; slot < pageCount && !stillMissing; ++slot) {
        const int idx = pageStart + slot;
        const int winIdx = idx - windowStart_;
        if (winIdx < 0 || winIdx >= windowCount_) continue;
        if (!isBookCoverReady(windowEntries_[winIdx].path, static_cast<size_t>(slot))) stillMissing = true;
      }
      COVER_LOG("LIB", "Cover: pass#%d DONE stillMissing=%d (free=%u)", coverPassCount_ + 1, stillMissing, ESP.getFreeHeap());
      if (!stillMissing || ++coverPassCount_ >= kMaxCoverPasses) {
        COVER_LOG("LIB", "Cover: pass#%d COMPLETE", coverPassCount_);
        coversComplete_ = true;
        coverGenIndex_ = -1;
        coverGeneratedMask_ = 0;
        coverGenRenderBatch_ = 0;
        cachedRenderSelector_ = -1;
        cachedRenderPage_ = -1;
        cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
        cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
        cachedInfoSearch_.clear();
        forceRender_ = true;
        requestUpdate();
        return;
      }
      coverGenIndex_ = 0;
    }
    return;
  }

  if (total <= 0) {
    upHeld_ = false; upLongTriggered_ = false;
    downHeld_ = false; downLongTriggered_ = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      upHeld_ = false; downHeld_ = false;
      upLongTriggered_ = false; downLongTriggered_ = false;
      onGoHome();
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
      if (!upHeld_) { upHeld_ = true; upLongTriggered_ = false; }
      if (!upLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
        upLongTriggered_ = true;
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Up);
        openSortPopup();
        return;
      }
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Down)) {
      if (!downHeld_) { downHeld_ = true; downLongTriggered_ = false; }
      if (!downLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
        downLongTriggered_ = true;
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Down);
        openFilterPopup();
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) { upHeld_ = false; upLongTriggered_ = false; }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) { downHeld_ = false; downLongTriggered_ = false; }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (total > 0 && selectorIndex_ < total) {
      const unsigned long held = mappedInput.getHeldTime();
      if (held >= 800) {
        const int idx = selectorIndex_;
        ensureWindowForIndex(idx);
        const int winIdx = idx - windowStart_;
        if (winIdx < 0 || winIdx >= windowCount_) { forceRender_ = true; requestUpdate(); return; }
        const std::string& path = windowEntries_[winIdx].path;
        const std::string title = windowEntries_[winIdx].title.empty() ? filenameWithoutExtension(path) : windowEntries_[winIdx].title;
        const bool isEpub = FsHelpers::hasEpubExtension(path);
        const bool isFav = FAVORITES.isFavorite(path);
        const auto* stats = READING_STATS.findBook(path);
        const bool isCompleted = stats && stats->completed;
        startActivityForResult(
            std::make_unique<BookContextMenuActivity>(renderer, mappedInput, title, isFav, isCompleted, isEpub, true),
            [this, idx, path, title, isEpub](const ActivityResult& result) {
              if (result.isCancelled) { forceRender_ = true; requestUpdate(); return; }
              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) { forceRender_ = true; requestUpdate(); return; }
              switch (static_cast<BookContextMenuActivity::MenuAction>(menuResult->action)) {
                case BookContextMenuActivity::MenuAction::OPEN_BOOK: onSelectBook(path); return;
                case BookContextMenuActivity::MenuAction::VIEW_STATS:
                  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { forceRender_ = true; requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::VIEW_METADATA:
                  startActivityForResult(std::make_unique<BookMetadataActivity>(renderer, mappedInput, path),
                                         [this](const ActivityResult&) { forceRender_ = true; requestUpdate(); });
                  return;
                case BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES:
                  FAVORITES.toggleBook(path); forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::MARK_READ_UNREAD: {
                  const auto* s = READING_STATS.findBook(path);
                  const bool wasCompleted = s && s->completed;
                  READING_STATS.beginSession(path, title,
                                            windowEntries_[idx - windowStart_].title.empty() ? "" : windowEntries_[idx - windowStart_].title,
                                            LibraryCache::thumbPathFor(path, coverWidth_, coverHeight_),
                                            wasCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  forceRender_ = true; requestUpdate(); return;
                }
                case BookContextMenuActivity::MenuAction::DELETE_CACHE:
                  if (isEpub) { Epub epub(path, "/.crosspoint"); epub.load(false, true); epub.clearCache(); }
                  forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_COVER_THUMB:
                  deleteLibraryCovers(path);
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_PAGE_COVER_THUMBS:
                  deletePageCovers();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::DELETE_ALL_LIBRARY_COVERS:
                  deleteAllLibraryCovers();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                case BookContextMenuActivity::MenuAction::REINDEX_LIBRARY:
                  LibraryCache::invalidate();
                  scanSd();
                  coversComplete_ = false; coverGenIndex_ = -1; forceRender_ = true; requestUpdate(); return;
                default: forceRender_ = true; requestUpdate(); return;
              }
            });
        return;
      }
      ensureWindowForIndex(selectorIndex_);
      const int winIdx2 = selectorIndex_ - windowStart_;
      if (winIdx2 >= 0 && winIdx2 < windowCount_) {
        onSelectBook(windowEntries_[winIdx2].path);
      }
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (upHeld_ || downHeld_) {
      upHeld_ = false; downHeld_ = false;
      upLongTriggered_ = false; downLongTriggered_ = false;
    } else {
      onGoHome();
    }
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
    if (!upHeld_) { upHeld_ = true; upLongTriggered_ = false; }
    if (!upLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
        upLongTriggered_ = true;
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Up);
        openSortPopup();
        return;
    }
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Down)) {
    if (!downHeld_) { downHeld_ = true; downLongTriggered_ = false; }
    if (!downLongTriggered_ && mappedInput.getHeldTime() >= kLongPressMs) {
        downLongTriggered_ = true;
        popupSpawnButton_ = static_cast<int>(MappedInputManager::Button::Down);
        openFilterPopup();
        return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld_ && !upLongTriggered_) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int r = (selectorIndex_ - ps) / gridColumns_;
      if (r == 0) {
        int prev = ps - gridsPerPage_;
        if (prev < 0) prev = ((total + gridsPerPage_ - 1) / gridsPerPage_ - 1) * gridsPerPage_;
        int prevItems = std::min(gridsPerPage_, total - prev);
        selectorIndex_ = prev + prevItems - 1;
      } else {
        selectorIndex_ -= gridColumns_;
      }
      int curPage = selectorIndex_ / gridsPerPage_;
      if (curPage != lastPage_) {
        coversComplete_ = false; coverGenIndex_ = -1; coverPassCount_ = 0;
        coverGeneratedMask_ = 0; lastPage_ = curPage;
        ensureWindowForIndex(selectorIndex_);
      }
      ensureWindowForIndex(selectorIndex_);
      requestUpdate();
    }
    upHeld_ = false; upLongTriggered_ = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld_ && !downLongTriggered_) {
      int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
      int pageItems = std::min(gridsPerPage_, total - ps);
      int rows = pageItems / gridColumns_;
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
        coversComplete_ = false; coverGenIndex_ = -1; coverPassCount_ = 0;
        coverGeneratedMask_ = 0; lastPage_ = curPage;
        ensureWindowForIndex(selectorIndex_);
      }
      ensureWindowForIndex(selectorIndex_);
      requestUpdate();
    }
    downHeld_ = false; downLongTriggered_ = false;
  }

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex_ = (selectorIndex_ > 0) ? selectorIndex_ - 1 : total - 1; moved = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex_ = (selectorIndex_ < total - 1) ? selectorIndex_ + 1 : 0; moved = true;
  }
  if (moved) {
    int curPage = selectorIndex_ / gridsPerPage_;
    if (curPage != lastPage_) {
        coversComplete_ = false; coverGenIndex_ = -1; coverPassCount_ = 0;
        coverGeneratedMask_ = 0; lastPage_ = curPage;
    }
    ensureWindowForIndex(selectorIndex_);
    requestUpdate();
  }
}

void LibraryActivity::render(RenderLock&&) {
  esp_task_wdt_reset();
  const int total = totalBooks_;
  const int curPageRaw = total > 0 ? selectorIndex_ / gridsPerPage_ : 0;

  if (!forceRender_ && popupMode_ == PopupMode::None &&
      curPageRaw == lastRenderedPage_ && selectorIndex_ == lastRenderedSelectorIndex_ &&
      coversComplete_ == lastRenderedCoversComplete_) {
    return;
  }

  const bool forcedRender = forceRender_;
  forceRender_ = false;
  lastRenderedPage_ = curPageRaw;
  lastRenderedSelectorIndex_ = selectorIndex_;
  lastRenderedCoversComplete_ = coversComplete_;

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int totalPages = total > 0 ? (total + gridsPerPage_ - 1) / gridsPerPage_ : 0;
  const int curPage = total > 0 ? curPageRaw + 1 : 0;

  COVER_LOG("LIB", "Render: start free=%u maxA=%u total=%d page=%d window=[%d,%d)",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap(), total, curPage, windowStart_, windowStart_ + windowCount_);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr, nullptr);

  // Pagination top-left on the header bar
  if (total > 0) {
    char hdrBuf[32] = {};
    if (!coversComplete_) {
      const int pgStartI = (curPage - 1) * gridsPerPage_;
      const int pgCountI = std::min(gridsPerPage_, total - pgStartI);
      int missing = 0;
      for (int i = 0; i < pgCountI; ++i) {
        const int idx = pgStartI + i;
        const int winIdx = idx - windowStart_;
        if (winIdx >= 0 && winIdx < windowCount_) {
          if (!isBookCoverReady(windowEntries_[winIdx].path, static_cast<size_t>(i))) missing++;
        }
      }
      snprintf(hdrBuf, sizeof(hdrBuf), "covers %d/%d", pgCountI - missing, pgCountI);
    } else {
      snprintf(hdrBuf, sizeof(hdrBuf), "%d/%d", curPage, totalPages);
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 6, hdrBuf, true,
                      EpdFontFamily::REGULAR);
  }

  const bool infoKeyChanged =
      cachedRenderSelector_ != selectorIndex_ || cachedRenderPage_ != curPageRaw ||
      cachedInfoFilter_ != currentFilter_ || cachedInfoSort_ != currentSort_ ||
      cachedInfoSearch_ != currentSearchText_;
  if (infoKeyChanged) {
    cachedInfo_.clear();
    switch (currentFilter_) {
      case CrossPointSettings::LIBRARY_FILTER_FAVOURITES: cachedInfo_ = tr(STR_FAVOURITES); break;
      case CrossPointSettings::LIBRARY_FILTER_LATEST_READ: cachedInfo_ = tr(STR_LATEST_READ); break;
      default: cachedInfo_ = tr(STR_ALL_BOOKS); break;
    }
    const char* sortLabel = nullptr;
    switch (currentSort_) {
      case CrossPointSettings::LIBRARY_SORT_TITLE_ASC:  sortLabel = tr(STR_SORT_TITLE_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_TITLE_DESC: sortLabel = tr(STR_SORT_TITLE_DESC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_ASC: sortLabel = tr(STR_SORT_AUTHOR_ASC); break;
      case CrossPointSettings::LIBRARY_SORT_AUTHOR_DESC: sortLabel = tr(STR_SORT_AUTHOR_DESC); break;
      case CrossPointSettings::LIBRARY_SORT_RECENT:     sortLabel = tr(STR_SORT_RECENT); break;
      case CrossPointSettings::LIBRARY_SORT_PROGRESS:   sortLabel = tr(STR_SORT_PROGRESS); break;
      default: break;
    }
    if (sortLabel && sortLabel[0]) { cachedInfo_ += " / "; cachedInfo_ += sortLabel; }
    if (!currentSearchText_.empty()) {
      cachedInfo_ += " [";
      cachedInfo_ += currentSearchText_.size() > 20 ? currentSearchText_.substr(0, 20) + ".." : currentSearchText_;
      cachedInfo_ += "]";
    }

    if (selectorIndex_ < total && windowCount_ > 0) {
      const int winIdx = selectorIndex_ - windowStart_;
      if (winIdx >= 0 && winIdx < windowCount_) {
        cachedSelTitle_ = windowEntries_[winIdx].title;
        if (cachedSelTitle_.empty()) cachedSelTitle_ = filenameWithoutExtension(windowEntries_[winIdx].path);
        const int maxSelW = pageWidth - 20;
        if (renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::REGULAR) > maxSelW) {
          while (cachedSelTitle_.size() > 3 &&
                 renderer.getTextWidth(UI_10_FONT_ID, (cachedSelTitle_ + "..").c_str(), EpdFontFamily::REGULAR) > maxSelW) {
            cachedSelTitle_.pop_back();
          }
          cachedSelTitle_ += "..";
        }
      } else {
        cachedSelTitle_.clear();
      }
    } else {
      cachedSelTitle_.clear();
    }

    cachedInfoFilter_ = currentFilter_;
    cachedInfoSort_ = currentSort_;
    cachedInfoSearch_ = currentSearchText_;
    cachedRenderSelector_ = selectorIndex_;
    cachedRenderPage_ = curPageRaw;
  }

  int lblW = renderer.getTextWidth(UI_10_FONT_ID, cachedInfo_.c_str(), EpdFontFamily::REGULAR);
  if (lblW > pageWidth - 20) {
    while (cachedInfo_.size() > 5 && renderer.getTextWidth(UI_10_FONT_ID, (cachedInfo_ + "..").c_str(), EpdFontFamily::REGULAR) > pageWidth - 20) {
      cachedInfo_.pop_back();
    }
    cachedInfo_ += "..";
  }
  lblW = renderer.getTextWidth(UI_10_FONT_ID, cachedInfo_.c_str(), EpdFontFamily::REGULAR);
  int centerX = (pageWidth - lblW) / 2;
  int headerY = metrics.topPadding + 8;
  renderer.drawText(UI_10_FONT_ID, centerX, headerY, cachedInfo_.c_str(), true, EpdFontFamily::REGULAR);

  if (total > 0 && selectorIndex_ < total && !cachedSelTitle_.empty()) {
    const int selTitleW = renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::REGULAR);
    const int selTitleX = (pageWidth - selTitleW) / 2;
    const int selTitleY = headerY + renderer.getLineHeight(UI_10_FONT_ID) + 2;
    renderer.drawText(UI_10_FONT_ID, selTitleX, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_LIBRARY_EMPTY));
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
  }

  if (total > 0 && windowCount_ > 0) {
  const int pageStart = (curPage - 1) * gridsPerPage_;
  const int pageCount = std::min(gridsPerPage_, total - pageStart);
  const int gap = gap_;
  const int rowPad = rowPad_;
  const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
  const int x0 = (pageWidth - gridW) / 4;
  const int rowH = coverHeight_ + rowPad;

  if (pageTitleCacheKey_ != pageStart) {
    pageTitleCache_.clear();
    pageTitleCache_.reserve(pageCount);
    constexpr int kCoverTextPad = 4;
    for (int i = 0; i < pageCount; ++i) {
      const int idx = pageStart + i;
      const int winIdx = idx - windowStart_;
      std::string t;
      if (winIdx >= 0 && winIdx < windowCount_) {
        t = windowEntries_[winIdx].title;
        if (t.empty()) t = filenameWithoutExtension(windowEntries_[winIdx].path);
      }
      pageTitleCache_.push_back(renderer.wrappedText(SMALL_FONT_ID, t.c_str(),
                                                     coverWidth_ - 2 * kCoverTextPad, 3, EpdFontFamily::BOLD));
    }
    pageTitleCacheKey_ = pageStart;
  }

  const bool selectionOnlyMove =
      !forcedRender && popupMode_ == PopupMode::None &&
      curPageRaw == lastRenderedPage_ && coversComplete_ == lastRenderedCoversComplete_ &&
      pageTitleCacheKey_ == pageStart &&
      lastRenderedSelectorIndex_ >= 0 && lastRenderedSelectorIndex_ < total &&
      selectorIndex_ != lastRenderedSelectorIndex_ && selectorIndex_ < total;

  if (selectionOnlyMove) {
    const int prevSel = lastRenderedSelectorIndex_;
    const int prevI = prevSel - pageStart;
    const int prevCol = prevI % gridColumns_;
    const int prevRow = prevI / gridColumns_;
    const int prevX = x0 + prevCol * (coverWidth_ + gap);
    const int prevY = contentTop + prevRow * rowH;
    renderer.fillRect(prevX - 6, prevY - 6, coverWidth_ + 12, coverHeight_ + 12, false);
    drawTileContent(prevI, pageStart, prevX, prevY);

    const int newI = selectorIndex_ - pageStart;
    const int newCol = newI % gridColumns_;
    const int newRow = newI / gridColumns_;
    const int newX = x0 + newCol * (coverWidth_ + gap);
    const int newY = contentTop + newRow * rowH;
    drawCyberpunkSelectionBorder(renderer, newX, newY, coverWidth_, coverHeight_);

    const int headerY2 = metrics.topPadding + 8;
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int selTitleY = headerY2 + lh + 2;

    const int winIdx = selectorIndex_ - windowStart_;
    cachedSelTitle_ = (winIdx >= 0 && winIdx < windowCount_) ? windowEntries_[winIdx].title : "";
    if (cachedSelTitle_.empty() && winIdx >= 0 && winIdx < windowCount_)
      cachedSelTitle_ = filenameWithoutExtension(windowEntries_[winIdx].path);
    const int maxSelW = pageWidth - 20;
    if (renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::REGULAR) > maxSelW) {
      while (cachedSelTitle_.size() > 3 &&
             renderer.getTextWidth(UI_10_FONT_ID, (cachedSelTitle_ + "..").c_str(), EpdFontFamily::REGULAR) > maxSelW) {
        cachedSelTitle_.pop_back();
      }
      cachedSelTitle_ += "..";
    }
    renderer.fillRect(0, selTitleY, pageWidth, lh, false);
    if (!cachedSelTitle_.empty()) {
      const int selTitleW = renderer.getTextWidth(UI_10_FONT_ID, cachedSelTitle_.c_str(), EpdFontFamily::REGULAR);
      const int selTitleX2 = (pageWidth - selTitleW) / 2;
      renderer.drawText(UI_10_FONT_ID, selTitleX2, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);
    }
    cachedRenderSelector_ = selectorIndex_;

    lastRenderedSelectorIndex_ = selectorIndex_;
    lastRenderedPage_ = curPageRaw;
    lastRenderedCoversComplete_ = coversComplete_;
    renderer.displayBuffer();
    return;
  }

  for (int i = 0; i < pageCount; ++i) {
    const int idx = pageStart + i;
    const int winIdx = idx - windowStart_;
    if (winIdx < 0 || winIdx >= windowCount_) continue;

    const int col = i % gridColumns_;
    const int row = i / gridColumns_;
    const int x = x0 + col * (coverWidth_ + gap);
    const int y = contentTop + row * rowH;
    drawTileContent(i, pageStart, x, y);
    if (idx == selectorIndex_) {
      drawCyberpunkSelectionBorder(renderer, x, y, coverWidth_, coverHeight_);
    }
  }

  if (totalPages > 1) {
    constexpr int DS = 8, DSp = 6;
    int dotW = totalPages * DS + (totalPages - 1) * DSp;
    int sx = (pageWidth - dotW) / 2;
    int sy = pageHeight - metrics.buttonHintsHeight - 14 - DS;
    for (int p = 0; p < totalPages; ++p) {
      int dx = sx + p * (DS + DSp);
      if (p == curPage - 1) renderer.fillRect(dx, sy, DS, DS, true);
      else renderer.drawRect(dx, sy, DS, DS, true);
    }
  }
  }

  if (popupMode_ != PopupMode::None) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
  }

  if (popupMode_ != PopupMode::None) popupOverlay_.render(renderer, pageWidth, pageHeight);
  COVER_LOG("LIB", "Render: end free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  renderer.displayBuffer();
}

void LibraryActivity::drawTileContent(int i, int pageStart, int x, int y) const {
  const int idx = pageStart + i;
  const int winIdx = idx - windowStart_;
  if (winIdx < 0 || winIdx >= windowCount_) return;

  bool drawn = false;
  const std::string& thumbPath = LibraryCache::thumbPathFor(windowEntries_[winIdx].path, coverWidth_, coverHeight_);
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
  }
  if (drawn) {
    const auto* rbStats = READING_STATS.findBook(windowEntries_[winIdx].path);
    const bool isComplete = rbStats && rbStats->completed;
    const bool isFav = FAVORITES.isFavorite(windowEntries_[winIdx].path);
    const bool isOpened = rbStats && rbStats->totalReadingMs > 0 && !isComplete;
    if (isComplete || isFav || isOpened)
      drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
  }
}

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  std::string thumbPath = LibraryCache::thumbPathFor(bookPath, coverWidth_, coverHeight_);
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
  }
}

void LibraryActivity::deletePageCovers() {
  int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pe = std::min(ps + gridsPerPage_, totalBooks_);
  for (int i = ps; i < pe; ++i) {
    const int winIdx = i - windowStart_;
    if (winIdx < 0 || winIdx >= windowCount_) {
      // Entry not in window — compute thumbPath from the path hash alone
      std::string dummy = LibraryCache::thumbPathFor("dummy", coverWidth_, coverHeight_);
      // We need the actual path. Load it from cache.
      std::vector<LibraryCache::Entry> tmp;
      if (LibraryCache::loadPage(tmp, i, 1) > 0) {
        std::string thumbPath = LibraryCache::thumbPathFor(tmp[0].path, coverWidth_, coverHeight_);
        if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) Storage.remove(thumbPath.c_str());
      }
      continue;
    }
    std::string thumbPath = LibraryCache::thumbPathFor(windowEntries_[winIdx].path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  // Walk the cache file removing all cover thumbnails.
  // We load paths in batches to avoid holding everything in RAM.
  const int total = LibraryCache::getCount();
  if (total <= 0) return;

  int batchStart = 0;
  constexpr int kBatchSize = 64;
  while (batchStart < total) {
    std::vector<LibraryCache::Entry> batch;
    const int got = LibraryCache::loadPage(batch, batchStart, kBatchSize);
    if (got <= 0) break;
    for (int i = 0; i < got; ++i) {
      std::string thumbPath = LibraryCache::thumbPathFor(batch[i].path, coverWidth_, coverHeight_);
      if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
        Storage.remove(thumbPath.c_str());
      }
    }
    batchStart += got;
  }
}
