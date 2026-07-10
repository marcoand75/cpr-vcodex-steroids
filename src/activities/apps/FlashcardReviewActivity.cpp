#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
struct WrappedCardBody {
  int fontId = UI_10_FONT_ID;
  int lineHeight = 0;
  int visibleLineCount = 1;
  std::vector<std::string> lines;
  bool fits = true;
};

constexpr size_t MAX_FLASHCARD_RENDER_TEXT_BYTES = 4096;
constexpr int MAX_FLASHCARD_WRAPPED_LINES = 256;

bool hasValue(const std::vector<std::string>& values, const std::string& key) {
  return std::find(values.begin(), values.end(), key) != values.end();
}

size_t utf8SafePrefixLength(const std::string& text, size_t limit) {
  if (limit >= text.size()) {
    return text.size();
  }

  while (limit > 0 && (static_cast<unsigned char>(text[limit]) & 0xC0U) == 0x80U) {
    --limit;
  }
  return limit;
}

std::string clippedCardTextForRender(const std::string& text) {
  const size_t limit = utf8SafePrefixLength(text, MAX_FLASHCARD_RENDER_TEXT_BYTES);
  std::string clipped = text.substr(0, limit);
  clipped += "...";
  return clipped;
}

void drawReviewScrollHints(GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth,
                           const bool canScrollUp, const bool canScrollDown) {
  const int headerY = metrics.topPadding + 5;
  const char* upLabel = tr(STR_DIR_UP);
  const char* downLabel = tr(STR_DIR_DOWN);
  const int labelWidth =
      std::max(renderer.getTextWidth(SMALL_FONT_ID, upLabel), renderer.getTextWidth(SMALL_FONT_ID, downLabel));
  const int boxHeight = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int boxPaddingX = 6;
  const int boxWidth = labelWidth + boxPaddingX * 2;
  const int hintGap = 22;
  const int downBoxX = pageWidth - 210 - boxWidth;
  const int upBoxX = downBoxX - hintGap - boxWidth;
  const int boxY = headerY - 2;

  const auto drawHint = [&](const int boxX, const char* label) {
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, boxX + (boxWidth - textWidth) / 2, headerY, label);
  };

  if (canScrollUp) {
    drawHint(upBoxX, upLabel);
  }
  if (canScrollDown) {
    drawHint(downBoxX, downLabel);
  }
}

std::vector<std::string> wrapCardBody(GfxRenderer& renderer, const int fontId, const std::string& text, const int width,
                                      const int maxLines, const EpdFontFamily::Style style) {
  std::vector<std::string> result;
  if (text.empty() || maxLines <= 0) {
    return result;
  }
  result.reserve(static_cast<size_t>(maxLines));

  size_t start = 0;
  while (start <= text.size() && static_cast<int>(result.size()) < maxLines) {
    const size_t end = text.find('\n', start);
    const std::string segment = end == std::string::npos ? text.substr(start) : text.substr(start, end - start);

    if (segment.empty()) {
      result.emplace_back("");
    } else {
      const int remainingLines = maxLines - static_cast<int>(result.size());
      auto wrapped = renderer.wrappedText(fontId, segment.c_str(), width, remainingLines, style);
      result.insert(result.end(), wrapped.begin(), wrapped.end());
    }

    if (end == std::string::npos) {
      break;
    }

    start = end + 1;
  }

  if (static_cast<int>(result.size()) > maxLines) {
    result.resize(maxLines);
  }

  return result;
}

int builtInReaderFontId(uint8_t family, uint8_t size) {
  switch (family) {
    case CrossPointSettings::NOTOSANS:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return NOTOSANS_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return NOTOSANS_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return NOTOSANS_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
      }
    case CrossPointSettings::LEXEND:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return LEXEND_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return LEXEND_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return LEXEND_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return LEXEND_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return LEXEND_14_FONT_ID;
      }
    case CrossPointSettings::BOOKERLY:
    default:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return BOOKERLY_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return BOOKERLY_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return BOOKERLY_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return BOOKERLY_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return BOOKERLY_14_FONT_ID;
      }
  }
}

