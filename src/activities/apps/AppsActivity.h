#pragma once

#include <vector>

#include "../Activity.h"
#include "util/ShortcutRegistry.h"
#include "../util/ListInputMapper.h"

class AppsActivity final : public Activity {
  ListInputMapper listInputMapper;
  int selectedIndex = 0;
  std::vector<const ShortcutDefinition*> appShortcuts;
  std::vector<std::string> shortcutSubtitles;

  void openSelectedApp();
  void rebuildShortcutSubtitles();

 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Apps", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};