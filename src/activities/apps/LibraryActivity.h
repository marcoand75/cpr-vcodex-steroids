#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "CrossPointSettings.h"
#include "components/LibraryIndex.h"
#include "components/LibraryPopupOverlay.h"

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

  // Cover generation (one per frame, like HomeActivity carousel)
  bool coverGenActive_ = false;          // cover generation loop is running
  bool coverGenPending_ = false;         // start generation on next frame (after grid is visible)
  bool coverGenLock_ = false;            // blocks input during single-cover generation to prevent state corruption
  int  coverGenSlot_ = 0;               // current slot being processed (0..gridsPerPage_-1)
  int  coverGenDone_ = 0;               // number of covers successfully generated
  int  coverGenTotal_ = 0;              // total missing covers on this page

  enum class PopupMode { None, Sort, Filter };
  PopupMode popupMode_ = PopupMode::None;
  LibraryPopupOverlay popupOverlay_;

   bool upHeld_ = false;
   bool upLongTriggered_ = false;
   bool downHeld_ = false;
   bool downLongTriggered_ = false;
   bool leftHeld_ = false;
   bool leftLongTriggered_ = false;
   bool rightHeld_ = false;
   bool rightLongTriggered_ = false;
   int popupSpawnButton_ = -1;
   bool launchFromApps = false;

   static constexpr unsigned long kLongPressMs = 800;

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
