#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ShortcutRegistry.h"
#include "../util/ListInputMapper.h"

class ShortcutOrderActivity final : public Activity {
 public:
  ShortcutOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ShortcutOrderGroup group)
      : Activity("ShortcutOrder", renderer, mappedInput), group(group) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  std::vector<ShortcutOrderEntry> entries;
  int selectedIndex = 0;
  bool moveMode = false;

  void reloadEntries();
  void moveSelectedEntry(int delta);

 private:
  ShortcutOrderGroup group;
  ListInputMapper listInputMapper;
  const char* getTitle() const;

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);
};
