#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Epub.h"
#include "EpubRenderMode.h"

class Page;
class GfxRenderer;

class Section {
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing,
                              bool forceParagraphIndents, uint8_t paragraphAlignment, uint16_t viewportWidth,
                              uint16_t viewportHeight, bool hyphenationEnabled, bool focusReadingEnabled,
                              bool embeddedStyle, uint8_t imageRendering, bool bionicReadingEnabled,
                              uint8_t guideDotMinGap, uint8_t renderMode);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  // Cumulative per-page word counts for absolute clipping word indices.
  // cumulativeWordCounts[p] = total word count on pages 0..p-1.
  // Built once after section load; size = pageCount + 1.
  std::vector<uint32_t> cumulativeWordCounts;

  explicit Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer,
                   const char* cacheSuffix = "")
      : epub(epub),
        spineIndex(spineIndex),
        renderer(renderer),
        filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + (cacheSuffix ? cacheSuffix : "") +
                 ".bin") {}
  ~Section() = default;
  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                       uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                       bool hyphenationEnabled, bool focusReadingEnabled, bool embeddedStyle, uint8_t imageRendering,
                        bool bionicReadingEnabled = false, uint8_t guideDotMinGap = 0,
                        uint8_t renderMode = 0);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                         uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                         bool hyphenationEnabled, bool focusReadingEnabled, bool embeddedStyle, uint8_t imageRendering,
                         const std::function<void()>& popupFn = nullptr, bool bionicReadingEnabled = false,
                         uint8_t guideDotMinGap = 0, uint8_t renderMode = 0);
  std::unique_ptr<Page> loadPageFromSectionFile();

  // Look up the page number for an anchor id from the section cache file.
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;

  // Look up the page number for a synthetic paragraph index from XPath p[N].
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t pIndex) const;

  // Look up the page number for a running list-item index from a KOReader li XPath.
  std::optional<uint16_t> getPageForListItemIndex(uint16_t liIndex) const;

  // Look up the synthetic paragraph index for the given rendered page.
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const;

  // Look up the XHTML byte offset recorded at the page boundary for the given page.
  std::optional<uint32_t> getXhtmlByteOffsetForPage(uint16_t page) const;

  // Build cumulative word counts array (pageCount + 1 entries).
  // Called once after section load. Must load every page once (I/O cost).
  void buildCumulativeWordCounts();

  // Return the total word count on pages 0..(page-1). Returns 0 if counts not built.
  uint32_t getCumulativeWordOffset(uint16_t page) const;
};
