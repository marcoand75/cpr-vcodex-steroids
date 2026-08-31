#include "ReaderMenuOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "fontIds.h"

namespace {
std::string getEntryTitle(const ReaderMenuItemDefinition* entry) {
  return std::string(I18N.get(entry->nameId));
}
}  // namespace

static void s_onBack(void* ctx) {
  auto* self = static_cast<ReaderMenuOrderActivity*>(ctx);
  if (self->moveMode) {
    self->moveMode = false;
    self->requestUpdate();
  } else {
    self->finish();
  }
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<ReaderMenuOrderActivity*>(ctx);
  if (!self->entries.empty()) {
    self->moveMode = !self->moveMode;
    self->requestUpdate();
  }
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<ReaderMenuOrderActivity*>(ctx);
  if (self->entries.empty()) {
    return;
  }
  if (self->moveMode) {
    self->moveSelectedEntry(delta);
    return;
  }
  if (delta > 0) {
    self->selectedIndex =
        ButtonNavigator::nextIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
  } else if (delta < 0) {
    self->selectedIndex =
        ButtonNavigator::previousIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
  }
  self->requestUpdate();
}

void ReaderMenuOrderActivity::onEnter() {
  Activity::onEnter();
  reloadEntries();

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavHandlers(nullptr, s_onNav, nullptr, this);

  requestUpdate();
}

void ReaderMenuOrderActivity::onExit() {
  Activity::onExit();
  // Ensure order changes are persisted even if the user exits without
  // explicitly confirming (e.g. Back button).
  SETTINGS.saveToFile();
}

void ReaderMenuOrderActivity::reloadEntries() {
  auto allEntries = getReaderMenuItemsInOrder(SETTINGS);
  entries.clear();
  for (const auto* entry : allEntries) {
    if (!isReaderMenuItemAlwaysVisible(entry->id) && getReaderMenuItemVisibility(entry->id, SETTINGS)) {
      entries.push_back(entry);
    }
  }
  if (entries.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
  }
}

void ReaderMenuOrderActivity::moveSelectedEntry(const int delta) {
  const int targetIndex = selectedIndex + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size()) ||
      targetIndex == selectedIndex) {
    return;
  }

  auto& selectedOrder =
      getReaderMenuItemOrderRef(SETTINGS, entries[selectedIndex]->id);
  auto& targetOrder = getReaderMenuItemOrderRef(SETTINGS, entries[targetIndex]->id);
  std::swap(selectedOrder, targetOrder);
  normalizeReaderMenuOrderSettings(SETTINGS);
  SETTINGS.saveToFile();

  std::swap(entries[selectedIndex], entries[targetIndex]);
  selectedIndex = targetIndex;
  requestUpdate();
}

const char* ReaderMenuOrderActivity::getTitle() const {
  return tr(STR_READER_MENU_ORDER);
}

void ReaderMenuOrderActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ReaderMenuOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);

  ListRenderHelper::drawHeader(renderer, getTitle());

  ListRenderHelper::drawListOrEmpty(renderer, layout, static_cast<int>(entries.size()), selectedIndex,
                                    [this](const int index) { return getEntryTitle(entries[index]); },
                                    tr(STR_NO_ENTRIES));

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK),
                              moveMode ? tr(STR_DONE) : tr(STR_SELECT),
                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}