std::vector<int> flashcardFontCandidates(int preferredFontId) {
  std::vector<int> candidates;
  const auto addUnique = [&candidates](const int fontId) {
    if (fontId != 0 && std::find(candidates.begin(), candidates.end(), fontId) == candidates.end()) {
      candidates.push_back(fontId);
    }
  };

  addUnique(preferredFontId);
  const int startSize =
      std::clamp<int>(SETTINGS.fontSize, CrossPointSettings::X_SMALL, CrossPointSettings::EXTRA_LARGE);
  for (int size = startSize; size >= CrossPointSettings::X_SMALL; --size) {
    addUnique(builtInReaderFontId(SETTINGS.fontFamily, static_cast<uint8_t>(size)));
  }
  addUnique(UI_12_FONT_ID);
  addUnique(UI_10_FONT_ID);
  addUnique(SMALL_FONT_ID);
  return candidates;
}

WrappedCardBody fitCardBody(GfxRenderer& renderer, const int preferredFontId, const std::string& text, const int width,
                            const int height, const EpdFontFamily::Style style) {
  WrappedCardBody fallback;

  for (const int fontId : flashcardFontCandidates(preferredFontId)) {
    const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
    const int maxLines = std::max(1, height / lineHeight);
    auto lines = wrapCardBody(renderer, fontId, text, width, maxLines + 1, style);
    const bool fits = static_cast<int>(lines.size()) <= maxLines;
    if (fits) {
      return WrappedCardBody{fontId, lineHeight, maxLines, std::move(lines), true};
    }

    fallback = WrappedCardBody{fontId, lineHeight, maxLines, {}, false};
  }

  auto lines = wrapCardBody(renderer, fallback.fontId, text, width, MAX_FLASHCARD_WRAPPED_LINES + 1, style);
  if (static_cast<int>(lines.size()) > MAX_FLASHCARD_WRAPPED_LINES) {
    lines.resize(MAX_FLASHCARD_WRAPPED_LINES);
    if (!lines.empty()) {
      const std::string truncatedLine = lines.back() + " ...";
      lines.back() = renderer.truncatedText(fallback.fontId, truncatedLine.c_str(), width, style);
    }
  }
  fallback.lines = std::move(lines);

  return fallback;
}
}  // namespace

void FlashcardReviewActivity::loadDeckData() {
  invalidateCardLayout();
  errorMessage.clear();
  if (!FLASHCARDS.loadDeck(deckPath, deck, &errorMessage)) {
    loaded = false;
    return;
  }

  if (!FLASHCARDS.loadDeckProgress(deck, progress, &errorMessage)) {
    loaded = false;
    deck.cards.clear();
    return;
  }
  const FlashcardDeckMetrics metrics = FLASHCARDS.buildMetrics(deck, progress);
  FLASHCARDS.registerDeckOpened(deck, metrics);
  queue = FLASHCARDS.buildSessionQueue(deck, progress);
  initialSessionSize = static_cast<int>(queue.size());
  activeCard = {};
  activeCardIndex = -1;
  activeCardLoaded = false;
  loaded = true;
}

bool FlashcardReviewActivity::isCurrentCardUnseen() const {
  if (queueIndex >= queue.size()) {
    return false;
  }
  const int index = queue[queueIndex];
  return index >= 0 && index < static_cast<int>(progress.size()) && progress[index].seenCount == 0;
}

FlashcardCardProgress& FlashcardReviewActivity::currentProgress() { return progress[queue[queueIndex]]; }

bool FlashcardReviewActivity::ensureCurrentCardLoaded() {
  if (queueIndex >= queue.size()) {
    return false;
  }

  const int cardIndex = queue[queueIndex];
  if (activeCardLoaded && activeCardIndex == cardIndex) {
    return true;
  }

  if (!FLASHCARDS.loadDeckCard(deck, cardIndex, activeCard, &errorMessage)) {
    activeCard = {};
    activeCardIndex = -1;
    activeCardLoaded = false;
    loaded = false;
    return false;
  }

  activeCardIndex = cardIndex;
  activeCardLoaded = true;
  invalidateCardLayout();
  return true;
}

const FlashcardCard& FlashcardReviewActivity::currentCard() const { return activeCard; }

