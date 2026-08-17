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
  std::string currentImagePath_;
  std::string callerFrameBufferPath_ = "/.crosspoint/screensaver-caller.tmp";
  // Overlay text position resolved ONCE per frame so every grayscale pass (BW,
  // LSB, MSB) draws the text in the exact same place. Re-rolling random() per
  // drawTextOverlay() call made the text jump between passes, leaving the old
  // position looking ghosted/dirty. -1 = not resolved yet for this frame.
  int overlayTextPosition_ = -1;

  void loadImages();
  void pickNextImage();
  void freeImageList();
  unsigned long getIntervalMs() const;
  int getMinBatteryPercent() const;
  bool isWakeButtonPressed() const;
  void drawTextOverlay();
  // Resolve the overlay fontId and style for the current screenSaverFontSize
  // setting. Used by both drawTextOverlay() and the pre-decode glyph prewarm.
  void getOverlayFont(int& fontId, EpdFontFamily::Style& style) const;

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
};