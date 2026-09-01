#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "./FileBrowserActivity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"
#include "util/ArenaAllocator.h"
#include "util/ShortcutRegistry.h"

struct RecentBook;
struct Rect;

// Lightweight shortcut descriptor used by HomeActivity's shortcut rows.
// Stored inline in StaticVector so no heap allocation is needed per frame.
struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
};

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // Arena-allocated buffer for cover image
  size_t coverBufferSize = 0;
  size_t arenaCoverBufferCursor = 0;  // Arena cursor at cover buffer allocation
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  int lastCarouselBookIndex = 0;
  int residentCarouselFrameIndex = -1;
  int residentCarouselSelectorIndex = -1;
  uint32_t residentCarouselFrameHash = 0;
  bool residentCarouselFrameValid = false;
  int cachedCarouselFrameHashIndex = -1;
  uint32_t cachedCarouselFrameHash = 0;
  bool cachedCarouselFrameHashValid = false;
  std::string carouselCoverLoadAttemptPath;
  bool carouselFramesReady = false;
  std::vector<RecentBook> recentBooks;
  // Arena-backed shortcut entries (replaces per-frame std::vector).
  mutable util::StaticVector<HomeShortcutEntry, 32> homeEntriesCache;
  mutable util::StaticVector<HomeShortcutEntry, 32> carouselEntriesCache;
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onAppsOpen();
  void onReadingStatsOpen();
  void onSyncDayOpen();
  void onOpdsBrowserOpen();
  void onWikipediaOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void preRenderCarouselFrames();
  bool renderCarouselFrame(int bookIndex);
  bool loadCarouselFrameFromStorage(int bookIndex);
  bool saveCarouselFrameToStorage(int bookIndex);
  void invalidateResidentCarouselFrame();
  void invalidateCarouselFrameHash();
  void requestFreshHomeRender(bool immediate = false);
  uint32_t getCachedCarouselFrameHash(int bookIndex);
  void scheduleCarouselCoverLoadIfNeeded();
  void pruneCarouselFrameCache();  // remove cached frames no longer matching the current book set
  void loadRecentBooks(int maxBooks);
  void reloadHomeBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  bool needsRecentCoverLoad(int coverHeight) const;
  void drawCarouselRecentsPanel(GfxRenderer& renderer, int totalBooks);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};