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
  std::vector<LibraryCache::Entry> entries_;
  std::vector<LibraryCache::Entry> unfilteredEntries_;
  int coverGenIndex_ = -1;
  bool coversComplete_ = false;
  int coverPassCount_ = 0;  // number of full indexing passes attempted on the current page
  uint64_t coverGeneratedMask_ = 0;  // bitmask of slots already generated this page pass
  int coverGenRenderBatch_ = 0;      // count of covers generated since last render
  static constexpr int kCoverRenderBatchEvery = 1;  // render every N covers (1 = per-cover, 2 = every other, etc.)
  static constexpr int kMaxCoverPasses = 2;  // give up after this many passes; failed covers fall back to placeholder
  int lastPage_ = -1;
  mutable int lastRenderedPage_ = -1;
  mutable int lastRenderedSelectorIndex_ = -1;
  mutable bool lastRenderedCoversComplete_ = false;  // avoids redundant redraws during indexing
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
  // Wrapped cover-title lines for the currently rendered page (at most
  // gridsPerPage_ entries). Built once per page change so the render loop does
  // not allocate during per-page cover indexing. Keyed by pageStart.
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
  // Button whose long-press spawned the active popup (ordinal, or -1 when
  // none); its release is consumed so it does not also move the popup selection.
  int popupSpawnButton_ = -1;
  static constexpr unsigned long kLongPressMs = 800;

  void applyLayoutFromSettings();
  void ensureLayoutUpToDate();
  void scanSd();
  void applyFilterAndSort();
  bool isBookCoverReady(const LibraryCache::Entry& entry) const;
  // Slot-aware overload: skips the linear search over `entries_` and checks the
  // thumbnail file + the current-page generation mask directly. `slot` is the
  // zero-based position within the current page (pageStart-relative).
  bool isBookCoverReady(const std::string& path, size_t slot) const;
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
};