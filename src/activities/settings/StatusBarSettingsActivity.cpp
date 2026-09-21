#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <cstring>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
// Full menu array (maximum 11 items). On X4, clock items (7-9) are filtered out.
constexpr int FULL_MENU_ITEMS = 11;
const StrId fullMenuNames[FULL_MENU_ITEMS] = {StrId::STR_CHAPTER_PAGE_COUNT,
                                              StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                              StrId::STR_PROGRESS_BAR,
                                              StrId::STR_PROGRESS_BAR_THICKNESS,
                                              StrId::STR_TITLE,
                                              StrId::STR_TIME_LEFT,
                                              StrId::STR_BATTERY,
                                              StrId::STR_CLOCK,
                                              StrId::STR_CLOCK_FORMAT,
                                              StrId::STR_CLOCK_SYNC_NOW,
                                              StrId::STR_XTC_STATUS_BAR};
// Indices of clock-related items in fullMenuNames.
constexpr int CLOCK_POSITION_IDX = 7;
constexpr int CLOCK_FORMAT_IDX = 8;
constexpr int CLOCK_SYNC_IDX = 9;
constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int TITLE_ITEMS = 3;
const StrId titleNames[TITLE_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int CLOCK_POSITION_ITEMS = 3;
const StrId clockPositionNames[CLOCK_POSITION_ITEMS] = {StrId::STR_HIDE, StrId::STR_DIR_RIGHT, StrId::STR_DIR_LEFT};

constexpr int CLOCK_FORMAT_ITEMS = 2;
const StrId clockFormatNames[CLOCK_FORMAT_ITEMS] = {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H};

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
  const int count = self->menuItemCount();
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, count);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, count);
  }
  self->requestUpdate();
}

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  // Build filtered menu: exclude clock items on X4 (no RTC).
  menuNames.clear();
  for (int i = 0; i < FULL_MENU_ITEMS; ++i) {
    if (!halClock.isAvailable()) {
      // Skip clock-related indices on devices without hardware clock.
      if (i == CLOCK_POSITION_IDX || i == CLOCK_FORMAT_IDX || i == CLOCK_SYNC_IDX) {
        continue;
      }
    }
    menuNames.push_back(fullMenuNames[i]);
  }

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
  const StrId selectedItem = menuNames[static_cast<size_t>(selectedIndex)];

  if (selectedItem == StrId::STR_CHAPTER_PAGE_COUNT) {
    SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2;
  } else if (selectedItem == StrId::STR_BOOK_PROGRESS_PERCENTAGE) {
    SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
  } else if (selectedItem == StrId::STR_PROGRESS_BAR) {
    SETTINGS.statusBarProgressBar = (SETTINGS.statusBarProgressBar + 1) % PROGRESS_BAR_ITEMS;
  } else if (selectedItem == StrId::STR_PROGRESS_BAR_THICKNESS) {
    SETTINGS.statusBarProgressBarThickness =
        (SETTINGS.statusBarProgressBarThickness + 1) % PROGRESS_BAR_THICKNESS_ITEMS;
  } else if (selectedItem == StrId::STR_TITLE) {
    SETTINGS.statusBarTitle = (SETTINGS.statusBarTitle + 1) % TITLE_ITEMS;
  } else if (selectedItem == StrId::STR_TIME_LEFT) {
    SETTINGS.statusBarTimeLeft = (SETTINGS.statusBarTimeLeft + 1) % CrossPointSettings::STATUS_BAR_TIME_LEFT_COUNT;
  } else if (selectedItem == StrId::STR_BATTERY) {
    SETTINGS.statusBarBattery = (SETTINGS.statusBarBattery + 1) % 2;
  } else if (selectedItem == StrId::STR_CLOCK) {
    SETTINGS.statusBarClock = (SETTINGS.statusBarClock + 1) % CLOCK_POSITION_ITEMS;
  } else if (selectedItem == StrId::STR_CLOCK_FORMAT) {
    SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % CLOCK_FORMAT_ITEMS;
  } else if (selectedItem == StrId::STR_CLOCK_SYNC_NOW) {
    startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { finish(); });
    return;
  } else if (selectedItem == StrId::STR_XTC_STATUS_BAR) {
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
      renderer, layout, static_cast<int>(menuItemCount()), static_cast<int>(selectedIndex),
      [this](int index) { return std::string(I18N.get(menuNames[static_cast<size_t>(index)])); }, nullptr, nullptr,
      [this](int index) {
        // Draw status for each setting
        const StrId item = menuNames[static_cast<size_t>(index)];
        if (item == StrId::STR_CHAPTER_PAGE_COUNT) {
          return SETTINGS.statusBarChapterPageCount ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (item == StrId::STR_BOOK_PROGRESS_PERCENTAGE) {
          return SETTINGS.statusBarBookProgressPercentage ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (item == StrId::STR_PROGRESS_BAR) {
          return I18N.get(progressBarNames[SETTINGS.statusBarProgressBar]);
        } else if (item == StrId::STR_PROGRESS_BAR_THICKNESS) {
          return I18N.get(progressBarThicknessNames[SETTINGS.statusBarProgressBarThickness]);
        } else if (item == StrId::STR_TITLE) {
          return I18N.get(titleNames[SETTINGS.statusBarTitle]);
        } else if (item == StrId::STR_TIME_LEFT) {
          const StrId timeLeftNames[] = {StrId::STR_HIDE, StrId::STR_CHAPTER, StrId::STR_BOOK,
                                         StrId::STR_SESSION_DURATION, StrId::STR_TODAY_TOTAL};
          return I18N.get(timeLeftNames[SETTINGS.statusBarTimeLeft]);
        } else if (item == StrId::STR_BATTERY) {
          return SETTINGS.statusBarBattery ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (item == StrId::STR_CLOCK) {
          return I18N.get(clockPositionNames[SETTINGS.statusBarClock]);
        } else if (item == StrId::STR_CLOCK_FORMAT) {
          return I18N.get(clockFormatNames[SETTINGS.clockFormat]);
        } else if (item == StrId::STR_CLOCK_SYNC_NOW) {
          return tr(STR_CLOCK_SYNC);
        } else if (item == StrId::STR_XTC_STATUS_BAR) {
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
  } else if (SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_SESSION) {
    timeLeftPreview = "45m";
  } else if (SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_TODAY) {
    timeLeftPreview = "1h 30m";
  }

  GUI.drawStatusBar(renderer, 75, 8, 32, title, verticalPreviewPadding, 0, false, timeLeftPreview);

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                    renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() - verticalPreviewPadding -
                        verticalPreviewTextPadding,
                    tr(STR_PREVIEW));

  renderer.displayBuffer();
}
