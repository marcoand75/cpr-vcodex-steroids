#pragma once

#include <vector>

#include "FavoritesStore.h"
#include "../Activity.h"
#include "../util/OrderListActivity.h"

class FavoritesOrderActivity final : public OrderListActivity<FavoritesOrderActivity, FavoriteBook> {
 public:
  FavoritesOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : OrderListActivity("FavoritesOrder", renderer, mappedInput) {}

  void reloadEntries() override;
  void moveSelectedEntry(int delta) override;
  void confirmDeleteSelectedEntry();
  void render(RenderLock&& lock) override;

  // Hold-to-delete: when not in moveMode, a confirm press held for >=1s deletes the selected entry.
  bool handleConfirmHold(unsigned long heldMs) override;

  const char* getTitle() const override { return tr(STR_ORDER_FAVORITES); }

  std::string getEntryTitle(FavoriteBook entry) const override;

  // FAVORITES is persisted live; nothing to do on exit.
  void save() override {}
};