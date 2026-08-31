#include "ShortcutLocationActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"

namespace {
const char* getLocationLabel(const ShortcutDefinition& definition) {
  return static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr)) ==
                 CrossPointSettings::SHORTCUT_HOME
             ? tr(STR_HOME_LOCATION)
             : tr(STR_APPS);
}
}  // namespace

void ShortcutLocationActivity::reloadEntries() {
  entries.clear();
  entries.reserve(getShortcutDefinitions().size());
  for (const auto& definition : getShortcutDefinitions()) {
    entries.push_back(&definition);
  }

  std::stable_sort(entries.begin(), entries.end(), [](const ShortcutDefinition* lhs, const ShortcutDefinition* rhs) {
    return getShortcutOrder(*lhs) < getShortcutOrder(*rhs);
  });

  selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(entries.size()));
}

void ShortcutLocationActivity::toggleSelectedEntry() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  auto* definition = entries[selectedIndex];
  auto& location = SETTINGS.*(definition->locationPtr);
  location = location == CrossPointSettings::SHORTCUT_HOME ? CrossPointSettings::SHORTCUT_APPS
                                                           : CrossPointSettings::SHORTCUT_HOME;
  requestUpdate();
}

void ShortcutLocationActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();
  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<ShortcutLocationActivity*>(ctx);
    self->finish();
  }, this, false);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<ShortcutLocationActivity*>(ctx);
    self->toggleSelectedEntry();
  }, this, true);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<ShortcutLocationActivity*>(ctx);
    if (self->entries.empty()) return;
    if (delta > 0) {
      self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
    } else {
      self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
    }
    self->requestUpdate();
  };

  listInputMapper.setNavReleaseAndContinuous(onNav, onNav, this);
}

void ShortcutLocationActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ShortcutLocationActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SHORTCUT_LOCATION));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (entries.empty()) {
    ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return std::string(I18N.get(entries[index]->nameId)); }, nullptr, nullptr,
                 [this](const int index) { return std::string(getLocationLabel(*entries[index])); }, true);
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}