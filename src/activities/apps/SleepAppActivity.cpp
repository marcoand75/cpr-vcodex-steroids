#include "SleepAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "SleepPreviewActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "util/SleepImageUtils.h"

void SleepAppActivity::loadDirectories() {
  directories = SleepImageUtils::listSleepDirectories();
  const int itemCount = static_cast<int>(directories.size()) + 1;
  if (selectedIndex >= itemCount) {
    selectedIndex = std::max(0, itemCount - 1);
  }
}

void SleepAppActivity::onBack(void* ctx) {
  static_cast<SleepAppActivity*>(ctx)->finish();
}

void SleepAppActivity::onConfirm(void* ctx) {
  auto* self = static_cast<SleepAppActivity*>(ctx);
  if (self->selectedIndex == 0) {
    SETTINGS.sleepImageOrder =
        (SETTINGS.sleepImageOrder + 1) % CrossPointSettings::SLEEP_IMAGE_ORDER_COUNT;
    SETTINGS.saveToFile();
    self->requestUpdate();
  } else {
    self->openSelectedDirectory();
  }
}

void SleepAppActivity::onNav(void* ctx, int delta) {
  auto* self = static_cast<SleepAppActivity*>(ctx);
  const int itemCount = static_cast<int>(self->directories.size()) + 1;
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, itemCount);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, itemCount);
  }
  self->requestUpdate();
}

void SleepAppActivity::onEnter() {
  Activity::onEnter();
  loadDirectories();

  listInputMapper.setBackHandler(onBack, this, false);
  listInputMapper.setConfirmHandler(onConfirm, this, false);
  listInputMapper.setNavPressAndContinuous(onNav, onNav, this);

  requestUpdate();
}

void SleepAppActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void SleepAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer);
  const std::string selectedDirectory = SleepImageUtils::resolveConfiguredSleepDirectory();
  const char* sleepOrderLabel =
      SETTINGS.sleepImageOrder == CrossPointSettings::SLEEP_IMAGE_SHUFFLE ? tr(STR_SHUFFLE) : tr(STR_SEQUENTIAL);

  ListRenderHelper::drawHeader(renderer, tr(STR_SLEEP), nullptr, true);

  if (directories.empty()) {
    ListRenderHelper::drawList(
        renderer, layout, 1, selectedIndex,
        [](int) { return std::string(tr(STR_SLEEP_ORDER)); }, nullptr,
        [](int) { return UIIcon::Recent; },
        [sleepOrderLabel](int) { return std::string(sleepOrderLabel); },
        true);

    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.contentTop + 56, tr(STR_NO_SLEEP_DIRECTORIES));
  } else {
    ListRenderHelper::drawList(
        renderer, layout, static_cast<int>(directories.size()) + 1, selectedIndex,
        [this](int index) {
          if (index == 0) return std::string(tr(STR_SLEEP_ORDER));
          return SleepImageUtils::getDirectoryLabel(directories[index - 1]);
        },
        nullptr,
        [](int index) { return index == 0 ? UIIcon::Recent : UIIcon::Folder; },
        [&selectedDirectory, sleepOrderLabel, this](int index) {
          if (index == 0) {
            return std::string(sleepOrderLabel);
          }
          return directories[index - 1] == selectedDirectory ? std::string(tr(STR_SELECTED)) : std::string();
        },
        true);
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), selectedIndex == 0 ? tr(STR_SELECT) : tr(STR_OPEN),
                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}

void SleepAppActivity::openSelectedDirectory() {
  if (directories.empty() || selectedIndex == 0) {
    return;
  }

  startActivityForResult(std::make_unique<SleepPreviewActivity>(renderer, mappedInput, directories[selectedIndex - 1]),
                         [this](const ActivityResult&) {
                           loadDirectories();
                           requestUpdate();
                         });
}
