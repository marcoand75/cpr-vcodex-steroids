#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ScreenSaverDirActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> directories;
  int selectedIndex = 0;

  void loadDirectories();
  void openSelectedDirectory();

 public:
  explicit ScreenSaverDirActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenSaverDir", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
