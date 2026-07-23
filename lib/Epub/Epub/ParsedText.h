#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"
#include "../Arena/Arena.h"

class GfxRenderer;

class ParsedText {
  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<bool> wordContinues;      // true = word attaches to previous (no space before it)
  std::vector<bool> wordIsFocusSuffix;  // true = token is the regular tail of a focus bold-prefix split
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool forceParagraphIndents;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  uint8_t guideDotMinGap = 0;  // 0=off, 16=standard, 32=large (pixels)
  bool firstLineIndentPending = true;
  mem::Arena layoutArena_;

  void prepareParagraphIndent(const GfxRenderer& renderer, int fontId);
  std::vector<size_t, mem::ArenaAllocator<size_t>> computeLineBreaks(
      const GfxRenderer& renderer, int fontId, int pageWidth,
      std::vector<uint16_t, mem::ArenaAllocator<uint16_t>>& wordWidths, std::vector<bool>& continuesVec,
      const std::vector<int16_t, mem::ArenaAllocator<int16_t>>& naturalGaps, mem::Arena& layoutArena);
  std::vector<size_t, mem::ArenaAllocator<size_t>> computeHyphenatedLineBreaks(
      const GfxRenderer& renderer, int fontId, int pageWidth,
      std::vector<uint16_t, mem::ArenaAllocator<uint16_t>>& wordWidths, std::vector<bool>& continuesVec,
      mem::Arena& layoutArena);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t, mem::ArenaAllocator<uint16_t>>& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth,
                   const std::vector<uint16_t, mem::ArenaAllocator<uint16_t>>& wordWidths,
                   const std::vector<bool>& continuesVec,
                   const std::vector<size_t, mem::ArenaAllocator<size_t>>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine, const GfxRenderer& renderer,
                   int fontId, const std::vector<int16_t, mem::ArenaAllocator<int16_t>>& naturalGaps,
                   mem::Arena& layoutArena);
  std::vector<uint16_t, mem::ArenaAllocator<uint16_t>> calculateWordWidths(const GfxRenderer& renderer, int fontId,
                                                                           mem::Arena& layoutArena);
  std::vector<int16_t, mem::ArenaAllocator<int16_t>> computeNaturalGaps(
      const GfxRenderer& renderer, int fontId,
      const std::vector<uint16_t, mem::ArenaAllocator<uint16_t>>& wordWidths,
      const std::vector<bool>& continuesVec, mem::Arena& layoutArena);

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool forceParagraphIndents = false,
                       const bool hyphenationEnabled = false, const bool focusReadingEnabled = false,
                       const uint8_t guideDotMinGap = 0,
                       const BlockStyle& blockStyle = BlockStyle())
      : blockStyle(blockStyle),
        extraParagraphSpacing(extraParagraphSpacing),
        forceParagraphIndents(forceParagraphIndents),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        guideDotMinGap(guideDotMinGap) {}
  ~ParsedText() = default;

  bool initLayoutArena(size_t capacity) { return layoutArena_.init(capacity); }
  bool hasLayoutArena() const { return layoutArena_.valid(); }
  void resetLayoutArena() { layoutArena_.reset(); }

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false);
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
