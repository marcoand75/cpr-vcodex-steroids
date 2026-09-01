#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ReaderMenuRegistry.h"
#include "../util/OrderListActivity.h"

class ReaderMenuOrderActivity final : public OrderListActivity<ReaderMenuOrderActivity, const ReaderMenuItemDefinition*> {
 public:
  explicit ReaderMenuOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : OrderListActivity("ReaderMenuOrder", renderer, mappedInput) {}

  void reloadEntries() override;
  void save() override;
  void moveSelectedEntry(int delta) override;

  const char* getTitle() const override {
    return tr(STR_READER_MENU_ORDER);
  }

  std::string getEntryTitle(const ReaderMenuItemDefinition* entry) const override {
    return std::string(I18N.get(entry->nameId));
  }
};