void FlashcardReviewActivity::invalidateCardLayout() {
  cardLayoutValid = false;
  wrappedLines.clear();
  wrappedFontId = 0;
  wrappedLineHeight = 0;
  visibleLineCount = 1;
  scrollLineOffset = 0;
  maxScrollLineOffset = 0;
}

void FlashcardReviewActivity::scrollCard(const int direction) {
  const int pageStep = std::max(1, visibleLineCount - 1);
  const int nextOffset = std::clamp(scrollLineOffset + direction * pageStep, 0, maxScrollLineOffset);
  if (nextOffset != scrollLineOffset) {
    scrollLineOffset = nextOffset;
    requestUpdate();
  }
}

void FlashcardReviewActivity::goToNextCard() {
  showBack = false;
  activeCard = {};
  activeCardIndex = -1;
  activeCardLoaded = false;
  invalidateCardLayout();
  const int sessionHandled = sessionReviewed + sessionSkipped;
  if (SETTINGS.flashcardStudyMode != CrossPointSettings::FLASHCARD_STUDY_INFINITE && initialSessionSize > 0 &&
      sessionHandled >= initialSessionSize) {
    finishWithSummary();
    return;
  }
  queueIndex++;
  if (queueIndex >= queue.size()) {
    if (SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE) {
      queue = FLASHCARDS.buildSessionQueue(deck, progress);
      queueIndex = 0;
      if (queue.empty()) {
        finishWithSummary();
        return;
      }
      requestUpdate();
      return;
    }
    finishWithSummary();
    return;
  }
  requestUpdate();
}

void FlashcardReviewActivity::markCurrentSuccess() {
  auto& item = currentProgress();
  if (isCurrentCardUnseen() && !hasValue(newlySeenKeys, item.key)) {
    newlySeenKeys.push_back(item.key);
    sessionNewSeen++;
  }
  FLASHCARDS.markCardSuccess(item);
  sessionReviewed++;
  sessionCorrect++;
  goToNextCard();
}

void FlashcardReviewActivity::markCurrentFailure() {
  const int cardIndex = queue[queueIndex];
  auto& item = currentProgress();
  if (isCurrentCardUnseen() && !hasValue(newlySeenKeys, item.key)) {
    newlySeenKeys.push_back(item.key);
    sessionNewSeen++;
  }
  FLASHCARDS.markCardFailure(item);
  queue.push_back(cardIndex);
  sessionReviewed++;
  sessionFailed++;
  goToNextCard();
}

void FlashcardReviewActivity::finishWithSummary() {
  if (loaded) {
    FLASHCARDS.saveDeckProgress(deck, progress);
    const FlashcardDeckMetrics metrics = FLASHCARDS.buildMetrics(deck, progress);
    FLASHCARDS.registerSession(deck, metrics);

    FlashcardSessionResult result;
    result.deckId = deck.deckId;
    result.deckPath = deck.path;
    result.deckTitle = deck.title;
    result.reviewed = sessionReviewed;
    result.correct = sessionCorrect;
    result.failed = sessionFailed;
    result.skipped = sessionSkipped;
    result.newSeen = sessionNewSeen;
    result.totalCards = metrics.totalCards;
    result.seenCards = metrics.seenCards;
    result.unseenCards = metrics.unseenCards;
    result.dueCards = metrics.dueCards;
    result.masteredCards = metrics.masteredCards;
    result.successRatePercent = metrics.successRatePercent;
    result.sessionCount = metrics.sessionCount + 1;
    result.dueRemaining = metrics.dueCards;
    setResult(ActivityResult{result});
  } else {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
  }
  finish();
}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  renderer.requestNextFullRefresh();
  orientationApplied = true;
  loadDeckData();
  requestUpdate(true);
}

void FlashcardReviewActivity::onExit() {
  if (orientationApplied) {
    renderer.setOrientation(originalOrientation);
    renderer.requestNextFullRefresh();
  }
  Activity::onExit();
}

