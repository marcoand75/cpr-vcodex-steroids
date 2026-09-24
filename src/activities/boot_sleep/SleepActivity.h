#pragma once

#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

  // Pick a fresh image from the configured sleep directory and draw it without
  // any popup or text. Used by the deep-sleep tap-to-cycle path: APP_STATE must
  // already be loaded; the renderer and display must already be initialized;
  // fonts are not required because only a BMP/PNG is drawn. No-op if no usable
  // image is found — the existing on-screen image stays visible.
  static void cycleScreensaverFromDeepSleep(GfxRenderer& renderer);

  // Snapshot the current framebuffer to SD so the cycle path can re-use it as
  // the background behind a transparent sleep PNG without needing fonts or the
  // EPUB parser. Called from SleepActivity::onEnter() before the "Going to
  // sleep" popup is drawn over the reader page.
  static void snapshotFramebufferForCycle();

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingDashboardSleepScreen() const;
  void renderCoverStatsSleepScreen(bool footerOnly = false) const;
  void renderCustomStatsSleepScreen(bool footerOnly = false) const;
  // preserveBackground: draw over the retained frame (transparent overlays) instead of clearing first.
  void renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath = "",
                               bool preserveBackground = false) const;
  bool renderPngSleepScreen(const std::string& sourcePath) const;
  // Transparent overlay sleep (upstream): alpha BMP/PNG art composited over the last screen.
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderTransparentCustomSleepScreen() const;
  // Quick Resume: keep the last screen and add a small moon marker.
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  bool resolveLastBookCoverPath(std::string& coverBmpPath) const;

  // True when sleep was triggered by the inactivity timeout (Quick Resume after timeout).
  bool fromTimeout = false;
};
