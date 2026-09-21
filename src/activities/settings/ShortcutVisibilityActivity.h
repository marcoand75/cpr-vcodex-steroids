#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ShortcutRegistry.h"
#include "../util/ListInputMapper.h"

class ShortcutVisibilityActivity final : public Activity {
 public:
  explicit ShortcutVisibilityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ShortcutVisibility", renderer, mappedInput) {}

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