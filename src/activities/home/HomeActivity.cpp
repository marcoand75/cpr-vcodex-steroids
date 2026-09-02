#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Utf8.h>
#include <Xtc.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/LibraryActivity.h"
#include "activities/apps/LibraryContextMenuActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/apps/ScreenSaverActivity.h"
#include "activities/apps/ClippingsAppActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/WikipediaActivity.h"
#include "activities/apps/QuickCardsActivity.h"
#include "util/BookFilter.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/home/BookContextMenuActivity.h"
#include "activities/home/BookMetadataActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/lyra/LyraMarcoand75Theme.h"
#include "components/PanelDrawHelper.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"
#include "util/ArenaAllocator.h"

namespace {
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int DEFAULT_HOME_SHORTCUT_PAGE_SIZE = 4;
constexpr int LYRA_HOME_SHORTCUT_PAGE_SIZE = 5;
constexpr int HOME_MAX_BOOKS = 10;
constexpr const char* CAROUSEL_FRAME_CACHE_DIR_LYRA = "/.crosspoint/home-carousel-cache";
constexpr const char* CAROUSEL_FRAME_CACHE_DIR_MARCOAND75 = "/.crosspoint/marcoand75-cache-v4";

// Bump this version whenever the theme rendering logic changes in a way that
// would make cached carousel frames invalid (e.g. layout, colours, metrics).
// Old cache directories are simply orphaned and will be cleaned by the user
// via "Clear theme cache" or by removing them from the SD card.
constexpr uint8_t MARCOAND75_CACHE_VERSION = 4;

const char* getCarouselFrameCacheDir() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_MARCOAND75
             ? CAROUSEL_FRAME_CACHE_DIR_MARCOAND75
             : CAROUSEL_FRAME_CACHE_DIR_LYRA;
}
constexpr uint32_t FNV1A_OFFSET = 2166136261UL;
constexpr uint32_t FNV1A_PRIME = 16777619UL;

std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

bool homeUsesFavorites() { return SETTINGS.homeBookSource == CrossPointSettings::HOME_BOOKS_FAVORITES; }

RecentBook toRecentBook(const FavoriteBook& book) {
  RecentBook recentBook{book.bookId, book.path, book.title, book.author, book.coverBmpPath};
  if (recentBook.title.empty()) {
    recentBook.title = book_filter::filenameWithoutExtension(recentBook.path);
  }
  return recentBook;
}

void updateHomeBookMetadata(const RecentBook& book) {
  if (homeUsesFavorites()) {
    FAVORITES.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
    return;
  }

  RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
}

bool canLoadHomeCover(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) ||
         FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

bool isValidBmpFile(const std::string& path) {
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    return false;
  }
  const size_t fileSize = file.size();

  // Lightweight header validation: read only the small BMP headers and check
  // consistency with the on-disk size, WITHOUT allocating a Bitmap (and so
  // without ever creating a ditherer). Detects truncated/corrupt covers while
  // costing a few bytes instead of a full parse.
  auto readLE32 = [](const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  };
  bool valid = false;
  if (fileSize >= 26) {
    uint8_t hdr[54];
    const size_t readHdr = file.read(hdr, sizeof(hdr));
    if (readHdr >= 26 && hdr[0] == 'B' && hdr[1] == 'M') {
      const uint32_t bfSize = readLE32(hdr + 2);       // total file size declared
      const uint32_t bfOffBits = readLE32(hdr + 10);   // byte offset of pixel data
      const uint32_t biSizeImage = (readHdr >= 38) ? readLE32(hdr + 34) : 0;
      const bool headerConsistent = (bfSize == 0 || bfSize <= fileSize);
      const bool pixelStartsInFile = bfOffBits <= fileSize;
      const bool pixelExtentOk = (biSizeImage == 0 ||
                                  (uint64_t)bfOffBits + biSizeImage <= fileSize);
      valid = headerConsistent && pixelStartsInFile && pixelExtentOk;
    }
  }
  file.close();
  return valid;
}

bool isValidHomeCoverPath(const std::string& coverBmpPath, const int coverHeight) {
  return isValidBmpFile(UITheme::getCoverThumbPath(coverBmpPath, coverHeight));
}

void removeInvalidHomeCoverTarget(const std::string& coverBmpPath, const int coverHeight) {
  if (coverBmpPath.empty()) {
    return;
  }

  const std::string resolvedPath = UITheme::getCoverThumbPath(coverBmpPath, coverHeight);
  if (Storage.exists(resolvedPath.c_str()) && !isValidBmpFile(resolvedPath)) {
    Storage.remove(resolvedPath.c_str());
  }
}

std::string getFavoriteRemovalKey(const FavoriteBook& book) {
  if (!book.path.empty()) {
    return book.path;
  }
  return book.bookId;
}

RecentBook resolveFavoriteForHome(const FavoriteBook& favorite) {
  RecentBook book = toRecentBook(favorite);
  if (book.path.empty() || !Storage.exists(book.path.c_str())) {
    return book;
  }

  const bool mayHaveCover = FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path);
  if (!book.bookId.empty() && !book.title.empty() && (!mayHaveCover || !book.coverBmpPath.empty())) {
    return book;
  }

  const FavoriteBook resolved = FAVORITES.getDataFromBook(book.path);
  bool changed = false;

  if (book.bookId.empty() && !resolved.bookId.empty()) {
    book.bookId = resolved.bookId;
    changed = true;
  }
  if (book.title.empty() && !resolved.title.empty()) {
    book.title = resolved.title;
    changed = true;
  }
  if (book.author.empty() && !resolved.author.empty()) {
    book.author = resolved.author;
    changed = true;
  }
  if (book.coverBmpPath.empty() && !resolved.coverBmpPath.empty()) {
    book.coverBmpPath = resolved.coverBmpPath;
    changed = true;
  }

  if (changed) {
    FAVORITES.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
  }
  return book;
}

// Fills `out` with the home shortcut entries. Returns the number of entries.
// No heap allocation: uses the caller-provided StaticVector.
int getHomeShortcutEntries(util::StaticVector<HomeShortcutEntry, 32>& out, bool hasOpdsServers) {
  out.clear();
  out.push_back(HomeShortcutEntry{nullptr, true});

  for (const auto& definition : getShortcutDefinitions()) {
    if (definition.id == ShortcutId::OpdsBrowser && !hasOpdsServers) {
      continue;
    }
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if (location == CrossPointSettings::SHORTCUT_HOME && getShortcutVisibility(definition)) {
      out.push_back(HomeShortcutEntry{&definition});
    }
  }

  std::stable_sort(out.begin(), out.end(), [](const HomeShortcutEntry& lhs, const HomeShortcutEntry& rhs) {
    const uint8_t lhsOrder = lhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*lhs.definition);
    const uint8_t rhsOrder = rhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*rhs.definition);
    return lhsOrder < rhsOrder;
  });

  return static_cast<int>(out.size());
}

