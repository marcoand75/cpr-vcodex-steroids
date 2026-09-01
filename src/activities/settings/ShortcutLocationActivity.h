#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ShortcutRegistry.h"
#include "../util/ListInputMapper.h"

class ShortcutLocationActivity final : public Activity {
 public:
  explicit ShortcutLocationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ShortcutLocation", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ListInputMapper listInputMapper;
  std::vector<const ShortcutDefinition*> entries;
  int selectedIndex = 0;

  void reloadEntries();
  void toggleSelectedEntry();
};