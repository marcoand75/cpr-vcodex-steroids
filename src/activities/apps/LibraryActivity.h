#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "CrossPointSettings.h"
#include "components/LibraryCache.h"
#include "components/LibraryPopupOverlay.h"
#include "util/ButtonNavigator.h"

class LibraryActivity final : public Activity {
 private:
  int selectorIndex_ = 0;
  // Cached window: a consecutive block of entries loaded from the v3 cache file.
  // This is the ONLY Entry storage in the activity — no full-library vector exists.
  // The window is at most kEntryWindow (64) entries, refreshed on page boundary.
  std::vector<LibraryCache::Entry> windowEntries_;
  int windowStart_ = 0;        // global index of the first entry in window_
  int windowCount_ = 0;        // actual number of entries in window_
  int totalBooks_ = 0;         // total books reported by cache header

  int coverGenIndex_ = -1;
  bool coversComplete_ = false;
  int coverPassCount_ = 0;
  uint64_t coverGeneratedMask_ = 0;
  int coverGenRenderBatch_ = 0;
  static constexpr int kCoverRenderBatchEvery = 1;
  static constexpr int kMaxCoverPasses = 2;
  int lastPage_ = -1;
  mutable int lastRenderedPage_ = -1;
  mutable int lastRenderedSelectorIndex_ = -1;
  mutable bool lastRenderedCoversComplete_ = false;
  mutable bool forceRender_ = true;

  // Render cache: header info / selected title are rebuilt only when their
  // inputs change, keeping render allocation-free during per-page indexing.
  std::string cachedInfo_;
  std::string cachedSelTitle_;
  int cachedRenderSelector_ = -1;
  int cachedRenderPage_ = -1;
  CrossPointSettings::LIBRARY_FILTER cachedInfoFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
  CrossPointSettings::LIBRARY_SORT cachedInfoSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
  std::string cachedInfoSearch_;
  std::vector<std::vector<std::string>> pageTitleCache_;
  int pageTitleCacheKey_ = -1;

  int coverWidth_ = 100;
  int coverHeight_ = 150;
  int gridColumns_ = 4;
  int gridsPerPage_ = 16;
  int gap_ = 7;
  int rowPad_ = 8;
  CrossPointSettings::LIBRARY_FILTER currentFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
  CrossPointSettings::LIBRARY_SORT currentSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
  std::string currentSearchText_;
  uint8_t lastLayoutSetting_ = CrossPointSettings::LIBRARY_LAYOUT_4X4;

  // Popup state
  enum class PopupMode { None, Sort, Filter };
  PopupMode popupMode_ = PopupMode::None;
  LibraryPopupOverlay popupOverlay_;

  // Long-press tracking for Up/Down
  bool upHeld_ = false;
  bool upLongTriggered_ = false;
  bool downHeld_ = false;
  bool downLongTriggered_ = false;
  int popupSpawnButton_ = -1;
  static constexpr unsigned long kLongPressMs = 800;

  void applyLayoutFromSettings();
  void ensureLayoutUpToDate();
  void scanSd();
  void applyFilterAndSort();
  // Ensure the cached window covers the current selector position.
  // Loads a new window from the cache file when the selector moves outside
  // the current window range.
  void ensureWindowForIndex(int index);
  bool isBookCoverReady(const std::string& path, size_t slot) const;
  void drawTileContent(int i, int pageStart, int x, int y) const;
  void deleteLibraryCovers(const std::string& bookPath);
  void deletePageCovers();
  void deleteAllLibraryCovers();
  void rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter);

  void openSortPopup();
  void openFilterPopup();
  void closePopup();
  void selectPopupItem();
  void beginTextSearch();

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
