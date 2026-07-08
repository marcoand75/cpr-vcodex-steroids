#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class ScreenSaverDirActivity final : public Activity {
  ListInputMapper listInputMapper;
  std::vector<std::string> directories;
  int selectedIndex = 0;

  void loadDirectories();
  void openSelectedDirectory();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);

 public:
  explicit ScreenSaverDirActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenSaverDir", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
