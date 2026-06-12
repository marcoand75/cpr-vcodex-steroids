#pragma once

#include <Epub/Page.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"

class DictionaryWordSelectActivity final : public Activity {
 public:
  DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               int readerFontId, int marginLeft, int marginTop)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct WordInfo {
    std::string text;
    std::string lookupText;
    int16_t screenX = 0;
    int16_t screenY = 0;
    int16_t width = 0;
    int16_t row = 0;
    int continuationIndex = -1;
    int continuationOf = -1;
  };

  struct Row {
    int16_t y = 0;
    std::vector<int> wordIndices;
  };

  std::shared_ptr<Page> page;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  std::vector<WordInfo> words;
  std::vector<Row> rows;
  int currentRow = 0;
  int currentWordInRow = 0;
  bool baseScreenBufferStored = false;
  uint8_t* baseScreenBuffer = nullptr;
  size_t baseScreenBufferSize = 0;

  void extractWords();
  void mergeHyphenatedWords();
  void moveRow(int delta);
  void moveWord(int delta);
  void lookupSelectedWord();
  void updateSelectionHighlight();
  bool redrawSelectionFast();
  bool storeBaseScreenBuffer();
  bool restoreBaseScreenBuffer();
  void invalidateBaseScreenBuffer();
  void freeBaseScreenBuffer();
  void drawSelectionHighlight();
};