// Builds the carousel shortcut list without truncating configured Home entries.
// Settings is still injected if missing, and Apps remains pinned last so the
// user always has an escape hatch even with aggressive shortcut customization.
// Writes into `out` and returns the number of entries. No heap allocation.
int buildCarouselEntries(util::StaticVector<HomeShortcutEntry, 32>& out,
                         const util::StaticVector<HomeShortcutEntry, 32>& all) {
  out.clear();
  HomeShortcutEntry appsEntry{nullptr, true};
  bool foundApps = false;
  bool foundSettings = false;

  for (const auto& e : all) {
    if (e.isAppsHub) {
      appsEntry = e;
      foundApps = true;
    } else {
      if (e.definition && e.definition->id == ShortcutId::Settings) {
        foundSettings = true;
      }
      out.push_back(e);
    }
  }

  if (!foundSettings) {
    for (const auto& def : getShortcutDefinitions()) {
      if (def.id == ShortcutId::Settings) {
        out.push_back(HomeShortcutEntry{&def});
        foundSettings = true;
        break;
      }
    }
  }

  if (foundApps) {
    out.push_back(appsEntry);
  }
  return static_cast<int>(out.size());
}

std::string getHomeShortcutTitle(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return tr(STR_APPS);
  }
  if (!entry.definition) {
    return "";
  }
  return I18N.get(entry.definition->nameId);
}

std::string getHomeShortcutSubtitle(const HomeShortcutEntry& entry) {
  return entry.definition ? ShortcutUiMetadata::getSubtitle(*entry.definition) : "";
}

UIIcon getHomeShortcutIcon(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return UIIcon::AppsHub;
  }
  return entry.definition ? entry.definition->icon : UIIcon::Folder;
}

bool showHomeShortcutAccessory(const HomeShortcutEntry& entry) {
  return entry.definition && ShortcutUiMetadata::showAccessory(*entry.definition);
}

bool isLyraCarouselTheme() {
  auto theme = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  return theme == CrossPointSettings::UI_THEME::LYRA_CAROUSEL ||
         theme == CrossPointSettings::UI_THEME::LYRA_MARCOAND75;
}

int wrapBookIndex(int index, int bookCount) {
  if (bookCount <= 0) {
    return 0;
  }
  while (index < 0) {
    index += bookCount;
  }
  return index % bookCount;
}

namespace CarouselHash {
uint32_t fnv1aByte(uint32_t hash, const uint8_t value) { return (hash ^ value) * FNV1A_PRIME; }

uint32_t fnv1aString(uint32_t hash, const std::string& value) {
  for (const char c : value) {
    hash = fnv1aByte(hash, static_cast<uint8_t>(c));
  }
  return fnv1aByte(hash, 0xFF);
}

uint32_t fnv1aU32(uint32_t hash, const uint32_t value) {
  hash = fnv1aByte(hash, static_cast<uint8_t>(value & 0xFF));
  hash = fnv1aByte(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
  hash = fnv1aByte(hash, static_cast<uint8_t>((value >> 16) & 0xFF));
  return fnv1aByte(hash, static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t fnv1aU64(uint32_t hash, const uint64_t value) {
  hash = fnv1aU32(hash, static_cast<uint32_t>(value & 0xFFFFFFFFULL));
  return fnv1aU32(hash, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFULL));
}
}  // namespace CarouselHash

// Theme-aware helpers for cover dimensions
int getCarouselCenterCoverW() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_MARCOAND75
             ? LyraMarcoand75Theme::kFiveCoverCenterW
             : LyraCarouselTheme::kCenterCoverW;
}
int getCarouselCenterCoverH() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_MARCOAND75
             ? LyraMarcoand75Theme::kFiveCoverCenterH
             : LyraCarouselTheme::kCenterCoverH;
}
std::string getCarouselCenterThumbPath(const RecentBook& book) {
  return UITheme::getCoverThumbPath(book.coverBmpPath, getCarouselCenterCoverW(), getCarouselCenterCoverH());
}

std::string getCarouselLegacyThumbPath(const RecentBook& book) {
  return UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselMetrics::values.homeCoverHeight);
}

bool hasCarouselUsableThumb(const RecentBook& book) {
  if (book.coverBmpPath.empty()) {
    return true;
  }
  const std::string centerCoverPath = getCarouselCenterThumbPath(book);
  if (Storage.exists(centerCoverPath.c_str())) {
    return true;
  }
  const std::string legacyCoverPath = getCarouselLegacyThumbPath(book);
  return Storage.exists(legacyCoverPath.c_str());
}

uint32_t hashCarouselThumbState(uint32_t hash, const RecentBook& book) {
  if (book.coverBmpPath.empty()) {
    return CarouselHash::fnv1aByte(hash, 0);
  }
  const std::string centerCoverPath = getCarouselCenterThumbPath(book);
  const std::string legacyCoverPath = getCarouselLegacyThumbPath(book);
  hash = CarouselHash::fnv1aByte(hash, Storage.exists(centerCoverPath.c_str()) ? 1 : 0);
  return CarouselHash::fnv1aByte(hash, Storage.exists(legacyCoverPath.c_str()) ? 1 : 0);
}

uint8_t getCarouselBookProgressPercent(const RecentBook& recentBook) {
  return READING_STATS.getBookProgressForHome(recentBook.bookId, recentBook.path);
}

// The portion of the frame hash that is shared by every book index: all params
// plus the full per-book loop (includes the two per-book Storage.exists
// thumb-state checks and the progress percent). This is the expensive O(N)
// work. FNV-1a is not commutative, so this prefix must be folded first and
// centerIdx appended LAST (see getCarouselFrameHash).
uint32_t getCarouselFramePrefixHash(const std::vector<RecentBook>& books, const int screenWidth,
                                    const int screenHeight, const size_t bufferSize, const bool darkMode) {
  uint32_t hash = FNV1A_OFFSET;
  hash = CarouselHash::fnv1aString(hash, "lyra-carousel-frame-v7-progress-badge");
  hash = CarouselHash::fnv1aU32(hash, static_cast<uint32_t>(screenWidth));
  hash = CarouselHash::fnv1aU32(hash, static_cast<uint32_t>(screenHeight));
  hash = CarouselHash::fnv1aU32(hash, static_cast<uint32_t>(bufferSize));
  hash = CarouselHash::fnv1aU32(hash, darkMode ? 1U : 0U);
  hash = CarouselHash::fnv1aU32(hash, static_cast<uint32_t>(SETTINGS.homeBookSource));
  hash = CarouselHash::fnv1aU32(hash, static_cast<uint32_t>(books.size()));

  for (const RecentBook& book : books) {
    hash = CarouselHash::fnv1aString(hash, book.bookId);
    hash = CarouselHash::fnv1aString(hash, book.path);
    hash = CarouselHash::fnv1aString(hash, book.title);
    hash = CarouselHash::fnv1aString(hash, book.author);
    hash = CarouselHash::fnv1aString(hash, book.coverBmpPath);
    hash = hashCarouselThumbState(hash, book);
    hash = CarouselHash::fnv1aByte(hash, getCarouselBookProgressPercent(book));
  }

  // The cached frame also renders the global stats panel (today / goal / streak
  // / finished). Include those values in the hash so the frame is regenerated
  // when they change (reading, day rollover, manual/auto clock sync). Without
  // this a stale frame is reused after a date change and the Home panel shows
  // yesterday's numbers even though summary.json was already regenerated.
  hash = CarouselHash::fnv1aU64(hash, READING_STATS.getTodayReadingMs());
  hash = CarouselHash::fnv1aU64(hash, getDailyReadingGoalMs());
  hash = CarouselHash::fnv1aU32(hash, READING_STATS.getCurrentStreakDays());
  hash = CarouselHash::fnv1aU32(hash, READING_STATS.getBooksFinishedCount());

  return hash;
}

