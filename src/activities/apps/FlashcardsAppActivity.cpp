#include "FlashcardsAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FlashcardsStore.h"
#include "FlashcardBrowserActivity.h"
#include "FlashcardRecentsActivity.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardStatsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ACTION_COUNT = 4;

bool hasStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

static std::string getSettingsSubtitle() {
  std::string studyModeLabel;
  switch (SETTINGS.flashcardStudyMode) {
    case CrossPointSettings::FLASHCARD_STUDY_DUE:
      studyModeLabel = tr(STR_DUE);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_INFINITE:
      studyModeLabel = tr(STR_RANDOM);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_SEQUENTIAL:
      studyModeLabel = tr(STR_SEQUENTIAL);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_SCHEDULED:
    default:
      studyModeLabel = tr(STR_SCHEDULED);
      break;
  }

  if (SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE ||
      SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_SEQUENTIAL) {
    return studyModeLabel;
  }

  return studyModeLabel + " | " +
         (SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_ALL
              ? std::string(tr(STR_ALL))
              : std::to_string(SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_10
                                   ? 10
                                   : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_20
                                         ? 20
                                         : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_30
                                               ? 30
                                               : 50));
}
}  // namespace

void FlashcardsAppActivity::refreshCounts() {
  recentCount = static_cast<int>(FLASHCARDS.getRecentDecks().size());
  deckCount = 0;
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasStatsToShow(record)) {
      deckCount++;
    }
  }
  selectedIndex = std::clamp(selectedIndex, 0, ACTION_COUNT - 1);
}

void FlashcardsAppActivity::openSelectedEntry() {
  std::unique_ptr<Activity> activity;
  switch (selectedIndex) {
    case 0:
      activity = std::make_unique<FlashcardBrowserActivity>(renderer, mappedInput);
      break;
    case 1:
      activity = std::make_unique<FlashcardRecentsActivity>(renderer, mappedInput);
      break;
    case 2:
      activity = std::make_unique<FlashcardStatsActivity>(renderer, mappedInput);
      break;
    default:
      activity = std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput);
      break;
  }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    refreshCounts();
    requestUpdate();
  });
}

void FlashcardsAppActivity::onBack(void* ctx) {
  static_cast<FlashcardsAppActivity*>(ctx)->finish();
}

void FlashcardsAppActivity::onConfirm(void* ctx) {
  static_cast<FlashcardsAppActivity*>(ctx)->openSelectedEntry();
}

void FlashcardsAppActivity::releaseNav(void* ctx, int delta) {
  auto* self = static_cast<FlashcardsAppActivity*>(ctx);
  if (delta > 0) self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, ACTION_COUNT);
  else if (delta < 0) self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, ACTION_COUNT);
  self->requestUpdate();
}

void FlashcardsAppActivity::continuousNav(void* ctx, int delta) {
  auto* self = static_cast<FlashcardsAppActivity*>(ctx);
  const int pageItems = UITheme::getNumberOfItemsPerPage(self->renderer, true, false, true, true);
  if (delta > 0) self->selectedIndex = ButtonNavigator::nextPageIndex(self->selectedIndex, ACTION_COUNT, pageItems);
  else if (delta < 0) self->selectedIndex = ButtonNavigator::previousPageIndex(self->selectedIndex, ACTION_COUNT, pageItems);
  self->requestUpdate();
}

void FlashcardsAppActivity::onEnter() {
  Activity::onEnter();
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  refreshCounts();

  listInputMapper.setBackHandler(onBack, this, true);
  listInputMapper.setConfirmHandler(onConfirm, this, true);
  listInputMapper.setNavReleaseAndContinuous(releaseNav, continuousNav, this);

  requestUpdate();
}

void FlashcardsAppActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

void FlashcardsAppActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void FlashcardsAppActivity::render(RenderLock&&) {
  refreshCounts();
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, true, metrics.verticalSpacing);

  ListRenderHelper::drawHeader(renderer, tr(STR_FLASHCARDS), std::to_string(deckCount).c_str(), true);

  ListRenderHelper::drawList(
      renderer, layout, ACTION_COUNT, selectedIndex,
      [](const int index) {
        switch (index) {
          case 0:
            return std::string(tr(STR_OPEN));
          case 1:
            return std::string(tr(STR_RECENTS));
          case 2:
            return std::string(tr(STR_STATISTICS));
          default:
            return std::string(tr(STR_SETTINGS_TITLE));
        }
      },
      [this](const int index) -> std::string {
        switch (index) {
          case 0:
            return tr(STR_FLASHCARDS_OPEN_DESC);
          case 1:
            return std::to_string(recentCount);
          case 2:
            return std::to_string(deckCount);
          default:
            return getSettingsSubtitle();
        }
      },
      [](const int index) {
        switch (index) {
          case 0:
            return UIIcon::Folder;
          case 1:
            return UIIcon::Recent;
          case 2:
            return UIIcon::Library;
          default:
            return UIIcon::Settings;
        }
      });

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