void FlashcardReviewActivity::loop() {
  if (!loaded) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  if (queue.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finishWithSummary();
    }
    return;
  }

  const bool backReleased = mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (backReleased) {
    finishWithSummary();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    scrollCard(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    scrollCard(1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showBack = !showBack;
    invalidateCardLayout();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    markCurrentSuccess();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    markCurrentFailure();
    return;
  }
}

void FlashcardReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (!loaded) {
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_OPEN));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10,
                              errorMessage.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : errorMessage.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, tr(STR_BACK));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  if (queue.empty()) {
    HeaderDateUtils::drawHeaderWithDate(renderer, deck.title.c_str(), tr(STR_NO_DUE_CARDS));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_NO_DUE_CARDS));
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, tr(STR_BACK));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  if (!ensureCurrentCardLoaded()) {
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_OPEN));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10,
                              errorMessage.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : errorMessage.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, tr(STR_BACK));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  const bool showSessionProgress = SETTINGS.flashcardStudyMode != CrossPointSettings::FLASHCARD_STUDY_INFINITE;
  const int sessionHandled = sessionReviewed + sessionSkipped;
  const int completedCards = std::clamp(sessionHandled, 0, initialSessionSize);
  const std::string headerSubtitle = showSessionProgress && initialSessionSize > 0
                                         ? std::to_string(completedCards) + "/" + std::to_string(initialSessionSize)
                                         : std::string();
  const std::string headerTitle =
      showSessionProgress && !headerSubtitle.empty() ? deck.title + " / " + headerSubtitle : deck.title;
  HeaderDateUtils::drawHeaderWithDate(renderer, headerTitle.c_str(), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sideHintsReserve = metrics.sideButtonHintsWidth + metrics.verticalSpacing + 8;
  const int cardX = metrics.contentSidePadding;
  const int cardY = contentTop;
  const int cardWidth = pageWidth - metrics.contentSidePadding * 2 - sideHintsReserve;
  const int cardHeight = contentBottom - contentTop;
  const int readerFontId = SETTINGS.getReaderFontId();

  renderer.drawRect(cardX, cardY, cardWidth, cardHeight);

  const std::string sideLabel = showBack ? tr(STR_CARD_BACK) : tr(STR_CARD_FRONT);
  renderer.drawText(SMALL_FONT_ID, cardX + 10, cardY + 10, sideLabel.c_str(), true, EpdFontFamily::BOLD);

  const int textWidth = cardWidth - 24;
  const int textTop = cardY + 34;
  const int textHeight = cardHeight - 50;
  const EpdFontFamily::Style bodyStyle = showBack ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  if (!cardLayoutValid) {
    const std::string& sourceText = showBack ? currentCard().back : currentCard().front;
    std::string clippedText;
    const std::string* bodyText = &sourceText;
    if (sourceText.size() > MAX_FLASHCARD_RENDER_TEXT_BYTES) {
      clippedText = clippedCardTextForRender(sourceText);
      bodyText = &clippedText;
    }

    auto fitted = fitCardBody(renderer, readerFontId, *bodyText, textWidth, textHeight, bodyStyle);
    wrappedFontId = fitted.fontId;
    wrappedLineHeight = fitted.lineHeight;
    visibleLineCount = fitted.visibleLineCount;
    wrappedLines = std::move(fitted.lines);
    maxScrollLineOffset = std::max(0, static_cast<int>(wrappedLines.size()) - std::max(1, visibleLineCount));
    scrollLineOffset = std::clamp(scrollLineOffset, 0, maxScrollLineOffset);
    cardLayoutValid = true;
  }

  const int firstLine = std::clamp(scrollLineOffset, 0, maxScrollLineOffset);
  const int endLine = std::min(static_cast<int>(wrappedLines.size()), firstLine + std::max(1, visibleLineCount));
  int textY = textTop;
  if (maxScrollLineOffset == 0) {
    textY += std::max(0, (textHeight - static_cast<int>(wrappedLines.size()) * wrappedLineHeight) / 2);
  }
  for (int lineIndex = firstLine; lineIndex < endLine; ++lineIndex) {
    renderer.drawText(wrappedFontId, cardX + 12, textY, wrappedLines[lineIndex].c_str(), true, bodyStyle);
    textY += wrappedLineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_FLIP), tr(STR_SUCCESS), tr(STR_FAIL));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  drawReviewScrollHints(renderer, metrics, pageWidth, scrollLineOffset > 0, scrollLineOffset < maxScrollLineOffset);

  renderer.displayBuffer();
}
