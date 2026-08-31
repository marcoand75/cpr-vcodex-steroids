#include "FlashcardSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
std::string getStudyModeValue() {
  switch (SETTINGS.flashcardStudyMode) {
    case CrossPointSettings::FLASHCARD_STUDY_DUE:
      return tr(STR_DUE);
    case CrossPointSettings::FLASHCARD_STUDY_INFINITE:
      return tr(STR_RANDOM_PRACTICE);
    case CrossPointSettings::FLASHCARD_STUDY_SEQUENTIAL:
      return tr(STR_SEQUENTIAL);
    case CrossPointSettings::FLASHCARD_STUDY_SCHEDULED:
    default:
      return tr(STR_SCHEDULED);
  }
}

std::string getSessionSizeValue() {
  switch (SETTINGS.flashcardSessionSize) {
    case CrossPointSettings::FLASHCARD_SESSION_10:
      return "10";
    case CrossPointSettings::FLASHCARD_SESSION_20:
      return "20";
    case CrossPointSettings::FLASHCARD_SESSION_30:
      return "30";
    case CrossPointSettings::FLASHCARD_SESSION_50:
      return "50";
    case CrossPointSettings::FLASHCARD_SESSION_ALL:
    default:
      return tr(STR_ALL);
  }
}
}  // namespace

void FlashcardSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<FlashcardSettingsActivity*>(ctx);
    self->finish();
  }, this, true);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<FlashcardSettingsActivity*>(ctx);
    self->toggleSelectedSetting();
    self->requestUpdate(true);
  }, this, true);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<FlashcardSettingsActivity*>(ctx);
    if (delta > 0) {
      self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, self->getSettingCount());
    } else {
      self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, self->getSettingCount());
    }
    self->requestUpdate();
  };

  listInputMapper.setNavReleaseAndContinuous(onNav, onNav, this);
}

void FlashcardSettingsActivity::toggleSelectedSetting() {
  if (selectedIndex == 0) {
    SETTINGS.flashcardStudyMode =
        (SETTINGS.flashcardStudyMode + 1) % static_cast<uint8_t>(CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT);
  } else if (selectedIndex == 1) {
    SETTINGS.flashcardSessionSize =
        (SETTINGS.flashcardSessionSize + 1) % static_cast<uint8_t>(CrossPointSettings::FLASHCARD_SESSION_SIZE_COUNT);
  }
  SETTINGS.saveToFile();
}

void FlashcardSettingsActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void FlashcardSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_SETTINGS_TITLE));

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, getSettingCount(), selectedIndex,
               [](const int index) {
                 if (index == 0) return std::string(tr(STR_STUDY_MODE));
                 return std::string(tr(STR_SESSION_SIZE));
               },
               nullptr, [](const int) { return UIIcon::Settings; },
               [](const int index) {
                 if (index == 0) return getStudyModeValue();
                 return getSessionSizeValue();
               },
               true);

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}