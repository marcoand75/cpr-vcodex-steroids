#include "TimeZoneSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/TimeUtils.h"
#include "util/TimeZoneRegistry.h"

void TimeZoneSelectActivity::onBack(void* ctx) { static_cast<TimeZoneSelectActivity*>(ctx)->finish(); }

void TimeZoneSelectActivity::onConfirm(void* ctx) {
  auto* self = static_cast<TimeZoneSelectActivity*>(ctx);
  {
    RenderLock lock(*self);
    const auto idx = static_cast<uint8_t>(self->selectedIndex);
    SETTINGS.timeZonePreset = TimeZoneRegistry::clampPresetIndex(idx);
    SETTINGS.saveToFile();
    TimeUtils::configureTimezone();
  }
  self->finish();
}

void TimeZoneSelectActivity::onNav(void* ctx, const int delta) {
  auto* self = static_cast<TimeZoneSelectActivity*>(ctx);
  const int totalItems = static_cast<int>(TimeZoneRegistry::getPresetCount());
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, totalItems);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, totalItems);
  }
  self->requestUpdate();
}

void TimeZoneSelectActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);

  listInputMapper.setBackHandler(onBack, this, false);
  listInputMapper.setConfirmHandler(onConfirm, this, false);
  listInputMapper.setNavHandlers(nullptr, onNav, nullptr, this);

  requestUpdate();
}

void TimeZoneSelectActivity::loop() { listInputMapper.loop(mappedInput); }

void TimeZoneSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);
  const int totalItems = static_cast<int>(TimeZoneRegistry::getPresetCount());
  const int currentIndex = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);

  ListRenderHelper::drawHeader(renderer, tr(STR_TIME_ZONE));
  ListRenderHelper::drawList(renderer, layout, totalItems, selectedIndex,
                             [](int index) {
                               return std::string(TimeZoneRegistry::getPresetLabel(static_cast<uint8_t>(index)));
                             },
                             nullptr, nullptr,
                             [currentIndex](int index) {
                               return index == currentIndex ? std::string(tr(STR_SELECTED)) : std::string("");
                             },
                             true);

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
