#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "CrossPointSettings.h"
#include "components/LibraryIndex.h"
#include "components/LibraryPopupOverlay.h"
#include "util/LongPress.h"

class LibraryActivity final : public Activity {
 private:
  int selectorIndex_ = 0;
  int totalBooks_ = 0;               // total non-tombstone books
  int totalPages_ = 0;               // total pages based on grid
  int lastPage_ = 0;                 // last rendered page for page-change detection
  mutable int lastRenderedPage_ = -1;
  mutable int lastRenderedSelectorIndex_ = -1;
  mutable bool forceRender_ = true;

  // Current page cache (one page worth of BookRefs)
  static constexpr int kMaxPageSlots = 16;  // max 4x4 grid
  LibraryIndex::BookRef pageCache_[kMaxPageSlots];

  // Render cache
  std::string cachedInfo_;
  std::string cachedSelTitle_;
  std::string cachedSelAuthor_;
  int cachedRenderSelector_ = -1;
  int cachedRenderPage_ = -1;
  mutable int cachedTotalBooks_ = -1;  // invalidate render when total changes
  CrossPointSettings::LIBRARY_FILTER cachedInfoFilter_ = CrossPointSettings::LIBRARY_FILTER_ALL;
  CrossPointSettings::LIBRARY_SORT cachedInfoSort_ = CrossPointSettings::LIBRARY_SORT_TITLE_ASC;
  std::string cachedInfoSearch_;
  mutable bool cachedCollectionsMode_ = false;
  mutable int  cachedCollectionIdx_ = -2;
  mutable std::string cachedCollectionName_;
  std::vector<std::vector<std::string>> pageTitleCache_;
  int pageTitleCacheKey_ = -1;

  int prevBorderIdx_ = -1;

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

  // Collections mode
  bool collectionsMode_ = false;         // true when browsing collections
  int  currentCollectionIdx_ = -1;       // selected collection index (-1 = list of collections)
  std::string currentCollectionName_;    // name of currently opened collection

  // Cover generation state (one slot per frame, like HomeActivity carousel)
  struct CoverGenState {
    bool active = false;   // cover generation loop is running
    bool pending = false;  // start generation on next frame (after grid is visible)
    int  slot = 0;         // current slot being processed (0..gridsPerPage_-1)
    int  done = 0;         // number of covers successfully generated
    int  total = 0;        // total missing covers on this page
    int  pageStart = -1;   // page start index when generation began
  };
  CoverGenState coverGen_;

  enum class PopupMode { None, Sort, Filter };
  PopupMode popupMode_ = PopupMode::None;
  LibraryPopupOverlay popupOverlay_;

  int popupSpawnButton_ = -1;
  bool launchFromApps = false;

  long_press::Button upPress_;
  long_press::Button downPress_;
  long_press::Button leftPress_;
  long_press::Button rightPress_;

  void applyLayoutFromSettings();
  void ensureLayoutUpToDate();
  void scanSd();
  void refreshPageCache();  // re-fetches current page from LibraryIndex
  void applyFilterAndSort();
  bool isBookCoverReady(const std::string& path) const;
  void drawTileContent(int i, int x, int y) const;
  void deleteLibraryCovers(const std::string& bookPath);
  void deleteBookFile(const std::string& bookPath);
  void deletePageCovers();
  void deleteAllLibraryCovers();
  void reloadPageCovers();
  bool generatePageCover(const std::string& path);
  void rebuildForFilter(CrossPointSettings::LIBRARY_FILTER filter);

  void openSortPopup();
  void openFilterPopup();
  void closePopup();
  void selectPopupItem();
  void beginTextSearch();

  // Rebuild cachedInfo_ when the input key (selector, page, filter, sort,
  // search, collection) changes. Returns true if the cache was rebuilt.
  // Used by both the partial and full render paths so the visible info
  // line stays consistent across them.
  bool rebuildInfoCacheIfChanged(int curPageRaw, int total);

  // Update only cachedSelTitle_ / cachedSelAuthor_ for the current
  // selectorIndex_, truncating the title to fit the screen. Used by the
  // render path.
  void refreshSelectedTitleAuthor(int selectorIndex, int total, int pageWidth);

 public:
  // When true, the next LibraryActivity launch will force an SD scan even
  // in manual update mode. Set by LibraryContextMenuActivity ("Update & Open").
  static bool forceScanOnNextOpen_;

  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool launchFromApps = false)
      : Activity("Library", renderer, mappedInput), launchFromApps(launchFromApps) {}
  void onEnter() override;
  void loop() override;
  void onExit() override;
  void freeBackgroundMemory() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
