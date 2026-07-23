#pragma once

#include <string>
#include <vector>

#include "../Activity.h"

class ScreenSaverActivity final : public Activity {
 private:
  std::vector<std::string> images_;
  int currentIndex_ = 0;
  unsigned long lastChangeMs_ = 0;
  unsigned long intervalMs_ = 0;
  unsigned long lastBatteryCheckMs_ = 0;
  bool firstRender_ = true;
  bool returnToCaller_ = false;
  std::string callerFrameBufferPath_ = "/.crosspoint/screensaver-caller.tmp";  // temp file for caller framebuffer snapshot

  void loadImages();
  unsigned long getIntervalMs() const;
  int getMinBatteryPercent() const;
  bool isWakeButtonPressed() const;
  void drawTextOverlay();

 public:
  explicit ScreenSaverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool returnToCaller = false)
      : Activity("ScreenSaver", renderer, mappedInput), returnToCaller_(returnToCaller) {}
  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_NONE; }
  bool preventAutoSleep() override { return true; }
   bool isScreenSaverActivity() const override { return true; }
   ActivityContext arenaContext() const override { return ActivityContext::SCREENSAVER; }
 };