#include "ShortcutOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
std::string getEntryTitle(const ShortcutOrderEntry& entry) {
  return entry.isAppsHub ? std::string(tr(STR_APPS)) : std::string(I18N.get(entry.definition->nameId));
}
}  // namespace

static void s_onBack(void* ctx) {
  auto* self = static_cast<ShortcutOrderActivity*>(ctx);
  if (self->moveMode) {
    self->moveMode = false;
    self->requestUpdate();
  } else {
    self->finish();
  }
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<ShortcutOrderActivity*>(ctx);
  if (!self->entries.empty()) {
    self->moveMode = !self->moveMode;
    self->requestUpdate();
  }
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<ShortcutOrderActivity*>(ctx);
  if (self->entries.empty()) {
    return;
  }
  if (self->moveMode) {
    self->moveSelectedEntry(delta);
    return;
  }
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
  }
  self->requestUpdate();
}

void ShortcutOrderActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavHandlers(nullptr, s_onNav, nullptr, this);

  requestUpdate();
}

void ShortcutOrderActivity::reloadEntries() {
  entries = getShortcutOrderEntries(group);
  if (entries.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
  }
}

void ShortcutOrderActivity::moveSelectedEntry(const int delta) {
  const int targetIndex = selectedIndex + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size()) || targetIndex == selectedIndex) {
    return;
  }

  auto& selectedOrder = getShortcutOrderRef(SETTINGS, entries[selectedIndex]);
  auto& targetOrder = getShortcutOrderRef(SETTINGS, entries[targetIndex]);
  std::swap(selectedOrder, targetOrder);
  normalizeShortcutOrderSettings(SETTINGS);

  std::swap(entries[selectedIndex], entries[targetIndex]);
  selectedIndex = targetIndex;
  requestUpdate();
}

const char* ShortcutOrderActivity::getTitle() const {
  return group == ShortcutOrderGroup::Home ? tr(STR_ORDER_HOME_SHORTCUTS) : tr(STR_ORDER_APPS_SHORTCUTS);
}

void ShortcutOrderActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ShortcutOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);

  ListRenderHelper::drawHeader(renderer, getTitle());

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, layout.contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(entries.size()), selectedIndex,
                               [this](const int index) { return getEntryTitle(entries[index]); });
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), moveMode ? tr(STR_DONE) : tr(STR_SELECT),
                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
