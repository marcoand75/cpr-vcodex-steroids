#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "../Activity.h"
#include "util/MarkdownReader.h"
#include "util/ScreenshotInfo.h"

/**
 * Reader for cached Wikipedia articles (per-article cache folders containing
 * article.md + index.bin + progress.bin).
 *
 * Uses the same reading/rendering pipeline as TxtReaderActivity (markdown
 * span parsing, chunked file reading with span-aware wrapping, page index in
 * RAM + a per-article index.bin cache, two-pass prewarm rendering) but WITHOUT
 * the book-reader side effects: no reading stats, achievements, recent books,
 * progress files, completed-book mover, or orientation handling.
 *
 * The article.md content is always treated as markdown.
 */
class WikiTxtReaderActivity final : public Activity {
 public:
  struct TextLine {
    struct TextSpan {
      std::string text;
      uint8_t style = 0;
    };

    std::string text;
    std::vector<TextSpan> spans;
    uint8_t style = 0;
    uint8_t alignment = CrossPointSettings::LEFT_ALIGN;
    uint8_t indent = 0;
  };

 private:
  // Article cache directory (per-article folder created by WikipediaActivity).
  std::string wikiDir;
  std::string title;
  size_t fileSize = 0;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // Streaming text reader - file offsets for each page.
  std::vector<size_t> pageOffsets;
  std::vector<TextLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation.
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  bool readContent(uint8_t* buffer, size_t offset, size_t length) const;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<TextLine>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  bool saveProgress(int page, size_t offset) const;
  bool loadProgress();
  static bool drawCurrentPageToBuffer(const std::string& wikiDir, GfxRenderer& renderer, MappedInputManager& mappedInput);

 public:
  explicit WikiTxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string wikiDir,
                                 std::string title);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
