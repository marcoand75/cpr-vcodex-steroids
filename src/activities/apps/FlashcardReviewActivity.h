#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"

class FlashcardReviewActivity final : public Activity {
  std::string deckPath;
  FlashcardDeck deck;
  std::vector<FlashcardCardProgress> progress;
  std::vector<int> queue;
  FlashcardCard activeCard;
  size_t queueIndex = 0;
  int activeCardIndex = -1;
  int initialSessionSize = 0;
  bool showBack = false;
  bool loaded = false;
  bool activeCardLoaded = false;
  bool cardLayoutValid = false;
  std::vector<std::string> wrappedLines;
  int wrappedFontId = 0;
  int wrappedLineHeight = 0;
  int visibleLineCount = 1;
  int scrollLineOffset = 0;
  int maxScrollLineOffset = 0;
  std::string errorMessage;
  GfxRenderer::Orientation originalOrientation = GfxRenderer::Orientation::Portrait;
  bool orientationApplied = false;

  int sessionReviewed = 0;
  int sessionCorrect = 0;
  int sessionFailed = 0;
  int sessionSkipped = 0;
  int sessionNewSeen = 0;
  std::vector<std::string> newlySeenKeys;

  void loadDeckData();
  void finishWithSummary();
  bool isCurrentCardUnseen() const;
  bool ensureCurrentCardLoaded();
  FlashcardCardProgress& currentProgress();
  const FlashcardCard& currentCard() const;
  void invalidateCardLayout();
  void scrollCard(int direction);
  void goToNextCard();
  void markCurrentSuccess();
  void markCurrentFailure();

 public:
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : Activity("FlashcardReview", renderer, mappedInput), deckPath(std::move(deckPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
