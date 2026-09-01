#include "ShortcutOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ShortcutOrderActivity::reloadEntries() {
  entries_ = getShortcutOrderEntries(group);
  selectedIndex_ = ButtonNavigator::clampIndex(selectedIndex_, static_cast<int>(entries_.size()));
}

void ShortcutOrderActivity::save() {
  SETTINGS.saveToFile();
}

void ShortcutOrderActivity::moveSelectedEntry(int delta) {
  const int targetIndex = selectedIndex_ + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries_.size()) || targetIndex == selectedIndex_) {
    return;
  }

  auto& selectedOrder = getShortcutOrderRef(SETTINGS, entries_[selectedIndex_]);
  auto& targetOrder = getShortcutOrderRef(SETTINGS, entries_[targetIndex]);
  std::swap(selectedOrder, targetOrder);
  normalizeShortcutOrderSettings(SETTINGS);

  std::swap(entries_[selectedIndex_], entries_[targetIndex]);
  selectedIndex_ = targetIndex;
  requestUpdate();
}