uint32_t getCarouselFrameHash(const std::vector<RecentBook>& books, const int centerIdx, const int screenWidth,
                              const int screenHeight, const size_t bufferSize, const bool darkMode) {
  // IMPORTANT: the per-book loop MUST come BEFORE centerIdx. FNV-1a is not
  // commutative/associative, so this ordering lets pruneCarouselFrameCache
  // compute the (expensive) per-book prefix ONCE per pass instead of for every
  // index — O(N^2) -> O(N) SD accesses, the startup bottleneck. Do NOT move
  // centerIdx ahead of the book loop for "cosmetic" ordering: it would re-bake
  // the per-book work into every index, reintroduce the O(N^2) startup stall,
  // and silently break the cached-frame keys. This ordering also changed the
  // hash key vs. the previous ordering, intentionally invalidating the old .bin
  // frames once (they are regenerated on first render after the update).
  return CarouselHash::fnv1aU32(
      getCarouselFramePrefixHash(books, screenWidth, screenHeight, bufferSize, darkMode),
      static_cast<uint32_t>(centerIdx));
}

std::string getCarouselFrameCachePathFromHash(const uint32_t hash) {
  char filename[96];
  std::snprintf(filename, sizeof(filename), "%s/%08lx.bin", getCarouselFrameCacheDir(),
                static_cast<unsigned long>(hash));
  return filename;
}

void getCarouselFrameCachePathFromHash(const uint32_t hash, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%s/%08lx.bin", getCarouselFrameCacheDir(),
                static_cast<unsigned long>(hash));
}

int getHomeShortcutPageSize() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA
             ? LYRA_HOME_SHORTCUT_PAGE_SIZE
             : DEFAULT_HOME_SHORTCUT_PAGE_SIZE;
}

}  // namespace

int HomeActivity::getMenuItemCount() const {
  int shortcutCount = getHomeShortcutEntries(homeEntriesCache, hasOpdsServers);
  if (isLyraCarouselTheme()) {
    shortcutCount = buildCarouselEntries(carouselEntriesCache, homeEntriesCache);
  }
  return static_cast<int>(recentBooks.size()) + shortcutCount;
}

