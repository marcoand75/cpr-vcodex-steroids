#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>

#include <optional>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "EpubReaderMenuActivity.h"
#include "ReaderUtils.h"
#include "activities/Activity.h"

class Page;

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingVisibleTextOffset;
  uint32_t pendingClippingAbsoluteStart = UINT32_MAX;
  std::string pendingClippingText;       // text content for text-search fallback
  uint32_t pendingBookmarkAbsoluteStart = UINT32_MAX;
  std::string pendingBookmarkSnippet;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int initialBookmarkSpineIndex = -1;
  int initialBookmarkPage = -1;
  std::optional<uint32_t> initialBookmarkVisibleTextOffset;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  std::string stableBookId;
  BookmarkStore bookmarkStore;
  ClippingStore clippingStore;
  // Per-book reader settings override (reader_settings.bin under the cache dir).
  bool hasPerBookSettingsOverride = false;
  // Last render mode that built this book's section successfully (persisted in
  // reader_settings.bin). 0xFF = unknown. On reopen this mode is tried first so
  // books that fell back to Balanced/Light/Safe Mode don't re-run the failing
  // attempts (and their long, doomed index passes) before hitting the cached
  // section. lastSuccessfulSafeMode marks the text-only Safe Mode (Light +
  // images suppressed), which has its own "_safe" cache suffix.
  uint8_t lastSuccessfulRenderMode = 0xFF;
  bool lastSuccessfulSafeMode = false;
  bool pendingScreenshot = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool pendingForceFullRefresh = false;
  bool statusBarTemporarilyHidden = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;
  bool clippingModeActive = false;
  int clippingStartWordIndex = -1;
  int clippingEndWordIndex = -1;
  int clippingStartRow = -1;
  int clippingEndRow = -1;
  bool clippingStartMarkSet = false;
  bool pendingReadingStatsLoad = false;
  bool pendingReadingStatsLoadDelayed = false;

  struct ClippingWordInfo {
    std::string text;
    int16_t screenX = 0;
    int16_t screenY = 0;
    int16_t width = 0;
    int row = 0;
    int globalIndex = 0;
  };
  std::vector<ClippingWordInfo> clippingWords;
  std::vector<int> clippingRowWordCounts;
  int clippingCurrentRow = 0;
  int clippingCurrentWordInRow = 0;
  int16_t clippingMarginLeft = 0;
  int16_t clippingMarginTop = 0;

  void extractClippingWords(std::shared_ptr<Page> page, int marginLeft, int marginTop);
  void enterClippingMode();
  void exitClippingMode();
  void moveClippingSelection(int deltaRow, int deltaWord);
  void renderClippingSelectionOverlay();
  void renderBookmarkHighlight(std::shared_ptr<Page> page, int marginLeft, int marginTop);
  void renderClippingHighlights(std::shared_ptr<Page> page, int marginLeft, int marginTop);
  void createClippingFromSelection();
  void exportClippingToTextFile(const ClippingStore::Clipping& clipping);
  int sessionStartSpineIndex = 0;
  int sessionStartPage = 0;
  bool sessionProgressTouched = false;
  std::shared_ptr<Page> currentOverlayPageCache;
  int currentOverlayPageSpineIndex = -1;
  int currentOverlayPageNumber = -1;
  int currentOverlayPageMarginLeft = 0;
  int currentOverlayPageMarginTop = 0;

  struct ReaderSettingsSnapshot {
    uint8_t darkMode = 0;
    uint8_t fadingFix = 0;
    uint8_t refreshFrequency = 0;
    uint8_t fontFamily = 0;
    uint8_t fontSize = 0;
    uint8_t lineSpacing = 0;
    uint8_t screenMargin = 0;
    uint8_t paragraphAlignment = 0;
    uint8_t embeddedStyle = 0;
    uint8_t hyphenationEnabled = 0;
    uint8_t bionicReading = 0;
    uint8_t guideReadingEnabled = 0;
    uint8_t dotsSpacing = 0;
    uint8_t epubRenderMode = 0;
    uint8_t orientation = 0;
    uint8_t extraParagraphSpacing = 0;
    uint8_t forceParagraphIndents = 0;
    uint8_t textAntiAliasing = 0;
    uint8_t textDarkness = 0;
    uint8_t readerRefreshMode = 0;
    uint8_t imageRendering = 0;
    std::string sdFontFamilyName;
  };

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  void renderContents(std::shared_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void renderSectionLoadFailure();
  // Inline-image lazy-extraction callback registered with ImageBlock so EPUB
  // images (whose source lives inside the archive, not yet extracted to disk)
  // get pulled out on first render. Registered in onEnter/onExit; uses the
  // reader as context so it can bail out if the EPUB has already been released.
  static bool extractInlineImage(void* context, const char* sourcePath, const char* destinationPath);
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  void saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  ReaderSettingsSnapshot captureReaderSettingsSnapshot() const;
  void applyReaderSettingsChanges(const ReaderSettingsSnapshot& before);
  // Per-book settings (reader_settings.bin): restore a book's saved reader
  // overrides on enter and persist them when the user changes reader settings.
  // Uses the steroids ReaderSettingsSnapshot as the on-disk payload, not the
  // CrossInk v7 struct, since the two settings models differ enough that a
  // byte-compatible v7 record would require porting CrossInk's whole settings
  // surface.
  void loadBookReaderSettings();
  void saveBookReaderSettings();
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void saveCurrentPageBookmark();
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();
  void markCurrentBookAsFinished();
  void pageTurn(bool isForwardTurn);
  void requestCurrentPageFullRefresh();
  void toggleTemporaryStatusBar();
  void cacheCurrentPageForOverlay(const std::shared_ptr<Page>& page, int marginLeft, int marginTop);
   void handleSelectLongPress();
   // Dispatch a BUTTON_ACTION to the appropriate reader action.
   // Called by the loop() when a long-press is detected on any button.
   // prevTriggered/nextTriggered indicate which button was pressed (for directional actions).
   // dir provides the ButtonDirection for directional adjustments (font size, orientation).
   // Returns true if the action was handled (and thus consumed).
   bool handleButtonAction(CrossPointSettings::BUTTON_ACTION action,
                           bool prevTriggered, bool nextTriggered,
                           ReaderUtils::ButtonDirection dir = ReaderUtils::ButtonDirection::BTN_DIR_NEUTRAL);
   void invalidateCurrentOverlayPageCache();
  std::shared_ptr<Page> loadCurrentPageForOverlay(int& outMarginLeft, int& outMarginTop);

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  // KOReader sync — standalone activity launch and result application
  enum class SyncLaunchMode { COMPARE, PULL_REMOTE, PUSH_LOCAL, AUTO_PUSH };
  bool pendingParagraphLookup = false;
  uint16_t pendingParagraphIndex = 0;
  bool pendingListItemLookup = false;
  uint16_t pendingListItemIndex = 0;
  void launchKOReaderSync(SyncLaunchMode mode);
  void applyPendingSyncSession();
  bool tryAutoPushOnClose();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              int initialBookmarkSpineIndex = -1, int initialBookmarkPage = -1,
                              std::optional<uint32_t> initialBookmarkVisibleTextOffset = std::nullopt)
      : Activity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        initialBookmarkSpineIndex(initialBookmarkSpineIndex),
        initialBookmarkPage(initialBookmarkPage),
        initialBookmarkVisibleTextOffset(initialBookmarkVisibleTextOffset) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
