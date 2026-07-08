#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
constexpr int MENU_ITEMS = 8;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_CHAPTER_PAGE_COUNT,
                                     StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                     StrId::STR_PROGRESS_BAR,
                                     StrId::STR_PROGRESS_BAR_THICKNESS,
                                     StrId::STR_TITLE,
                                     StrId::STR_TIME_LEFT,
                                     StrId::STR_BATTERY,
                                     StrId::STR_XTC_STATUS_BAR};
constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int TITLE_ITEMS = 3;
const StrId titleNames[TITLE_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int XTC_STATUS_BAR_ITEMS = 3;
const StrId xtcStatusBarNames[XTC_STATUS_BAR_ITEMS] = {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP};

const int widthMargin = 10;
const int verticalPreviewPadding = 50;
const int verticalPreviewTextPadding = 40;
}  // namespace

static void s_onBack(void* ctx) {
  static_cast<StatusBarSettingsActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<StatusBarSettingsActivity*>(ctx);
  self->handleSelection();
  self->requestUpdate();
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<StatusBarSettingsActivity*>(ctx);
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, MENU_ITEMS);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, MENU_ITEMS);
  }
  self->requestUpdate();
}

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  // Clamp statusBarProgressBar and statusBarTitle in case of corrupt/migrated data
  if (SETTINGS.statusBarProgressBar >= PROGRESS_BAR_ITEMS) {
    SETTINGS.statusBarProgressBar = CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  }

  if (SETTINGS.statusBarTitle >= PROGRESS_BAR_THICKNESS_ITEMS) {
    SETTINGS.statusBarTitle = CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL;
  }

  if (SETTINGS.statusBarTitle >= TITLE_ITEMS) {
    SETTINGS.statusBarTitle = CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE;
  }

  if (SETTINGS.statusBarTimeLeft >= CrossPointSettings::STATUS_BAR_TIME_LEFT_COUNT) {
    SETTINGS.statusBarTimeLeft = CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_HIDE;
  }

  if (SETTINGS.xtcStatusBarMode >= XTC_STATUS_BAR_ITEMS) {
    SETTINGS.xtcStatusBarMode = CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_HIDE;
  }

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavReleaseAndContinuous(s_onNav, s_onNav, this);

  requestUpdate();
}

void StatusBarSettingsActivity::onExit() { Activity::onExit(); }

void StatusBarSettingsActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void StatusBarSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Chapter Page Count
    SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2;
  } else if (selectedIndex == 1) {
    // Book Progress %
    SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
  } else if (selectedIndex == 2) {
    // Progress Bar
    SETTINGS.statusBarProgressBar = (SETTINGS.statusBarProgressBar + 1) % PROGRESS_BAR_ITEMS;
  } else if (selectedIndex == 3) {
    // Progress Bar Thickness
    SETTINGS.statusBarProgressBarThickness =
        (SETTINGS.statusBarProgressBarThickness + 1) % PROGRESS_BAR_THICKNESS_ITEMS;
  } else if (selectedIndex == 4) {
    // Chapter Title
    SETTINGS.statusBarTitle = (SETTINGS.statusBarTitle + 1) % TITLE_ITEMS;
  } else if (selectedIndex == 5) {
    // Time Left
    SETTINGS.statusBarTimeLeft = (SETTINGS.statusBarTimeLeft + 1) % CrossPointSettings::STATUS_BAR_TIME_LEFT_COUNT;
  } else if (selectedIndex == 6) {
    // Show Battery
    SETTINGS.statusBarBattery = (SETTINGS.statusBarBattery + 1) % 2;
  } else if (selectedIndex == 7) {
    // XTC Status Bar
    SETTINGS.xtcStatusBarMode = (SETTINGS.xtcStatusBarMode + 1) % XTC_STATUS_BAR_ITEMS;
  }
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, false, metrics.verticalSpacing);

  ListRenderHelper::drawHeader(renderer, tr(STR_CUSTOMISE_STATUS_BAR));

  ListRenderHelper::drawList(
      renderer, layout, static_cast<int>(MENU_ITEMS), static_cast<int>(selectedIndex),
      [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr, nullptr,
      [this](int index) {
        // Draw status for each setting
        if (index == 0) {
          return SETTINGS.statusBarChapterPageCount ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == 1) {
          return SETTINGS.statusBarBookProgressPercentage ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == 2) {
          return I18N.get(progressBarNames[SETTINGS.statusBarProgressBar]);
        } else if (index == 3) {
          return I18N.get(progressBarThicknessNames[SETTINGS.statusBarProgressBarThickness]);
        } else if (index == 4) {
          return I18N.get(titleNames[SETTINGS.statusBarTitle]);
        } else if (index == 5) {
          const StrId timeLeftNames[] = {StrId::STR_HIDE, StrId::STR_CHAPTER, StrId::STR_BOOK};
          return I18N.get(timeLeftNames[SETTINGS.statusBarTimeLeft]);
        } else if (index == 6) {
          return SETTINGS.statusBarBattery ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == 7) {
          return I18N.get(xtcStatusBarNames[SETTINGS.xtcStatusBarMode]);
        } else {
          return tr(STR_HIDE);
        }
      },
      true);

  // Draw button hints
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  std::string title;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = tr(STR_EXAMPLE_BOOK);
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_EXAMPLE_CHAPTER);
  }

  // Hardcoded preview time-left examples (CrossInk-style)
  const char* timeLeftPreview = nullptr;
  if (SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_CHAPTER) {
    timeLeftPreview = "1h 20m";
  } else if (SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_BOOK) {
    timeLeftPreview = "3h 40m";
  }

  GUI.drawStatusBar(renderer, 75, 8, 32, title, verticalPreviewPadding, 0, false, timeLeftPreview);

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                    renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() - verticalPreviewPadding -
                        verticalPreviewTextPadding,
                    tr(STR_PREVIEW));

  renderer.displayBuffer();
}
