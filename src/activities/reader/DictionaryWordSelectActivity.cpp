#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionaryStore.h"
#include "DictionarySuggestionsActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int HIGHLIGHT_PADDING_X = 2;
constexpr int HIGHLIGHT_PADDING_Y = 1;
constexpr int HIGHLIGHT_RADIUS = 3;
}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  extractWords();
  mergeHyphenatedWords();
  if (!rows.empty()) {
    currentRow = std::min<int>(static_cast<int>(rows.size()) / 3, static_cast<int>(rows.size()) - 1);
    currentWordInRow = 0;
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::onExit() {
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

void DictionaryWordSelectActivity::extractWords() {
  words.clear();
  rows.clear();
  if (!page) return;

  prepareReaderFontMetrics();

  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;

    const uint16_t count = block->wordCount();
    for (uint16_t i = 0; i < count; ++i) {
      const char* wordText = block->wordText(i);
      const std::string cleaned = DictionaryStore::cleanWord(wordText);
      if (cleaned.empty()) continue;
      const int16_t x = static_cast<int16_t>(line.xPos + block->wordXpos(i) + marginLeft);
      const int16_t y = static_cast<int16_t>(line.yPos + marginTop);
      const int16_t width = static_cast<int16_t>(std::max(1, measureWordWidth(wordText)));
      words.push_back(WordInfo{wordText, cleaned, x, y, width, 0});
    }
  }

  if (words.empty()) return;
  std::sort(words.begin(), words.end(), [](const WordInfo& a, const WordInfo& b) {
    if (std::abs(a.screenY - b.screenY) > 2) return a.screenY < b.screenY;
    return a.screenX < b.screenX;
  });

  int16_t currentY = words[0].screenY;
  rows.push_back(Row{currentY, {}});
  for (size_t i = 0; i < words.size(); ++i) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back(Row{currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

void DictionaryWordSelectActivity::prepareReaderFontMetrics() {
  if (!page || !renderer.isSdCardFont(readerFontId)) return;

  std::string pageText;
  pageText.reserve(2048);
  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;

    const uint16_t count = block->wordCount();
    for (uint16_t i = 0; i < count; ++i) {
      if (!pageText.empty()) pageText.push_back(' ');
      pageText += block->wordText(i);
    }
  }

  if (!pageText.empty()) {
    renderer.ensureSdCardFontReady(readerFontId, pageText.c_str(), 0x01);
  }
}

int DictionaryWordSelectActivity::measureWordWidth(const char* text) const {
  return renderer.getTextAdvanceX(readerFontId, text, EpdFontFamily::REGULAR);
}

void DictionaryWordSelectActivity::mergeHyphenatedWords() {
  for (size_t rowIndex = 0; rowIndex + 1 < rows.size(); ++rowIndex) {
    if (rows[rowIndex].wordIndices.empty() || rows[rowIndex + 1].wordIndices.empty()) continue;

    const int lastIndex = rows[rowIndex].wordIndices.back();
    const int nextIndex = rows[rowIndex + 1].wordIndices.front();
    const std::string& raw = words[lastIndex].text;
    if (raw.empty()) continue;

    bool hyphenated = raw.back() == '-';
    if (!hyphenated && raw.size() >= 2 && static_cast<unsigned char>(raw[raw.size() - 2]) == 0xC2 &&
        static_cast<unsigned char>(raw[raw.size() - 1]) == 0xAD) {
      hyphenated = true;
    }
    if (!hyphenated) continue;

    std::string first = raw;
    if (!first.empty() && first.back() == '-') {
      first.pop_back();
    } else if (first.size() >= 2) {
      first.erase(first.size() - 2);
    }

    const std::string merged = DictionaryStore::cleanWord(first + words[nextIndex].text);
    if (merged.empty()) continue;
    words[lastIndex].lookupText = merged;
    words[nextIndex].lookupText = merged;
    words[lastIndex].continuationIndex = nextIndex;
    words[nextIndex].continuationOf = lastIndex;
  }
}

void DictionaryWordSelectActivity::moveRow(const int delta) {
  if (rows.empty()) return;
  const int oldWordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int oldCenter = words[oldWordIndex].screenX + words[oldWordIndex].width / 2;

  currentRow = (currentRow + delta + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
  int bestIndex = 0;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(rows[currentRow].wordIndices.size()); ++i) {
    const int wordIndex = rows[currentRow].wordIndices[i];
    const int center = words[wordIndex].screenX + words[wordIndex].width / 2;
    const int distance = std::abs(center - oldCenter);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  currentWordInRow = bestIndex;
  updateSelectionHighlight();
}

void DictionaryWordSelectActivity::moveWord(const int delta) {
  if (rows.empty()) return;
  const int rowCount = static_cast<int>(rows.size());
  const int wordCount = static_cast<int>(rows[currentRow].wordIndices.size());
  if (wordCount <= 0) return;

  if (delta < 0 && currentWordInRow > 0) {
    --currentWordInRow;
  } else if (delta > 0 && currentWordInRow + 1 < wordCount) {
    ++currentWordInRow;
  } else if (delta < 0) {
    currentRow = (currentRow + rowCount - 1) % rowCount;
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  } else {
    currentRow = (currentRow + 1) % rowCount;
    currentWordInRow = 0;
  }
  updateSelectionHighlight();
}

void DictionaryWordSelectActivity::updateSelectionHighlight() {
  // Fast-path: avoid re-rendering the whole page (which re-warms the SD font
  // cache and is very slow). The page is already drawn from the last render();
  // we only need to restore the previously highlighted words to plain and draw
  // the new highlight on top. Returns without re-rendering the page.
  if (!rows.empty() && currentRow >= 0 && currentRow < static_cast<int>(rows.size())) {
    refreshSelectionFast();
    return;
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::drawWordPlain(const int wordIndex) {
  if (wordIndex < 0 || wordIndex >= static_cast<int>(words.size())) return;
  const auto& word = words[wordIndex];
  const int lineHeight = renderer.getLineHeight(readerFontId);
  // Erase the previous highlight (fill the same rounded rect with white) and
  // redraw the word in normal ink.
  renderer.fillRoundedRect(word.screenX - HIGHLIGHT_PADDING_X, word.screenY - HIGHLIGHT_PADDING_Y,
                           word.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2, HIGHLIGHT_RADIUS,
                           Color::White);
  renderer.drawText(readerFontId, word.screenX, word.screenY, word.text.c_str(), true, EpdFontFamily::REGULAR);
}

void DictionaryWordSelectActivity::refreshSelectionFast() {
  RenderLock lock(*this);

  // Restore the previous highlight(s) to plain text.
  for (const int idx : highlightedWordIndices) {
    drawWordPlain(idx);
  }
  highlightedWordIndices.clear();

  // Draw the new highlight.
  if (!rows.empty() && currentRow >= 0 && currentRow < static_cast<int>(rows.size()) && currentWordInRow >= 0 &&
      currentWordInRow < static_cast<int>(rows[currentRow].wordIndices.size())) {
    const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
    const auto& word = words[wordIndex];
    const int lineHeight = renderer.getLineHeight(readerFontId);

    auto drawSelectedWord = [&](const WordInfo& selectedWord, const int idx) {
      renderer.fillRoundedRect(selectedWord.screenX - HIGHLIGHT_PADDING_X, selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                               selectedWord.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2,
                               HIGHLIGHT_RADIUS, Color::Black);
      renderer.drawText(readerFontId, selectedWord.screenX, selectedWord.screenY, selectedWord.text.c_str(), false,
                        EpdFontFamily::REGULAR);
      highlightedWordIndices.push_back(idx);
    };

    drawSelectedWord(word, wordIndex);
    const int linkedIndex = word.continuationOf >= 0 ? word.continuationOf : word.continuationIndex;
    if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
      drawSelectedWord(words[linkedIndex], linkedIndex);
    }
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void DictionaryWordSelectActivity::prewarmCurrentSelectionText() const {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  std::string text = words[wordIndex].text;
  const int linkedIndex = words[wordIndex].continuationOf >= 0 ? words[wordIndex].continuationOf
                                                               : words[wordIndex].continuationIndex;
  if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
    text.push_back(' ');
    text += words[linkedIndex].text;
  }

  if (!text.empty()) {
    fcm->prewarmCache(readerFontId, text.c_str(), 0x01);
  }
}

void DictionaryWordSelectActivity::drawSelectionHighlight() {
  highlightedWordIndices.clear();
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const auto& word = words[wordIndex];
  const int lineHeight = renderer.getLineHeight(readerFontId);

  auto drawSelectedWord = [&](const WordInfo& selectedWord, const int idx) {
    renderer.fillRoundedRect(selectedWord.screenX - HIGHLIGHT_PADDING_X, selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                             selectedWord.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2,
                             HIGHLIGHT_RADIUS, Color::Black);
    renderer.drawText(readerFontId, selectedWord.screenX, selectedWord.screenY, selectedWord.text.c_str(), false,
                      EpdFontFamily::REGULAR);
    highlightedWordIndices.push_back(idx);
  };

  drawSelectedWord(word, wordIndex);

  const int linkedIndex = word.continuationOf >= 0 ? word.continuationOf : word.continuationIndex;
  if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
    drawSelectedWord(words[linkedIndex], linkedIndex);
  }
}

void DictionaryWordSelectActivity::lookupSelectedWord() {
  if (rows.empty()) return;
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const std::string query = words[wordIndex].lookupText.empty() ? DictionaryStore::cleanWord(words[wordIndex].text)
                                                                : words[wordIndex].lookupText;
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  if (query.empty()) {
    GUI.drawPopup(renderer, tr(STR_LOOKUP_EMPTY_PAGE));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(700);
    requestUpdate();
    return;
  }

  Rect popup;
  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_PREPARING));
  }
  if (!DICTIONARIES.prepareActive([this, &popup](int percent) {
        RenderLock lock(*this);
        GUI.fillPopupProgress(renderer, popup, percent);
      })) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_NOT_READY));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(900);
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_LOOKUP));
  }
  const auto lookup = DICTIONARIES.lookup(query, true);
  if (lookup.status == DictionaryLookupResult::Status::Found) {
    startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                               renderer, mappedInput, page, lookup.headword, lookup.definition, lookup.truncated,
                               readerFontId, DICTIONARIES.getDefinitionFontId(readerFontId), marginLeft, marginTop),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               setResult(ActivityResult{});
                               finish();
                               return;
                             }
                             requestUpdate();
                           });
    return;
  }

  if (!lookup.suggestions.empty()) {
    startActivityForResult(std::make_unique<DictionarySuggestionsActivity>(
                               renderer, mappedInput, page, query, lookup.suggestions, readerFontId, marginLeft,
                               marginTop),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               setResult(ActivityResult{});
                               finish();
                               return;
                             }
                             requestUpdate();
                           });
    return;
  }

  GUI.drawPopup(renderer, lookup.status == DictionaryLookupResult::Status::NoDictionary ? tr(STR_DICTIONARY_NONE_SELECTED)
                                                                                        : tr(STR_DEFINITION_NOT_FOUND));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(900);
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lookupSelectedWord();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    moveRow(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    moveRow(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moveWord(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moveWord(1);
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  constexpr bool foregroundBlack = true;
  if (page) {
    // Draw the page directly. drawText() draws real pixels when the font cache
    // is NOT scanning, so we must not wrap page->render() in a PrewarmScope
    // (that would only record text and skip drawing). SD glyphs are loaded
    // on demand here and stay cached for subsequent selection moves, making
    // them fast once the first render warms the cache.
    page->render(renderer, readerFontId, marginLeft, marginTop, foregroundBlack);
  }

  if (rows.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_LOOKUP_EMPTY_PAGE));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sideBackgroundWidth = metrics.sideButtonHintsWidth + 8;
  const int sideBackgroundHeight = 168;
  if (gpio.deviceIsX3()) {
    constexpr int sideY = 151;
    renderer.fillRect(0, sideY, sideBackgroundWidth, sideBackgroundHeight / 2, false);
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth,
                      sideBackgroundHeight / 2, false);
  } else {
    const int sideY = std::min(341, std::max(0, renderer.getScreenHeight() - sideBackgroundHeight - 4));
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth,
                      sideBackgroundHeight, false);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  prewarmCurrentSelectionText();
  drawSelectionHighlight();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