void HomeActivity::loadRecentBooks(const int maxBooks) {
  LOG_DBG("HOME", "loadRecentBooks: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  invalidateResidentCarouselFrame();
  invalidateCarouselFrameHash();
  recentBooks.clear();
  if (homeUsesFavorites()) {
    FAVORITES.ensureLoaded();
    const auto books = FAVORITES.getBooks();
    char staleFavorites[32][128];
    int staleFavoriteCount = 0;
    const bool unlimited = (maxBooks <= 0);
    recentBooks.reserve(unlimited ? books.size() : std::min(static_cast<int>(books.size()), maxBooks));

    for (const FavoriteBook& book : books) {
      if (book.path.empty() || !Storage.exists(book.path.c_str())) {
        const std::string removalKey = getFavoriteRemovalKey(book);
        if (!removalKey.empty() && staleFavoriteCount < 32) {
          std::strncpy(staleFavorites[staleFavoriteCount], removalKey.c_str(), sizeof(staleFavorites[0]) - 1);
          staleFavorites[staleFavoriteCount][sizeof(staleFavorites[0]) - 1] = '\0';
          ++staleFavoriteCount;
        }
        continue;
      }

      if (unlimited || static_cast<int>(recentBooks.size()) < maxBooks) {
        recentBooks.push_back(resolveFavoriteForHome(book));
      }
    }

    for (int i = 0; i < staleFavoriteCount; ++i) {
      FAVORITES.removeBook(staleFavorites[i]);
    }
    LOG_DBG("HOME", "loadRecentBooks: favorites end heap=%u maxA=%u books=%zu",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap(), recentBooks.size());
    return;
  }

  const auto& books = RECENT_BOOKS.getBooks();
  const bool unlimited = (maxBooks <= 0);
  recentBooks.reserve(unlimited ? books.size() : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (!unlimited && static_cast<int>(recentBooks.size()) >= maxBooks) {
      break;
    }
    if (!RecentBooksStore::isMissing(book)) {
      recentBooks.push_back(book);
    }
  }
  LOG_DBG("HOME", "loadRecentBooks: end heap=%u maxA=%u books=%zu",
               ESP.getFreeHeap(), ESP.getMaxAllocHeap(), recentBooks.size());
}

void HomeActivity::reloadHomeBooks(const int maxBooks) {
  loadRecentBooks(maxBooks);

  const int menuCount = getMenuItemCount();
  if (selectorIndex >= menuCount) {
    selectorIndex = std::max(0, menuCount - 1);
  }

  recentsLoading = false;
  recentsLoaded = !needsRecentCoverLoad(UITheme::getInstance().getMetrics().homeCoverHeight);
  coverRendered = false;
  freeCoverBuffer();
}

bool HomeActivity::needsRecentCoverLoad(const int coverHeight) const {
  for (const RecentBook& book : recentBooks) {
    if (!canLoadHomeCover(book.path)) {
      continue;
    }

    if (book.coverBmpPath.empty()) {
      return true;
    }

    const bool missingThumb = isLyraCarouselTheme() ? !hasCarouselUsableThumb(book)
                                                    : !isValidHomeCoverPath(book.coverBmpPath, coverHeight);
    if (missingThumb) {
      return true;
    }
  }
  return false;
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  LOG_DBG("HOME", "loadRecentCovers: start heap=%u maxA=%u books=%zu",
               ESP.getFreeHeap(), ESP.getMaxAllocHeap(), recentBooks.size());
  recentsLoading = true;
  // The first home render can cache a placeholder while thumbnails are still missing.
  // Drop that cache before generating covers so the next render reads the fresh BMPs.
  coverRendered = false;
  freeCoverBuffer();

  bool showingLoading = false;
  Rect popupRect;
  bool needsRefresh = false;

  const auto updateProgress = [this, &showingLoading, &popupRect](const int progress) {
    RenderLock lock(*this);
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, progress);
  };

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (isLyraCarouselTheme() && progress != lastCarouselBookIndex) {
      progress++;
      continue;
    }
    if (!canLoadHomeCover(book.path)) {
      progress++;
      continue;
    }

    const bool missingThumb =
        book.coverBmpPath.empty() ||
        (isLyraCarouselTheme() ? !hasCarouselUsableThumb(book) : !isValidHomeCoverPath(book.coverBmpPath, coverHeight));
    if (missingThumb) {
      if (isLyraCarouselTheme()) {
        carouselCoverLoadAttemptPath = book.path;
        carouselFramesReady = false;
        invalidateResidentCarouselFrame();
        invalidateCarouselFrameHash();
      }
      updateProgress(10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
      removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);

      if (FsHelpers::hasEpubExtension(book.path)) {
        // EPUB extraction needs the inflate window (~32 KB) + JPEG/PNG decoder.
        // Require a minimum contiguous block; skip this pass if not met (retry on next render).
        LOG_DBG("HOME", "EPUB try: path=%s H=%d lyra=%d free=%u maxA=%u",
                  book.path.c_str(), coverHeight, isLyraCarouselTheme() ? 1 : 0,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (ESP.getMaxAllocHeap() < 32 * 1024) {
          LOG_DBG("HOME", "EPUB SKIP (low heap): maxA=%u", ESP.getMaxAllocHeap());
          progress++;
          continue;
        }
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(isLyraCarouselTheme(), true)) {
          LOG_DBG("HOME", "EPUB load ok: path=%s free=%u maxA=%u",
                    book.path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
          // Epub::getTitle()/getAuthor() return const std::string& (member refs),
          // so a plain copy is the correct, coherent choice here (std::move on a
          // const-ref would just copy anyway). The Epub object is a local that
          // goes out of scope right after this block.
          const std::string epubTitle = epub.getTitle();
          if (!epubTitle.empty()) {
            book.title = epubTitle;
          }
          const std::string epubAuthor = epub.getAuthor();
          if (!epubAuthor.empty()) {
            book.author = epubAuthor;
          }
          book.coverBmpPath = epub.getThumbBmpPath();

          // Heap may be fragmented after parsing OPF/TOC.  Re-check before decode.
          if (ESP.getMaxAllocHeap() < 28 * 1024) {
            LOG_DBG("HOME", "EPUB SKIP post-load (low heap): maxA=%u", ESP.getMaxAllocHeap());
            progress++;
            continue;
          }

          yield();
          esp_task_wdt_reset();

          const bool success =
              isLyraCarouselTheme()
                  ? epub.generateThumbBmp(getCarouselCenterCoverW(), getCarouselCenterCoverH()) &&
                        isValidBmpFile(getCarouselCenterThumbPath(book))
                  : epub.generateThumbBmp(coverHeight) && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          LOG_DBG("HOME", "EPUB thumb result=%d: path=%s free=%u maxA=%u",
                    success ? 1 : 0, book.path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
          if (!success && !isLyraCarouselTheme()) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        } else {
          LOG_DBG("HOME", "EPUB load FAIL: path=%s free=%u maxA=%u",
                    book.path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          std::string title = std::move(xtc.getTitle());
          std::string author = std::move(xtc.getAuthor());
          if (!title.empty()) {
            book.title = std::move(title);
          }
          if (!author.empty()) {
            book.author = std::move(author);
          }
          book.coverBmpPath = xtc.getThumbBmpPath();
          const bool success =
              isLyraCarouselTheme()
                  ? xtc.generateThumbBmp(getCarouselCenterCoverW(), getCarouselCenterCoverH()) &&
                        isValidBmpFile(getCarouselCenterThumbPath(book))
                  : xtc.generateThumbBmp(coverHeight) && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          if (!success && !isLyraCarouselTheme()) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        }
      } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
        Txt txt(book.path, "/.crosspoint");
        if (txt.load()) {
          std::string title = std::move(txt.getTitle());
          if (!title.empty()) {
            book.title = std::move(title);
          }
          book.coverBmpPath = txt.getCoverBmpPath();
          removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
          const bool success = txt.generateCoverBmp() && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          if (!success) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
  if (needsRefresh) {
    if (isLyraCarouselTheme()) {
      carouselFramesReady = false;
      invalidateResidentCarouselFrame();
      invalidateCarouselFrameHash();
      LOG_DBG("HOME", "loadRecentCovers: before preRenderCarouselFrames heap=%u maxA=%u",
                   ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      preRenderCarouselFrames();
      LOG_DBG("HOME", "loadRecentCovers: after preRenderCarouselFrames heap=%u maxA=%u",
                   ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
    requestUpdate();
  }
  LOG_DBG("HOME", "loadRecentCovers: end heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void HomeActivity::scheduleCarouselCoverLoadIfNeeded() {
  if (!isLyraCarouselTheme() || recentBooks.empty() || lastCarouselBookIndex < 0 ||
      lastCarouselBookIndex >= static_cast<int>(recentBooks.size())) {
    return;
  }
  const RecentBook& book = recentBooks[lastCarouselBookIndex];
  if (book.path != carouselCoverLoadAttemptPath && canLoadHomeCover(book.path) &&
      (book.coverBmpPath.empty() || !hasCarouselUsableThumb(book))) {
    recentsLoaded = false;
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("HOME", "onEnter: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  hasOpdsServers = OPDS_STORE.hasServers();

  selectorIndex = 0;
  firstRenderDone = false;
  recentsLoading = false;
  recentsLoaded = false;
  lastCarouselBookIndex = 0;
  invalidateResidentCarouselFrame();
  invalidateCarouselFrameHash();
  carouselFramesReady = false;
  carouselCoverLoadAttemptPath.clear();

  const auto& metrics = UITheme::getInstance().getMetrics();
  reloadHomeBooks(std::min(HOME_MAX_BOOKS, metrics.homeRecentBooksCount));

  LOG_DBG("HOME", "onEnter: after reloadHomeBooks heap=%u maxA=%u books=%zu",
               ESP.getFreeHeap(), ESP.getMaxAllocHeap(), recentBooks.size());
  LOG_DBG("HOME", "onEnter: activityArena used=%u maxUsed=%u capacity=%u",
               static_cast<unsigned>(util::g_activityArena.used()),
               static_cast<unsigned>(util::g_activityArena.maxUsed()),
               static_cast<unsigned>(util::g_activityArena.capacity()));

  // Drop any stale carousel frame cache (e.g. frames rendered with old reading
  // statistics) and force a fresh render — important after returning from a
  // finished read so the carousel shows updated progress/last-read at once.
  // Instead of ensureLoaded(), read from summary.json which is much lighter.
  if (isLyraCarouselTheme()) {
    // Read global summary from summary.json - no need to load full store
    auto global = READING_STATS.getGlobalSummary();
    LOG_DBG("HOME", "onEnter: global summary: total=%llu today=%llu streak=%d",
               (unsigned long long)global.totalReadingMs,
               (unsigned long long)global.todayReadingMs,
               global.currentStreakDays);
    invalidateResidentCarouselFrame();
    invalidateCarouselFrameHash();
    carouselFramesReady = false;
    pruneCarouselFrameCache();
  }

  if (READING_STATS.isHomeInvalidationRequested()) {
    READING_STATS.clearHomeInvalidationRequest();
    if (!isLyraCarouselTheme()) {
      invalidateResidentCarouselFrame();
      invalidateCarouselFrameHash();
      carouselFramesReady = false;
    }
    coverRendered = false;
    freeCoverBuffer();
    LOG_DBG("HOME", "onEnter: stats invalidation requested, refreshed home cache");
  }

  LOG_DBG("HOME", "onEnter: end heap=%u maxA=%u frag=%u(%u+%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // One-time heap compaction before the cover-generation loop (loadRecentCovers)
  // frees renderer temp buffers so the EPUB/JPEG/PNG decoders have the largest
  // possible contiguous block, reducing OOM skips on the Lyra/Marcoand75 paths.
  LOG_DBG("HOME", "freeUnusedRenderMemory: maxA before=%u", ESP.getMaxAllocHeap());
  renderer.freeUnusedRenderMemory();
  LOG_DBG("HOME", "freeUnusedRenderMemory: maxA after=%u", ESP.getMaxAllocHeap());

  requestUpdate();
}
void HomeActivity::onExit() {
  Activity::onExit();
  coverBufferStored = false;  // invalidate before free
  if (coverBuffer) {
    util::g_activityArena.rewind(arenaCoverBufferCursor);
    coverBuffer = nullptr;
    coverBufferSize = 0;
  }
  // Log heap state after freeing the cover buffer. The next activity
  // (typically Library) can use this to schedule its scan with awareness of
  // fragmentation.
  LOG_DBG("HOME", "onExit: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

bool HomeActivity::storeCoverBuffer() {
  if (coverRectW <= 0 || coverRectH <= 0) return false;

  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;

  // Release previous arena allocation by rewinding to the cursor before it.
  if (coverBuffer) {
    util::g_activityArena.rewind(arenaCoverBufferCursor);
    coverBuffer = nullptr;
    coverBufferSize = 0;
  }

  arenaCoverBufferCursor = util::g_activityArena.cursor();
  coverBuffer = static_cast<uint8_t*>(util::g_activityArena.push(needed));
  if (!coverBuffer) {
    arenaCoverBufferCursor = 0;
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", static_cast<unsigned>(needed));
    return false;
  }
  coverBufferSize = needed;

  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, needed)) {
    // Keep the buffer allocated — a transient copy failure is not a reason
    // to free it and re-fragment the heap.
    coverBufferStored = false;
    return false;
  }

  coverBufferStored = true;
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed > coverBufferSize) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, needed);
}

/// Mark the cover buffer as stale so the next render re-stores it.
/// Does NOT free the underlying allocation — only onExit() does that.
void HomeActivity::freeCoverBuffer() {
  coverBufferStored = false;
}

bool HomeActivity::loadCarouselFrameFromStorage(int bookIndex) {
  if (recentBooks.empty()) {
    return false;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  const size_t bufferSize = renderer.getBufferSize();
  char cachePath[128];
  getCarouselFrameCachePathFromHash(getCachedCarouselFrameHash(safeBookIndex), cachePath, sizeof(cachePath));
  const unsigned long dbgRead0 = millis();

  FsFile file;
  if (!Storage.openFileForRead("HCR", cachePath, file)) {
    LOG_DBG("HCR", "loadCarouselFrameFromStorage: MISS idx=%d (no file)", safeBookIndex);
    return false;
  }

  if (file.size() != bufferSize) {
    file.close();
    Storage.remove(cachePath);
    LOG_DBG("HCR", "loadCarouselFrameFromStorage: MISS idx=%d (size mismatch)", safeBookIndex);
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    file.close();
    return false;
  }

  size_t totalRead = 0;
  while (totalRead < bufferSize) {
    const int bytesRead = file.read(frameBuffer + totalRead, bufferSize - totalRead);
    if (bytesRead <= 0) {
      break;
    }
    totalRead += static_cast<size_t>(bytesRead);
  }
  file.close();

  if (totalRead != bufferSize) {
    Storage.remove(cachePath);
    invalidateResidentCarouselFrame();
    LOG_DBG("HCR", "loadCarouselFrameFromStorage: MISS idx=%d (short read %zu/%zu)",
            safeBookIndex, totalRead, bufferSize);
    return false;
  }

  invalidateResidentCarouselFrame();
  carouselFramesReady = true;
  // Frame content is now fully in the frame buffer. Re-anchor the render timer
  // so the subsequent displayBuffer() GFX log measures only the compositing
  // work done on top of the cached frame, not time since an old clearScreen().
  renderer.resetRenderTimer();
  LOG_DBG("HCR", "loadCarouselFrameFromStorage: HIT idx=%d (%zu bytes, read=%ums)",
          safeBookIndex, bufferSize, static_cast<int>(millis() - dbgRead0));
  return true;
}

bool HomeActivity::saveCarouselFrameToStorage(int bookIndex) {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  const size_t bufferSize = renderer.getBufferSize();
  char cachePath[128];
  getCarouselFrameCachePathFromHash(getCachedCarouselFrameHash(safeBookIndex), cachePath, sizeof(cachePath));

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(getCarouselFrameCacheDir());

  FsFile file;
  if (!Storage.openFileForWrite("HCR", cachePath, file)) {
    return false;
  }

  const size_t written = file.write(frameBuffer, bufferSize);
  file.close();

  if (written != bufferSize) {
    Storage.remove(cachePath);
    return false;
  }

  return true;
}

bool HomeActivity::renderCarouselFrame(int bookIndex) {
  if (recentBooks.empty()) {
    return false;
  }

  const unsigned long dbgT0 = millis();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  bool localCoverRendered = false;
  bool localCoverBufferStored = false;
  bool localBufferRestored = false;
  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  // setPreRenderIndex sets lastCarouselSelectorIndex so drawRecentBookCover
  // picks the correct center book. We pass bookCount (not safeBookIndex) as
  // selectorIndex so inCarouselRow=false and the frame is stored with a thin
  // outline; drawCarouselBorder() overlays the thick selection border at
  // display time only when the carousel row is actually active.
  if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_MARCOAND75) {
    LyraMarcoand75Theme::setPreRenderIndex(safeBookIndex);
  } else {
    LyraCarouselTheme::setPreRenderIndex(safeBookIndex);
  }
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, bookCount, localCoverRendered, localCoverBufferStored, localBufferRestored,
                          [] { return false; });
  LOG_DBG("HCR", "renderCarouselFrame: idx=%d drawRecentBookCover=%ums", safeBookIndex,
          static_cast<int>(millis() - dbgT0));

  if (!renderer.getFrameBuffer()) {
    invalidateResidentCarouselFrame();
    return false;
  }
  invalidateResidentCarouselFrame();
  carouselFramesReady = true;
  saveCarouselFrameToStorage(safeBookIndex);
  LOG_DBG("HCR", "renderCarouselFrame: idx=%d total=%ums", safeBookIndex,
          static_cast<int>(millis() - dbgT0));
  return true;
}

void HomeActivity::invalidateResidentCarouselFrame() {
  residentCarouselFrameIndex = -1;
  residentCarouselSelectorIndex = -1;
  residentCarouselFrameHash = 0;
  residentCarouselFrameValid = false;
}

void HomeActivity::invalidateCarouselFrameHash() {
  cachedCarouselFrameHashIndex = -1;
  cachedCarouselFrameHash = 0;
  cachedCarouselFrameHashValid = false;
  cachedCarouselFramePrefixHash = 0;
  cachedCarouselFramePrefixValid = false;
}

void HomeActivity::requestFreshHomeRender(const bool immediate) {
  if (isLyraCarouselTheme()) {
    invalidateResidentCarouselFrame();
  }
  requestUpdate(immediate);
}

uint32_t HomeActivity::getCachedCarouselFrameHash(const int bookIndex) {
  if (recentBooks.empty()) {
    return 0;
  }

  const int safeBookIndex = wrapBookIndex(bookIndex, static_cast<int>(recentBooks.size()));
  if (cachedCarouselFrameHashValid && cachedCarouselFrameHashIndex == safeBookIndex) {
    return cachedCarouselFrameHash;
  }

  // Compute the expensive per-book prefix only once, then derive each frame
  // key by appending the center index. This turns O(N) SD accesses into O(1)
  // for every index after the first lookup in a render pass.
  if (!cachedCarouselFramePrefixValid) {
    cachedCarouselFramePrefixHash =
        getCarouselFramePrefixHash(recentBooks, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                   renderer.getBufferSize(), renderer.isDarkMode());
    cachedCarouselFramePrefixValid = true;
  }

  cachedCarouselFrameHash = CarouselHash::fnv1aU32(cachedCarouselFramePrefixHash,
                                                   static_cast<uint32_t>(safeBookIndex));
  cachedCarouselFrameHashIndex = safeBookIndex;
  cachedCarouselFrameHashValid = true;
  return cachedCarouselFrameHash;
}

void HomeActivity::preRenderCarouselFrames() {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return;
  }

  freeCoverBuffer();
  const int bookCount = static_cast<int>(recentBooks.size());
  const int centerIdx = wrapBookIndex(lastCarouselBookIndex, bookCount);
  // Load the center frame from the SD cache directly into the frame buffer, or
  // render it fresh and write it to the SD cache for future loads.
  if (!loadCarouselFrameFromStorage(centerIdx)) {
    renderCarouselFrame(centerIdx);
  }
  carouselFramesReady = true;
}

void HomeActivity::pruneCarouselFrameCache() {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return;
  }

  const char* cacheDir = getCarouselFrameCacheDir();
  Storage.mkdir(cacheDir);

  // Collect the set of frame hashes that are still valid for the current book
  // set. The frame hash already folds in progress/last-read stats, so a frame
  // cached with stale statistics produces a different hash and is dropped here
  // — this both bounds cache growth and guarantees a fresh frame after reading.
  //
  // O(N): compute the expensive per-book prefix (thumb-state Storage.exists +
  // progress % for every book) ONCE, then derive each frame key by hashing the
  // center index. The previous single-entry cache re-ran the whole per-book
  // loop for every index (O(N^2) SD accesses on startup).
  const uint32_t prefix =
      getCarouselFramePrefixHash(recentBooks, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                 renderer.getBufferSize(), renderer.isDarkMode());
  util::StaticVector<uint32_t, 32> validHashes;
  validHashes.reserve(static_cast<size_t>(recentBooks.size()));
  for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
    validHashes.push_back(CarouselHash::fnv1aU32(prefix, static_cast<uint32_t>(i)));
  }
  std::sort(validHashes.begin(), validHashes.end());
  invalidateCarouselFrameHash();

  auto d = Storage.open(cacheDir);
  if (!d || !d.isDirectory()) {
    return;
  }
  d.rewindDirectory();
  char nb[96];
  for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    f.getName(nb, sizeof(nb));
    f.close();
    const size_t nameLen = std::strlen(nb);
    if (nameLen < 9 || std::strcmp(nb + nameLen - 4, ".bin") != 0) {
      continue;
    }
    const uint32_t h = static_cast<uint32_t>(std::strtoul(nb, nullptr, 16));
    if (!std::binary_search(validHashes.begin(), validHashes.end(), h)) {
      char full[128];
      std::snprintf(full, sizeof(full), "%s/%s", cacheDir, nb);
      Storage.remove(full);
    }
  }
  d.close();
}

void HomeActivity::loop() {
  if (firstRenderDone && !recentsLoaded && !recentsLoading) {
    loadRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
    return;
  }

  const int menuCount = getMenuItemCount();
  const int shortcutCount = getHomeShortcutEntries(homeEntriesCache, hasOpdsServers);
  const int carouselCount = isLyraCarouselTheme() ? buildCarouselEntries(carouselEntriesCache, homeEntriesCache) : shortcutCount;
  const util::StaticVector<HomeShortcutEntry, 32>& homeEntries =
      isLyraCarouselTheme() ? carouselEntriesCache : homeEntriesCache;
  const int recentCount = static_cast<int>(recentBooks.size());
  const int homeCount = carouselCount;
  const int shortcutPageSize = getHomeShortcutPageSize();

  if (isLyraCarouselTheme()) {
    // Carousel navigation: Left/Right move within the focused row;
    // Up/Down toggle between the carousel row and the shortcuts row.
    const bool inCarouselRow = recentCount > 0 && selectorIndex < recentCount;

    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + recentCount - 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + homeCount - 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow && homeCount > 0) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = recentCount;  // land on first shortcut
        requestUpdate();
      } else if (!inCarouselRow && recentCount > 0) {
        selectorIndex = wrapBookIndex(lastCarouselBookIndex, recentCount);
        requestUpdate();
      }
    }
  } else {
    buttonNavigator.onNextPress([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPreviousPress([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, menuCount, recentCount, homeCount, shortcutPageSize] {
      if (menuCount <= 0) {
        return;
      }

      if (homeCount <= shortcutPageSize) {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      } else if (selectorIndex < recentCount) {
        selectorIndex = recentCount;
      } else {
        const int selectedHomeIndex = selectorIndex - recentCount;
        selectorIndex =
            recentCount + ButtonNavigator::nextPageIndex(selectedHomeIndex, homeCount, shortcutPageSize);
      }
      requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, menuCount, recentCount, homeCount, shortcutPageSize] {
      if (menuCount <= 0) {
        return;
      }

      if (homeCount <= shortcutPageSize) {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      } else if (selectorIndex < recentCount) {
        selectorIndex = recentCount + ButtonNavigator::previousPageIndex(0, homeCount, shortcutPageSize);
      } else {
        const int selectedHomeIndex = selectorIndex - recentCount;
        selectorIndex =
            recentCount + ButtonNavigator::previousPageIndex(selectedHomeIndex, homeCount, shortcutPageSize);
      }
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < recentBooks.size()) {
      if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
        const RecentBook selectedBook = recentBooks[selectorIndex];
        const int currentSelection = selectorIndex;
        const bool deleteFromFavorites = homeUsesFavorites();
        const bool isEpub = FsHelpers::hasEpubExtension(selectedBook.path);
        const bool isFavorite =
            deleteFromFavorites || FAVORITES.isFavorite(selectedBook.path);

        // Check reading status
        SummaryJSON::BookBadge badge;
        const bool hasBadge = READING_STATS.getBookHomeStats(selectedBook.bookId, selectedBook.path, badge);
        const bool isCompleted = hasBadge && badge.completed;

        const std::string subtitle = !selectedBook.author.empty() ? selectedBook.author : selectedBook.path;

        startActivityForResult(
            std::make_unique<BookContextMenuActivity>(renderer, mappedInput,
                                                      getRecentBookConfirmationLabel(selectedBook),
                                                      isFavorite, isCompleted, isEpub),
            [this, selectedBook, currentSelection, deleteFromFavorites, isCompleted](const ActivityResult& result) {
              if (isLyraCarouselTheme()) {
                invalidateResidentCarouselFrame();
              }

              if (result.isCancelled) {
                // Refresh home when returning from metadata view
                if (isLyraCarouselTheme()) {
                  lastCarouselBookIndex = currentSelection;
                }
                requestUpdate(true);
                return;
              }

              const auto* menuResult = std::get_if<MenuResult>(&result.data);
              if (!menuResult) {
                requestUpdate(true);
                return;
              }

              const int action = menuResult->action;
              switch (action) {
                case static_cast<int>(BookContextMenuActivity::MenuAction::REMOVE_FROM_RECENTS): {
                  const bool removed = deleteFromFavorites
                                           ? FAVORITES.removeBook(selectedBook.path)
                                           : RECENT_BOOKS.removeBook(selectedBook.path);
                  if (removed) {
                    const auto& metrics = UITheme::getInstance().getMetrics();
                    reloadHomeBooks(std::min(HOME_MAX_BOOKS, metrics.homeRecentBooksCount));
                    if (recentBooks.empty()) {
                      selectorIndex = 0;
                    } else if (currentSelection >= static_cast<int>(recentBooks.size())) {
                      selectorIndex = static_cast<int>(recentBooks.size()) - 1;
                    } else {
                      selectorIndex = currentSelection;
                    }
                    if (isLyraCarouselTheme()) {
                      lastCarouselBookIndex = selectorIndex < static_cast<int>(recentBooks.size()) ? selectorIndex : 0;
                      preRenderCarouselFrames();
                    }
                  }
                  break;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::ADD_TO_FAVORITES): {
                  FAVORITES.toggleBook(selectedBook.path);
                  break;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::VIEW_METADATA): {
                  startActivityForResult(
                      std::make_unique<BookMetadataActivity>(renderer, mappedInput, selectedBook.path),
                      [this](const ActivityResult&) { requestFreshHomeRender(true); });
                  return;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::MARK_READ_UNREAD): {
                  // Toggle completed status — beginSession creates the book entry if missing
                  READING_STATS.beginSession(selectedBook.path, selectedBook.title,
                                             selectedBook.author, selectedBook.coverBmpPath,
                                             isCompleted ? 0 : 100);
                  READING_STATS.endSession();
                  // Force save so the change persists immediately, even with zero reading time.
                  READING_STATS.saveToFile();
                  break;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::OPEN_BOOK): {
                  onSelectBook(selectedBook.path);
                  return;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::VIEW_STATS): {
                  activityManager.replaceActivity(
                      std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, selectedBook.path));
                  return;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::DELETE_CACHE): {
                  Epub epub(selectedBook.path, "/.crosspoint");
                  epub.load(false, true);
                  epub.clearCache();
                  break;
                }
                case static_cast<int>(BookContextMenuActivity::MenuAction::CLEAR_THEME_CACHE): {
                  // Delete all *.bin files in the active theme cache directory
                  invalidateResidentCarouselFrame();
                  invalidateCarouselFrameHash();
                  const char* cacheDir = getCarouselFrameCacheDir();
                  Storage.mkdir(cacheDir);
                  auto d = Storage.open(cacheDir);
                  if (d && d.isDirectory()) {
                    d.rewindDirectory();
                    char nb[96];
                    for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
                      f.getName(nb, sizeof(nb));
                      if (!f.isDirectory()) {
                        std::string full = std::string(cacheDir) + "/" + nb;
                        f.close();
                        Storage.remove(full.c_str());
                      } else {
                        f.close();
                      }
                    }
                    d.close();
                  }
                  break;
                }
              }
              requestUpdate(true);
            });
        return;
      }

      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    const int homeIndex = selectorIndex - static_cast<int>(recentBooks.size());
    if (homeIndex < 0 || homeIndex >= static_cast<int>(homeEntries.size())) {
      return;
    }

    const auto& selectedEntry = homeEntries[homeIndex];
    if (selectedEntry.isAppsHub) {
      onAppsOpen();
    } else if (selectedEntry.definition) {
      switch (selectedEntry.definition->id) {
        case ShortcutId::BrowseFiles:
          onFileBrowserOpen();
          break;
        case ShortcutId::ReadingStats:
          onReadingStatsOpen();
          break;
        case ShortcutId::SyncDay:
          onSyncDayOpen();
          break;
        case ShortcutId::Settings:
          activityManager.goToSettings();
          break;
        case ShortcutId::ReadingHeatmap:
          startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::ReadingProfile:
          startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::Library:
          if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
            startActivityForResult(
                std::make_unique<LibraryContextMenuActivity>(renderer, mappedInput),
                [this](const ActivityResult&) { requestFreshHomeRender(true); });
          } else {
            activityManager.goToLibrary();
          }
          break;
        case ShortcutId::Achievements:
          startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::IfFound:
          startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::RecentBooks:
          activityManager.goToRecentBooks();
          break;
        case ShortcutId::Bookmarks:
          startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::Favorites:
          startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) {
                                   const auto& metrics = UITheme::getInstance().getMetrics();
                                   reloadHomeBooks(std::min(HOME_MAX_BOOKS, metrics.homeRecentBooksCount));
                                   requestFreshHomeRender(true);
                                 });
          break;
        case ShortcutId::Flashcards:
          startActivityForResult(std::make_unique<FlashcardsAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::Dictionary:
          startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::FileTransfer:
          activityManager.goToFileTransfer();
          break;
        case ShortcutId::Sleep:
          startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
        case ShortcutId::ScreenSaver:
          startActivityForResult(std::make_unique<ScreenSaverActivity>(renderer, mappedInput),
                                 [this](const ActivityResult&) { requestFreshHomeRender(true); });
          break;
         case ShortcutId::Clippings:
           startActivityForResult(std::make_unique<ClippingsAppActivity>(renderer, mappedInput),
                                  [this](const ActivityResult&) { requestFreshHomeRender(true); });
           break;
          case ShortcutId::Wikipedia:
            onWikipediaOpen();
            break;
          case ShortcutId::QuickCards:
            startActivityForResult(std::make_unique<QuickCardsActivity>(renderer, mappedInput),
                                   [this](const ActivityResult&) { requestFreshHomeRender(true); });
            break;
          case ShortcutId::Plugins:
            activityManager.goToPluginBrowser();
            break;
          case ShortcutId::OpdsBrowser:
           onOpdsBrowserOpen();
          break;
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const unsigned long dbgRender0 = millis();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int recentCount = static_cast<int>(recentBooks.size());
  const bool carouselTheme = isLyraCarouselTheme();
  const bool wasFirstRenderDone = firstRenderDone;
  const bool inCarouselRow = carouselTheme && selectorIndex < recentCount;
  if (inCarouselRow) {
    lastCarouselBookIndex = selectorIndex;
    scheduleCarouselCoverLoadIfNeeded();
  }

  bool usedCarouselFrame = false;
  if (carouselTheme && !recentBooks.empty()) {
    const int centerIdx = wrapBookIndex(lastCarouselBookIndex, recentCount);
    const unsigned long dbgHash0 = millis();
    const uint32_t frameHash = getCachedCarouselFrameHash(centerIdx);
    LOG_DBG("HCR", "render getCarouselFrameHash: idx=%d hash=%08x %ums",
            centerIdx, frameHash, static_cast<int>(millis() - dbgHash0));
    const bool residentFrameMatches = residentCarouselFrameValid && residentCarouselFrameIndex == centerIdx &&
                                      residentCarouselSelectorIndex == selectorIndex &&
                                      residentCarouselFrameHash == frameHash;
    if (!residentFrameMatches) {
      // Load the center frame from the SD cache directly into the frame buffer,
      // or render it fresh (and cache to SD). No intermediate slot allocation.
      if (!loadCarouselFrameFromStorage(centerIdx)) {
        renderCarouselFrame(centerIdx);
      }
    }

    const unsigned long dbgHIT0 = millis();
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    if (frameBuffer) {
      renderer.fillRect(0, 0, pageWidth, metrics.homeTopPadding, false);
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
      HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
      drawCarouselRecentsPanel(renderer, recentCount);
      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             inCarouselRow);
      LOG_DBG("HCR", "live render carousel frame overlay: %ums (fillRect+header+border)",
              static_cast<int>(millis() - dbgHIT0));
      usedCarouselFrame = true;
      residentCarouselFrameIndex = centerIdx;
      residentCarouselSelectorIndex = selectorIndex;
      residentCarouselFrameHash = frameHash;
      residentCarouselFrameValid = true;
    }
  }

  if (!usedCarouselFrame) {
    invalidateResidentCarouselFrame();
    renderer.clearScreen();
    bool bufferRestored = coverBufferStored && restoreCoverBuffer();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
    HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
    drawCarouselRecentsPanel(renderer, recentCount);

    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = metrics.homeCoverTileHeight;
    GUI.drawRecentBookCover(renderer, Rect{coverRectX, coverRectY, coverRectW, coverRectH},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  const int shortcutCount = getHomeShortcutEntries(homeEntriesCache, hasOpdsServers);
  const int carouselCount = isLyraCarouselTheme() ? buildCarouselEntries(carouselEntriesCache, homeEntriesCache) : shortcutCount;
  const util::StaticVector<HomeShortcutEntry, 32>& homeEntries =
      isLyraCarouselTheme() ? carouselEntriesCache : homeEntriesCache;
  const int selectedHomeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const Rect shortcutsRect{
      0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
      pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing +
                    metrics.buttonHintsHeight + metrics.verticalSpacing)};

  const int shortcutDisplayCount = static_cast<int>(homeEntries.size());
  const int shortcutPageSize = getHomeShortcutPageSize();

  const unsigned long dbgMenu0 = millis();
  if (carouselTheme || shortcutDisplayCount <= shortcutPageSize) {
    GUI.drawButtonMenu(
        renderer, shortcutsRect, shortcutDisplayCount, selectedHomeIndex,
        [&homeEntries](const int index) { return getHomeShortcutTitle(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutIcon(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutSubtitle(homeEntries[index]); },
        [&homeEntries](const int index) { return showHomeShortcutAccessory(homeEntries[index]); });
  } else {
    const int headerHeight = 34;
    const int listTop = shortcutsRect.y + headerHeight + 12;
    const int listHeight = std::max(0, shortcutsRect.height - headerHeight - 12);
    const int currentPage = std::max(0, selectedHomeIndex >= 0 ? selectedHomeIndex / shortcutPageSize : 0);
    const int totalPages =
        (static_cast<int>(homeEntries.size()) + shortcutPageSize - 1) / shortcutPageSize;
    const int pageStart = currentPage * shortcutPageSize;
    const int pageItemCount = std::min(shortcutPageSize, static_cast<int>(homeEntries.size()) - pageStart);
    const int localSelectedIndex = (selectedHomeIndex >= pageStart && selectedHomeIndex < pageStart + pageItemCount)
                                       ? selectedHomeIndex - pageStart
                                       : -1;
    char sectionLabel[64];
    snprintf(sectionLabel, sizeof(sectionLabel), "%s (%d)", tr(STR_SHORTCUTS_SECTION), static_cast<int>(homeEntries.size()));
    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "%d/%d", currentPage + 1, totalPages);

    GUI.drawSubHeader(
        renderer,
        Rect{metrics.contentSidePadding, shortcutsRect.y, pageWidth - metrics.contentSidePadding * 2, headerHeight},
        sectionLabel, pageLabel);
    GUI.drawButtonMenu(
        renderer, Rect{0, listTop, pageWidth, listHeight}, pageItemCount, localSelectedIndex,
        [&homeEntries, pageStart](const int index) { return getHomeShortcutTitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutIcon(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutSubtitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) {
          return showHomeShortcutAccessory(homeEntries[pageStart + index]);
        });

    // Draw scrollbar for shortcuts pagination
    if (totalPages > 1) {
      constexpr int scrollBarWidth = 4;
      constexpr int scrollBarGap = 6;
      const int scrollTrackX = pageWidth - metrics.contentSidePadding;
      const int scrollBarHeight = std::max(18, (listHeight * listHeight) / (shortcutDisplayCount * listHeight / totalPages));
      const int scrollBarY = listTop + ((listHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
      renderer.drawLine(scrollTrackX, listTop, scrollTrackX, listTop + listHeight, true);
      renderer.fillRect(scrollTrackX - scrollBarWidth + 1, scrollBarY, scrollBarWidth, scrollBarHeight, true);
    }
  }
  LOG_DBG("HCR", "render drawButtonMenu/icons: %ums", static_cast<int>(millis() - dbgMenu0));

  ListRenderHelper::drawHints(renderer, mappedInput, "", tr(STR_SELECT),
                              carouselTheme ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                              carouselTheme ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  LOG_DBG("HCR", "render pre-displayBuffer cumulative (post-carousel): %ums total=%ums",
          static_cast<int>(millis() - dbgMenu0), static_cast<int>(millis() - dbgRender0));

  const unsigned long dbgDisp0 = millis();
  renderer.displayBuffer();
  LOG_DBG("HCR", "render displayBuffer: %ums (cumulative in-render=%ums)",
          static_cast<int>(millis() - dbgDisp0), static_cast<int>(millis() - dbgRender0));

  if (wasFirstRenderDone && carouselTheme && recentsLoaded && !carouselFramesReady && !recentBooks.empty()) {
    preRenderCarouselFrames();
    if (carouselFramesReady) {
      requestUpdate();
    }
  }

  if (!firstRenderDone) {
    firstRenderDone = true;
    if (!recentsLoaded || (carouselTheme && recentsLoaded && !carouselFramesReady)) {
      requestUpdate();
    }
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onAppsOpen() { activityManager.goToApps(); }

void HomeActivity::onReadingStatsOpen() {
  activityManager.replaceActivity(std::make_unique<ReadingStatsActivity>(renderer, mappedInput));
}

void HomeActivity::onSyncDayOpen() {
  activityManager.replaceActivity(std::make_unique<SyncDayActivity>(renderer, mappedInput));
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onWikipediaOpen() {
  startActivityForResult(
      std::make_unique<WikipediaActivity>(renderer, mappedInput),
      [this](const ActivityResult&) { requestFreshHomeRender(true); });
}

void HomeActivity::drawCarouselRecentsPanel(GfxRenderer& renderer, const int totalBooks) {
  // Panel shows carousel recents count + filter info only in Lyra Marcoand75 theme.
  // Lyra Carousel has its own dedicated cover display without this overlay panel.
  auto theme = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  if (theme != CrossPointSettings::UI_THEME::LYRA_MARCOAND75 || totalBooks == 0) return;

  const int screenW = renderer.getScreenWidth();
  // Height: progress bar panel (42px) + 8px extra = 50px
  constexpr int panelH = 50;
  // Width: 6/10 of screen minus 16px
  const int panelW = screenW * 6 / 10 - 16;
  constexpr int panelX = 8;
  constexpr int panelY = 10;  // 10px margin above
  const int fontId = UI_12_FONT_ID;
  const int lh = renderer.getLineHeight(fontId);
  constexpr int textLeft = 20;  // same spacing as book title in panel below

  PanelDrawHelper::drawCyberpunkPanel(renderer, panelX, panelY, panelW, panelH, true);

  char buf[48];
  snprintf(buf, sizeof(buf), "%s (%d)", tr(STR_CAROUSEL_RECENTS), totalBooks);
  const int textY = panelY + (panelH - lh) / 2;
  renderer.drawText(fontId, panelX + textLeft, textY, buf, true, EpdFontFamily::BOLD);
}