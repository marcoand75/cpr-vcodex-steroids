#include "ShortcutVisibilityActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"

namespace {
const char* getVisibilityLabel(const ShortcutDefinition& definition) {
  return getShortcutVisibility(definition) ? tr(STR_SHOW) : tr(STR_HIDDEN);
}
}  // namespace

void ShortcutVisibilityActivity::reloadEntries() {
  entries.clear();
  entries.reserve(getShortcutDefinitions().size());
  for (const auto& definition : getShortcutDefinitions()) {
    if (isShortcutAlwaysVisible(definition)) {
      continue;
    }
    entries.push_back(&definition);
  }

  std::stable_sort(entries.begin(), entries.end(), [](const ShortcutDefinition* lhs, const ShortcutDefinition* rhs) {
    return getShortcutOrder(*lhs) < getShortcutOrder(*rhs);
  });

  selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(entries.size()));
}

void ShortcutVisibilityActivity::toggleSelectedEntry() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  auto& visible = getShortcutVisibilityRef(SETTINGS, *entries[selectedIndex]);
  visible = visible == 0 ? 1 : 0;
  requestUpdate();
}

void ShortcutVisibilityActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();
  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<ShortcutVisibilityActivity*>(ctx);
    self->finish();
  }, this, false);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<ShortcutVisibilityActivity*>(ctx);
    self->toggleSelectedEntry();
  }, this, true);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<ShortcutVisibilityActivity*>(ctx);
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

void ShortcutVisibilityActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ShortcutVisibilityActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SHORTCUT_VISIBILITY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (entries.empty()) {
    ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return std::string(I18N.get(entries[index]->nameId)); }, nullptr, nullptr,
                 [this](const int index) { return std::string(getVisibilityLabel(*entries[index])); }, true);
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}