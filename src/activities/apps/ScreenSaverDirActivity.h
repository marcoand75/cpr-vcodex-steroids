#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class ScreenSaverDirActivity final : public Activity {
  ListInputMapper listInputMapper;
  std::vector<std::string> directories;
  int selectedIndex = 0;
  bool forReader = false;

  void loadDirectories();
  void openSelectedDirectory();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);

 public:
  explicit ScreenSaverDirActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forReader = false)
      : Activity("ScreenSaverDir", renderer, mappedInput), forReader(forReader) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
