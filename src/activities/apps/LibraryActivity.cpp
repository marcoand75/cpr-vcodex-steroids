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
// This must match kMaxIconBytes in lib/GfxRenderer/GfxRenderer.cpp.
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

// Helper shared by scanSd() and rebuildForFilter().
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
  pageTitleCacheKey_ = -1;  // cover width changed -> wrapping width is stale
}

bool LibraryActivity::isBookCoverReady(const std::string& path, size_t slot) const {
  const std::string tp = LibraryCache::thumbPathFor(path, coverWidth_, coverHeight_);
  if (!tp.empty() && Storage.exists(tp.c_str())) return true;
  // Also accept slots we've already generated this pass — the FAT driver
  // may not make the BMP visible immediately after write. Only valid for the
  // current page (slot < 64). The bit is set only on a successful generation.
  if (slot < 64 && (coverGeneratedMask_ & (uint64_t{1} << slot))) return true;
  return false;
}

void LibraryActivity::rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter) {
  std::vector<LibraryCache::Entry> allEntries;
  if (!LibraryCache::load(allEntries)) {
    renderer.clearScreen();
    Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
    GUI.fillPopupProgress(renderer, popupRect, 0);
    renderer.displayBuffer();
    LibraryCache::scan(renderer, popupRect, allEntries, SETTINGS.libraryRootDir);
  }

  unfilteredEntries_.clear();
  for (int r = 0; r < static_cast<int>(allEntries.size()); ++r) {
    // Keep the WDT fed on very large libraries.
    if ((r & 0xF) == 0) { yield(); esp_task_wdt_reset(); }
    if (includeBookByFilter(allEntries[r], filter))
      unfilteredEntries_.push_back(std::move(allEntries[r]));
  }

  currentFilter_ = filter;
  applyFilterAndSort();
}

