#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ScreenSaverPreviewActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string directoryPath;
  std::vector<std::string> imagePaths;
  int selectedIndex = 0;
  bool forReader = false;

  void loadImages();
  void renderPreview(bool showLoadingPopup);
  void selectDirectory();
  void showLoadError(const char* message);

 public:
  ScreenSaverPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string directoryPath,
                             bool forReader = false)
      : Activity("ScreenSaverPreview", renderer, mappedInput), directoryPath(std::move(directoryPath)),
        forReader(forReader) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
  ActivityContext arenaContext() const override { return ActivityContext::SCREENSAVER; }
};
