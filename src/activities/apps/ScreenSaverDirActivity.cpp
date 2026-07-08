#include "ScreenSaverDirActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "ScreenSaverPreviewActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "util/SleepImageUtils.h"

void ScreenSaverDirActivity::loadDirectories() {
  directories = SleepImageUtils::listSleepDirectories();
  const int itemCount = static_cast<int>(directories.size()) + 1;
  if (selectedIndex >= itemCount) {
    selectedIndex = std::max(0, itemCount - 1);
  }
}

void ScreenSaverDirActivity::onBack(void* ctx) {
  static_cast<ScreenSaverDirActivity*>(ctx)->finish();
}

void ScreenSaverDirActivity::onConfirm(void* ctx) {
  auto* self = static_cast<ScreenSaverDirActivity*>(ctx);
  if (self->selectedIndex == 0) {
    SETTINGS.screenSaverOrder =
        (SETTINGS.screenSaverOrder + 1) % CrossPointSettings::SCREENSAVER_ORDER_COUNT;
    SETTINGS.saveToFile();
    self->requestUpdate();
  } else {
    self->openSelectedDirectory();
  }
}

void ScreenSaverDirActivity::onNav(void* ctx, int delta) {
  auto* self = static_cast<ScreenSaverDirActivity*>(ctx);
  const int itemCount = static_cast<int>(self->directories.size()) + 1;
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, itemCount);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, itemCount);
  }
  self->requestUpdate();
}

void ScreenSaverDirActivity::onEnter() {
  Activity::onEnter();
  loadDirectories();

  listInputMapper.setBackHandler(onBack, this, false);
  listInputMapper.setConfirmHandler(onConfirm, this, false);
  listInputMapper.setNavPressAndContinuous(onNav, onNav, this);

  requestUpdate();
}

void ScreenSaverDirActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ScreenSaverDirActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer);
  const std::string selectedDirectory = SETTINGS.screenSaverDirectory;
  const char* screenSaverOrderLabel =
      SETTINGS.screenSaverOrder == CrossPointSettings::SCREENSAVER_SHUFFLE ? tr(STR_SHUFFLE) : tr(STR_SEQUENTIAL);

  ListRenderHelper::drawHeader(renderer, tr(STR_SCREENSAVER), nullptr, true);

  if (directories.empty()) {
    ListRenderHelper::drawList(
        renderer, layout, 1, selectedIndex,
        [](int) { return std::string(tr(STR_SCREENSAVER_ORDER)); }, nullptr,
        [](int) { return UIIcon::Recent; },
        [screenSaverOrderLabel](int) { return std::string(screenSaverOrderLabel); },
        true);

    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.contentTop + 56, tr(STR_NO_SLEEP_DIRECTORIES));
  } else {
    ListRenderHelper::drawList(
        renderer, layout, static_cast<int>(directories.size()) + 1, selectedIndex,
        [this](int index) {
          if (index == 0) return std::string(tr(STR_SCREENSAVER_ORDER));
          return SleepImageUtils::getDirectoryLabel(directories[index - 1]);
        },
        nullptr,
        [](int index) { return index == 0 ? UIIcon::Recent : UIIcon::Folder; },
        [&selectedDirectory, screenSaverOrderLabel, this](int index) {
          if (index == 0) {
            return std::string(screenSaverOrderLabel);
          }
          return directories[index - 1] == selectedDirectory ? std::string(tr(STR_SELECTED)) : std::string();
        },
        true);
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), selectedIndex == 0 ? tr(STR_SELECT) : tr(STR_OPEN),
                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}

void ScreenSaverDirActivity::openSelectedDirectory() {
  if (directories.empty() || selectedIndex == 0) {
    return;
  }

  startActivityForResult(std::make_unique<ScreenSaverPreviewActivity>(renderer, mappedInput, directories[selectedIndex - 1]),
                         [this](const ActivityResult&) {
                           loadDirectories();
                           requestUpdate();
                         });
}