void LibraryActivity::applyFilterAndSort() {
  std::string searchLower = normalizeForSort(currentSearchText_);

  entries_.clear();
  if (searchLower.empty()) {
    // If unfilteredEntries_ is empty we've already moved it into entries_
    // on a previous call (e.g. after an initial sort).  Reload from the
    // on-disk cache so we don't lose the unfiltered set.
    if (unfilteredEntries_.empty()) {
      LibraryCache::load(unfilteredEntries_);
    }
    // Move instead of copy: entries_ takes ownership of the Entry objects
    // and their internal strings without duplicating heap allocations.
    entries_ = std::move(unfilteredEntries_);
    unfilteredEntries_.clear();
  } else {
    // Search text is non-empty: reload the unfiltered set if it was consumed
    // by a previous move, then filter by text match.
    if (unfilteredEntries_.empty()) {
      LibraryCache::load(unfilteredEntries_);
    }
    for (const auto& e : unfilteredEntries_) {
      if (normalizeForSort(e.title).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
        continue;
      }
      if (!e.author.empty() && normalizeForSort(e.author).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
        continue;
      }
      if (normalizeForSort(e.path).find(searchLower) != std::string::npos) {
        entries_.push_back(e);
      }
    }
  }

  const int n = static_cast<int>(entries_.size());
  HOMEPAGE_LOG("LIB", "filter+sort: n=%d heap=%u maxA=%u", n, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (n > 1) {
    // Sort without pre-computed sort keys: compareNormalized works on stack
    // buffers and never allocates heap memory.  This avoids ~206 temporary
    // std::string allocations that would otherwise fragment the heap.
    struct SortMeta {
      uint32_t lastReadAt = 0;
      uint8_t progress = 0;
      bool completed = false;
    };
    // Compact allocation: SortMeta is 7 bytes, rounded to 8 by alignment.
    // For 103 entries that's ~824 bytes in one contiguous block.
    std::vector<SortMeta> meta(n);
    for (int i = 0; i < n; ++i) {
      const auto* s = READING_STATS.findBook(entries_[i].path);
      if (s) {
        meta[i].lastReadAt = s->lastReadAt;
        meta[i].progress = s->lastProgressPercent;
        meta[i].completed = s->completed;
      }
    }
    HOMEPAGE_LOG("LIB", "filter+sort: after meta heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    yield();
    esp_task_wdt_reset();
    HOMEPAGE_LOG("LIB", "filter+sort: before sort heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    std::sort(idx.begin(), idx.end(), [&](int a, int b) -> bool {
      const auto& ea = entries_[a];
      const auto& eb = entries_[b];
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

    HOMEPAGE_LOG("LIB", "filter+sort: after sort heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // Reorder entries_ according to the sorted index permutation.
    std::vector<LibraryCache::Entry> reordered;
    reordered.reserve(n);
    HOMEPAGE_LOG("LIB", "filter+sort: after reserve(reordered) heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    for (int i : idx)
      reordered.push_back(std::move(entries_[i]));
    HOMEPAGE_LOG("LIB", "filter+sort: after reorder heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    entries_.swap(reordered);
    HOMEPAGE_LOG("LIB", "filter+sort: after swap heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  selectorIndex_ = 0;
  coversComplete_ = false;
  coverGenIndex_ = -1;
  lastPage_ = 0;  // selectorIndex_ was just reset to 0, which is on page 0
  pageTitleCacheKey_ = -1;  // entries order/contents changed
  // Force cached header info (filter/sort/title) to be rebuilt on next render.
  // Without this, the render guard sees cached values matching current values
  // and skips the rebuild, leaving the header empty after a filter/sort change.
  cachedRenderSelector_ = -1;
  cachedRenderPage_ = -1;
  cachedInfoFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(-1);
  cachedInfoSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(-1);
  cachedInfoSearch_.clear();
  forceRender_ = true;
}

void LibraryActivity::scanSd() {
  currentFilter_ = static_cast<CrossPointSettings::LIBRARY_FILTER>(SETTINGS.libraryFilter);
  currentSort_ = static_cast<CrossPointSettings::LIBRARY_SORT>(SETTINGS.librarySort);
  currentSearchText_ = SETTINGS.librarySearchText;

  // Try incremental sync first; falls back to full scan only
  // when the cache file is missing or corrupt.
  if (LibraryCache::sync(unfilteredEntries_, SETTINGS.libraryRootDir)) {
    HOMEPAGE_LOG("LIB", "scanSd: after sync heap=%u maxA=%u unfiltered=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), unfilteredEntries_.size());
    applyFilterAndSort();
    HOMEPAGE_LOG("LIB", "scanSd: after filter+sort heap=%u maxA=%u entries=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), entries_.size());
    LOG_DBG("LIB", "Synced %d entries from library cache (root=%s)", static_cast<int>(entries_.size()), SETTINGS.libraryRootDir);
    return;
  }

  // Cache unavailable – full scan with progress popup.
  renderer.clearScreen();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
  GUI.fillPopupProgress(renderer, popupRect, 0);
  renderer.displayBuffer();

  std::vector<LibraryCache::Entry> allEntries;
  LibraryCache::scan(renderer, popupRect, allEntries, SETTINGS.libraryRootDir);

  unfilteredEntries_.clear();
  for (auto& e : allEntries) {
    if (includeBookByFilter(e, currentFilter_))
      unfilteredEntries_.push_back(std::move(e));
  }

  applyFilterAndSort();
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
  // Force a redraw: closing the popup changes only popupMode_, so without
  // this the render guard would early-return and leave the popup overlay on
  // screen until the next key press.
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
  lastPage_ = 0;  // start on page 0 so intra-page Left/Right don't falsely trigger page change
  lastRenderedPage_ = -1; forceRender_ = true;
  popupMode_ = PopupMode::None;
  upHeld_ = false; upLongTriggered_ = false;
  downHeld_ = false; downLongTriggered_ = false;
  popupSpawnButton_ = -1;
  lastLayoutSetting_ = SETTINGS.libraryLayout;
  HOMEPAGE_LOG("LIB", "onEnter: before scanSd heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  scanSd();
  HOMEPAGE_LOG("LIB", "onEnter: after scanSd heap=%u maxA=%u entries=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), entries_.size());
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
  entries_.clear();
  unfilteredEntries_.clear();
  pageTitleCache_.clear();
  pageTitleCacheKey_ = -1;
}

void LibraryActivity::loop() {
  if (popupMode_ != PopupMode::None) {
    // Consume the button release that opened this popup so it does not also
    // move the selection (long-press Up/Down opens the popup, then lifting the
    // finger would otherwise shift the cursor by one).
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

  const int total = static_cast<int>(entries_.size());

  ensureLayoutUpToDate();

  // ---- Cover generation: one thumb per frame, block input ----
  if (!coversComplete_ && total > 0) {
    const int pageStart = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
    const int pageCount = std::min(gridsPerPage_, total - pageStart);

    if (coverGenIndex_ < 0) {
      coverGeneratedMask_ = 0;
      coverPassCount_ = 0;
      coverGenRenderBatch_ = 0;
      // P2: if every slot already has its thumb on disk, the page is ready
      // immediately — skip the one-slot-per-frame pass that would otherwise
      // freeze navigation for gridsPerPage_ frames on re-visits to an already
      // indexed page.
      bool allReady = true;
      for (int s = 0; s < pageCount && allReady; ++s) {
        if (!isBookCoverReady(entries_[pageStart + s].path, static_cast<size_t>(s))) allReady = false;
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
      // Heap diagnostics before starting a cover pass.
      // The two persistent vectors (entries_, unfilteredEntries_) hold ~3000
      // small std::string allocations that fragment the heap.  Log both here
      // and after cache clear so we can track how much contiguous memory the
      // clear recovers.
      const uint32_t freeBefore = ESP.getFreeHeap();
      const uint32_t maxABefore = ESP.getMaxAllocHeap();
      COVER_LOG("LIB", "Covers: pass#%d START pageStart=%d pageCount=%d free=%u maxA=%u",
                coverPassCount_, pageStart, pageCount, freeBefore, maxABefore);
      // First frame of this pass: redraw the full grid so placeholders + any
      // existing covers from previous sessions are visible immediately.
      forceRender_ = true;
      requestUpdate();
      coverGenIndex_ = 0;
      return;  // render this frame, generate next frame
    }

    // Generate the next missing thumb.
    bool generatedOne = false;
    for (int attempt = 0; attempt < pageCount && !generatedOne; ++attempt) {
      const int slot = (coverGenIndex_ + attempt) % pageCount;
      const int idx = pageStart + slot;
      if (!isBookCoverReady(entries_[idx].path, static_cast<size_t>(slot))) {
        yield();
        esp_task_wdt_reset();
        COVER_LOG("LIB", "Cover: att#%d slot=%d idx=%d gen... path=%s free=%u maxA=%u",
                  attempt, slot, idx, entries_[idx].path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        // Free render caches so the EPUB cover decoder gets more contiguous heap.
        // Font cache, page-title cache and header strings all hold medium-size
        // allocations that fragment the heap; none are needed during cover generation.
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
        COVER_LOG("LIB", "Cover: post-clear free=%u maxA=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        const bool genOk = LibraryCache::generateCoverForBook(entries_[idx].path, coverWidth_, coverHeight_);
        const bool slotOk = slot < 64;
        COVER_LOG("LIB", "Cover: slot=%d gen=%d slotOk=%d free=%u maxA=%u", slot, genOk, slotOk, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (genOk && slotOk) {
          coverGeneratedMask_ |= (uint64_t{1} << slot);
          generatedOne = true;
        }
        // Force a render update in both success and failure so the user
        // sees either the new thumb or the placeholder right away.
        forceRender_ = true;
        coverGenRenderBatch_++;
        if (coverGenRenderBatch_ >= kCoverRenderBatchEvery) {
          coverGenRenderBatch_ = 0;
          requestUpdate();
        }
        // Only block input & return when we actually generated a cover.
        // On failure, continue the inner loop so we try other missing
        // slots in this frame instead of retrying the same failing slot
        // forever.  The retry budget still gives failing slots another
        // chance on the next pass.
        if (generatedOne) return;
      }
    }

    coverGenIndex_++;
    if (coverGenIndex_ >= pageCount) {
      // Full pass done.  If nothing is still missing, or we've exceeded the
      // retry budget, mark the page complete and allow navigation.
      bool stillMissing = false;
      for (int slot = 0; slot < pageCount && !stillMissing; ++slot) {
        if (!isBookCoverReady(entries_[pageStart + slot].path, static_cast<size_t>(slot))) stillMissing = true;
      }
      COVER_LOG("LIB", "Cover: pass#%d DONE stillMissing=%d (free=%u)", coverPassCount_ + 1, stillMissing, ESP.getFreeHeap());
      if (!stillMissing || ++coverPassCount_ >= kMaxCoverPasses) {
        COVER_LOG("LIB", "Cover: pass#%d COMPLETE (stillMissing=%d budgetExceeded=%d)", coverPassCount_, stillMissing, coverPassCount_ >= kMaxCoverPasses);
        coversComplete_ = true;
        coverGenIndex_ = -1;
        coverGeneratedMask_ = 0;
        coverGenRenderBatch_ = 0;
        // Invalidate cached header strings cleared during cover gen (line 670-672).
        // Without this, the next render sees infoKeyChanged=false and the header
        // is left empty on screen.
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

    return;  // block input while indexing
  }

  if (total <= 0) {
    // Reset held-key state when transitioning to empty, so long-press
    // sort/filter triggers work even if navigation left stale state.
    upHeld_ = false; upLongTriggered_ = false;
    downHeld_ = false; downLongTriggered_ = false;
    // The list is empty (e.g., a filter produced zero matches). Only allow
    // Back (go home) and long-press popup triggers so the user is never stuck.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      upHeld_ = false; downHeld_ = false;
      upLongTriggered_ = false; downLongTriggered_ = false;
      onGoHome();
    }
    // Long-press Up/Down to open sort/filter popups (so the user can switch
    // back to a non-empty filter).
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
      upHeld_ = false; upLongTriggered_ = false;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      downHeld_ = false; downLongTriggered_ = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (total > 0 && selectorIndex_ < total) {
      const unsigned long held = mappedInput.getHeldTime();
      if (held >= 800) {
        const int idx = selectorIndex_;
        const std::string& path = entries_[idx].path;
        const std::string title = entries_[idx].title.empty() ? filenameWithoutExtension(path) : entries_[idx].title;
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
                                            entries_[idx].title.empty() ? "" : entries_[idx].title,
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
      onSelectBook(entries_[selectorIndex_].path);
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
      }
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
      }
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
    requestUpdate();
  }
}

void LibraryActivity::render(RenderLock&&) {
  esp_task_wdt_reset();
  const int total = static_cast<int>(entries_.size());
  const int curPageRaw = total > 0 ? selectorIndex_ / gridsPerPage_ : 0;
  // Guard: skip full redraw when nothing changed.  During cover indexing
  // forceRender_ is only set when a thumb was generated or the initial popup
  // needs to appear, so we don't redraw the entire screen every idle frame.
  if (!forceRender_ && popupMode_ == PopupMode::None &&
      curPageRaw == lastRenderedPage_ && selectorIndex_ == lastRenderedSelectorIndex_ &&
      coversComplete_ == lastRenderedCoversComplete_) {
    return;
  }
  // Capture whether this is a forced redraw (cover completion, menu actions
  // that change ribbons, etc.) before clearing the flag. The incremental
  // selector path (P3) must NOT run on a forced redraw, because a forced
  // redraw can change tile content beyond just the selection highlight.
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

  // Diagnostic: log heap state at render start to isolate if rendering fragments heap
  COVER_LOG("LIB", "Render: start free=%u maxA=%u total=%d page=%d",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap(), total, curPage);

  // Draw header bar (black background + battery) without title/subtitle
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr, nullptr);

  // Pagination top-left on the header bar
  if (total > 0) {
    char hdrBuf[32] = {};
    if (!coversComplete_) {
      // Show "N/M" cover generation progress instead of page number.
      const int pgStartI = (curPage - 1) * gridsPerPage_;
      const int pgCountI = std::min(gridsPerPage_, total - pgStartI);
      int missing = 0;
      for (int i = 0; i < pgCountI; ++i) {
        if (!isBookCoverReady(entries_[pgStartI + i].path, static_cast<size_t>(i))) missing++;
      }
      snprintf(hdrBuf, sizeof(hdrBuf), "covers %d/%d", pgCountI - missing, pgCountI);
    } else {
      snprintf(hdrBuf, sizeof(hdrBuf), "%d/%d", curPage, totalPages);
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 6, hdrBuf, true,
                      EpdFontFamily::REGULAR);
  }

  // Rebuild the cached header/title strings only when their inputs change.
  // This keeps render effectively allocation-free in the steady state and
  // during per-page cover indexing, where render runs every frame.
  // Runs regardless of total so that filter/sort info is shown even when
  // the library is empty (zero search results, etc.).
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

    if (selectorIndex_ < total) {
      cachedSelTitle_ = entries_[selectorIndex_].title;
      if (cachedSelTitle_.empty()) cachedSelTitle_ = filenameWithoutExtension(entries_[selectorIndex_].path);
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

  // Draw selected book title below the info line, centered
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
    // No return — fall through to the header info + popup at the end of this function
    // so the sort/filter/title bar is rendered even when the library is empty.
  }

  if (total > 0) {
  const int pageStart = (curPage - 1) * gridsPerPage_;
  const int pageCount = std::min(gridsPerPage_, total - pageStart);
  const int gap = gap_;
  const int rowPad = rowPad_;
  const int gridW = gridColumns_ * coverWidth_ + (gridColumns_ - 1) * gap;
  const int x0 = (pageWidth - gridW) / 4;
  const int rowH = coverHeight_ + rowPad;

  // Build the wrapped cover-title cache for this page once. This avoids
  // per-frame heap allocation in the render loop during cover indexing, where
  // render runs every frame. Rebuilt only when the page (or its contents)
  // changes; pageTitleCacheKey_ is invalidated in applyFilterAndSort /
  // applyLayoutFromSettings / onExit.
  if (pageTitleCacheKey_ != pageStart) {
    pageTitleCache_.clear();
    pageTitleCache_.reserve(pageCount);
    constexpr int kCoverTextPad = 4;
    for (int i = 0; i < pageCount; ++i) {
      const int idx = pageStart + i;
      std::string t = entries_[idx].title;
      if (t.empty()) t = filenameWithoutExtension(entries_[idx].path);
      pageTitleCache_.push_back(renderer.wrappedText(SMALL_FONT_ID, t.c_str(),
                                                     coverWidth_ - 2 * kCoverTextPad, 3, EpdFontFamily::BOLD));
    }
    pageTitleCacheKey_ = pageStart;
  }

  // P3: incremental selector update. When only the selection moved within the
  // already-rendered page (same page, same cover state, no forced redraw), we
  // avoid clearScreen() + full grid redraw. We repaint just the previously
  // selected tile (removing its border), draw the new selection border, and
  // refresh the selected-title line — a fraction of the full-screen work.
  // The framebuffer already holds this page's full render, so the e-ink diff
  // is tiny (same FAST_REFRESH as a full update). Falls through to the full
  // redraw for any other change.
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
    // The selection border is two concentric rounded rects (offsets 4 and 6px);
    // clear the full 6px bounding box so both are removed. With gap >= 7 the
    // 6px clear still never reaches a neighbour tile (1px spare on each side).
    renderer.fillRect(prevX - 6, prevY - 6, coverWidth_ + 12, coverHeight_ + 12, false);
    drawTileContent(prevI, pageStart, prevX, prevY);

    const int newI = selectorIndex_ - pageStart;
    const int newCol = newI % gridColumns_;
    const int newRow = newI / gridColumns_;
    const int newX = x0 + newCol * (coverWidth_ + gap);
    const int newY = contentTop + newRow * rowH;
    drawCyberpunkSelectionBorder(renderer, newX, newY, coverWidth_, coverHeight_);

    // Refresh the selected-title line under the header (white band + redraw).
    // The info line and pagination are unchanged on a pure selector move.
    const int headerY = metrics.topPadding + 8;
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int selTitleY = headerY + lh + 2;
    cachedSelTitle_ = entries_[selectorIndex_].title;
    if (cachedSelTitle_.empty()) cachedSelTitle_ = filenameWithoutExtension(entries_[selectorIndex_].path);
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
      const int selTitleX = (pageWidth - selTitleW) / 2;
      renderer.drawText(UI_10_FONT_ID, selTitleX, selTitleY, cachedSelTitle_.c_str(), true, EpdFontFamily::BOLD);
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
  }  // end if (totalPages > 1)
  }  // end if (total > 0)

  if (popupMode_ != PopupMode::None) {
    // While a popup is open the Up/Down side buttons scroll the list and the
    // front Back/Confirm buttons close/choose — same vocabulary as the
    // homepage carousel book popup.
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP_SORT), tr(STR_DIR_DOWN_FILTER));
  }

  if (popupMode_ != PopupMode::None) popupOverlay_.render(renderer, pageWidth, pageHeight);
  // Diagnostic: log heap state at render end to measure fragmentation caused by rendering
  COVER_LOG("LIB", "Render: end free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  renderer.displayBuffer();
}

void LibraryActivity::drawTileContent(int i, int pageStart, int x, int y) const {
  const int idx = pageStart + i;
  bool drawn = false;
  const std::string& thumbPath = LibraryCache::thumbPathFor(entries_[idx].path, coverWidth_, coverHeight_);
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
    const auto* rbStats = READING_STATS.findBook(entries_[idx].path);
    const bool isComplete = rbStats && rbStats->completed;
    const bool isFav = FAVORITES.isFavorite(entries_[idx].path);
    const bool isOpened = rbStats && rbStats->totalReadingMs > 0 && !isComplete;
    if (isComplete || isFav || isOpened)
      drawRibbonBadge(renderer, x, y, coverWidth_, coverHeight_, isComplete, isFav, isOpened);
  }
}

void LibraryActivity::deleteLibraryCovers(const std::string& bookPath) {
  // Delete only the generated thumbnail. The book must stay in the library
  // cache; the caller resets coversComplete_ so the cover is regenerated.
  // (Removing the cache entry here would make the book vanish from the list
  // and force a re-parse on the next sync — inconsistent with the page/all
  // delete variants which only remove the BMP files.)
  std::string thumbPath = LibraryCache::thumbPathFor(bookPath, coverWidth_, coverHeight_);
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
  }
}

void LibraryActivity::deletePageCovers() {
  // Delete only the generated thumbnails for the current page. The books must
  // stay in the library cache (consistent with the single/all delete variants,
  // which only remove BMP files); the caller resets coversComplete_ so the
  // missing covers are regenerated for this page.
  int ps = (selectorIndex_ / gridsPerPage_) * gridsPerPage_;
  int pe = std::min(ps + gridsPerPage_, static_cast<int>(entries_.size()));
  for (int i = ps; i < pe; ++i) {
    std::string thumbPath = LibraryCache::thumbPathFor(entries_[i].path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
}

void LibraryActivity::deleteAllLibraryCovers() {
  for (auto& e : entries_) {
    std::string thumbPath = LibraryCache::thumbPathFor(e.path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
  for (auto& e : unfilteredEntries_) {
    std::string thumbPath = LibraryCache::thumbPathFor(e.path, coverWidth_, coverHeight_);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
    }
  }
}