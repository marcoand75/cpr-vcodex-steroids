#include "ReaderMenuOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ReaderMenuOrderActivity::reloadEntries() {
  auto allEntries = getReaderMenuItemsInOrder(SETTINGS);
  entries_.clear();
  for (const auto* entry : allEntries) {
    if (!isReaderMenuItemAlwaysVisible(entry->id) && getReaderMenuItemVisibility(entry->id, SETTINGS)) {
      entries_.push_back(entry);
    }
  }
  selectedIndex_ = ButtonNavigator::clampIndex(selectedIndex_, static_cast<int>(entries_.size()));
}

void ReaderMenuOrderActivity::save() {
  SETTINGS.saveToFile();
}

void ReaderMenuOrderActivity::moveSelectedEntry(int delta) {
  const int targetIndex = selectedIndex_ + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries_.size()) || targetIndex == selectedIndex_) {
    return;
  }

  auto& selectedOrder = getReaderMenuItemOrderRef(SETTINGS, entries_[selectedIndex_]->id);
  auto& targetOrder = getReaderMenuItemOrderRef(SETTINGS, entries_[targetIndex]->id);
  std::swap(selectedOrder, targetOrder);
  normalizeReaderMenuOrderSettings(SETTINGS);

  std::swap(entries_[selectedIndex_], entries_[targetIndex]);
  selectedIndex_ = targetIndex;
  requestUpdate();
}
