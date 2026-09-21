#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ShortcutRegistry.h"
#include "../util/OrderListActivity.h"

class ShortcutOrderActivity final : public OrderListActivity<ShortcutOrderActivity, ShortcutOrderEntry> {
 public:
  explicit ShortcutOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ShortcutOrderGroup group)
      : OrderListActivity("ShortcutOrder", renderer, mappedInput), group(group) {}

  void reloadEntries() override;
  void save() override;
  void moveSelectedEntry(int delta) override;

  const char* getTitle() const override {
    return group == ShortcutOrderGroup::Home ? tr(STR_ORDER_HOME_SHORTCUTS) : tr(STR_ORDER_APPS_SHORTCUTS);
  }

  std::string getEntryTitle(ShortcutOrderEntry entry) const override {
    return entry.isAppsHub ? std::string(tr(STR_APPS)) : std::string(I18N.get(entry.definition->nameId));
  }

 private:
  ShortcutOrderGroup group;
};
