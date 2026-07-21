#pragma once
#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"

class FontCacheManager;

// Represents a line of text on a page
class TextBlock final : public Block {
 private:
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  // Per-word focus boundary: N > 0 means the first N bytes of words[i] are rendered bold,
  // the remainder in the base style. 0 means no split (whole word uses wordStyles[i]).
  std::vector<uint8_t> wordFocusBoundary;
  // Pre-computed pixel offset from word start to the regular suffix, stored when boundary > 0.
  std::vector<uint16_t> wordFocusSuffixX;
  // Per-word Guide Dots X offset: 0 means no dot for this word.
  // Non-zero value indicates the X position relative to word start where a Guide Dot is drawn.
  // Vector is empty when guide dots are disabled for the entire block.
  std::vector<uint16_t> wordGuideDotXOffset;
  // Per-word flags: bit 0 = black background, bit 1 = layout-inserted hyphen
  // Vector is empty when no words have flags set.
  std::vector<uint8_t> wordFlags;
  BlockStyle blockStyle;

 public:
  static constexpr uint8_t WORD_FLAG_BACKGROUND_BLACK = 0x01;
  static constexpr uint8_t WORD_FLAG_INSERTED_HYPHEN = 0x02;

  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, std::vector<uint8_t> focus_boundary,
                     std::vector<uint16_t> focus_suffix_x, std::vector<uint16_t> guide_dot_x_offset = {},
                     std::vector<uint8_t> word_flags = {}, const BlockStyle& blockStyle = BlockStyle())
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        wordFocusBoundary(std::move(focus_boundary)),
        wordFocusSuffixX(std::move(focus_suffix_x)),
        wordGuideDotXOffset(std::move(guide_dot_x_offset)),
        wordFlags(std::move(word_flags)),
        blockStyle(blockStyle) {}
  ~TextBlock() override = default;
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  const std::vector<std::string>& getWords() const { return words; }
  const std::vector<int16_t>& getWordXpos() const { return wordXpos; }
  bool hasGuideDots() const { return !wordGuideDotXOffset.empty(); }
  const std::vector<uint16_t>& getWordGuideDotXOffset() const { return wordGuideDotXOffset; }
  bool hasWordFlags() const { return !wordFlags.empty(); }
  const std::vector<uint8_t>& getWordFlags() const { return wordFlags; }
  bool isEmpty() override { return words.empty(); }
  size_t wordCount() const { return words.size(); }
  bool wordEndsWithInsertedHyphen(size_t i) const {
    return i < wordFlags.size() && (wordFlags[i] & WORD_FLAG_INSERTED_HYPHEN) != 0;
  }
  void recordFontUsage(FontCacheManager& fontCacheManager, int fontId, uint8_t bionicReadingMode = 0) const;
  void render(const GfxRenderer& renderer, int fontId, int x, int y, uint8_t bionicReadingMode = 0) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
