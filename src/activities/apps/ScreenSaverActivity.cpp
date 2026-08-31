#include "ScreenSaverActivity.h"

#include "CrossPointState.h"
#include "ReadingStatsStore.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <PNGdec.h>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PngSleepRenderer.h"
#include "util/SleepImageUtils.h"

// Declared in main.cpp — free/restore font heap to maximise contiguous memory
// for PNG decoder (~38 KB) during screensaver image rendering.
extern void freeFontMemory();
extern void restoreFontMemory();

void ScreenSaverActivity::loadImages() {
  images_.clear();
  currentImagePath_.clear();
  const char* dir = returnToCaller_ ? SETTINGS.screenSaverReaderDir : SETTINGS.screenSaverDirectory;
  std::string dirPath;
  if (dir[0] != '\0') {
    dirPath = dir;
  } else if (!returnToCaller_) {
    dirPath = SleepImageUtils::resolveConfiguredSleepDirectory();
  } else {
    // For reading activity fallback to general screensaver directory
    if (SETTINGS.screenSaverDirectory[0] != '\0') {
      dirPath = SETTINGS.screenSaverDirectory;
    } else {
      dirPath = SleepImageUtils::resolveConfiguredSleepDirectory();
    }
  }
  if (dirPath.empty()) return;
  images_ = SleepImageUtils::listImageFiles(dirPath);

  // Pick the initial image, then free the vector to reclaim heap for the PNG
  // decoder (~38 KB). Only the current image path is needed for rendering; the
  // next image is resolved lazily in pickNextImage().
  if (!images_.empty()) {
    const uint8_t order = returnToCaller_ ? SETTINGS.screenSaverReaderOrder : SETTINGS.screenSaverOrder;
    if (order == CrossPointSettings::SCREENSAVER_SHUFFLE) {
      // Shuffle the first image too: randomize avoiding the persistent recent
      // list so a fresh screensaver session does not always open on the same
      // first file (images_[0]) every time.
      const uint16_t fileCount = static_cast<uint16_t>(images_.size());
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentScreensaverFill), images_.size() - 1));
      int idx = random(static_cast<int>(images_.size()));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentScreensaver(static_cast<uint16_t>(idx), window);
           attempt++) {
        idx = random(static_cast<int>(images_.size()));
      }
      currentIndex_ = idx;
      currentImagePath_ = images_[static_cast<size_t>(idx)];
      APP_STATE.pushRecentScreensaver(static_cast<uint16_t>(idx));
      APP_STATE.saveToFile();
    } else {
      // Sequential mode: start from the beginning of the scan.
      currentIndex_ = 0;
      currentImagePath_ = images_[0];
    }
  }
  freeImageList();
}

void ScreenSaverActivity::freeImageList() {
  // Free the heap used by the full image list (can be 20+ KB for many images).
  // PNG decoder needs ~38 KB contiguous; every byte matters on ESP32-C3.
  std::vector<std::string>().swap(images_);
}

void ScreenSaverActivity::pickNextImage() {
  const char* dir = returnToCaller_ ? SETTINGS.screenSaverReaderDir : SETTINGS.screenSaverDirectory;
  std::string dirPath;
  if (dir[0] != '\0') {
    dirPath = dir;
  } else if (!returnToCaller_) {
    dirPath = SleepImageUtils::resolveConfiguredSleepDirectory();
  } else {
    if (SETTINGS.screenSaverDirectory[0] != '\0') {
      dirPath = SETTINGS.screenSaverDirectory;
    } else {
      dirPath = SleepImageUtils::resolveConfiguredSleepDirectory();
    }
  }
  if (dirPath.empty()) return;

  // Re-scan the directory to get all current images, pick one, then free.
  auto scanned = SleepImageUtils::listImageFiles(dirPath);
  if (scanned.empty()) {
    currentImagePath_.clear();
    return;
  }

  const uint8_t order = returnToCaller_ ? SETTINGS.screenSaverReaderOrder : SETTINGS.screenSaverOrder;
  int next;
  if (order == CrossPointSettings::SCREENSAVER_SEQUENTIAL) {
    // Find current image index in the new scan to determine next sequentially.
    next = 0;
    for (int i = 0; i < static_cast<int>(scanned.size()); ++i) {
      if (scanned[i] == currentImagePath_) {
        next = (i + 1) % static_cast<int>(scanned.size());
        break;
      }
    }
  } else {
    // Shuffle with anti-repetition: avoid the last N recent images.
    // Uses a persistent 12-entry circular buffer saved to APP_STATE so
    // repetition is avoided across screensaver sessions and deep sleep cycles.
    const uint16_t fileCount = static_cast<uint16_t>(scanned.size());
    const uint8_t window =
        static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentScreensaverFill), scanned.size() - 1));
    next = random(static_cast<int>(scanned.size()));
    for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentScreensaver(static_cast<uint16_t>(next), window);
         attempt++) {
      next = random(static_cast<int>(scanned.size()));
    }
    APP_STATE.pushRecentScreensaver(static_cast<uint16_t>(next));
    APP_STATE.saveToFile();
  }

  currentImagePath_ = scanned[next];
  // Free the scanned list immediately.
  std::vector<std::string>().swap(scanned);
}

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

BitmapPlacement getBitmapPlacement(const Bitmap& bitmap, const int pageWidth, const int pageHeight) {
  BitmapPlacement placement;
  placement.x = 0;
  placement.y = 0;

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      placement.cropX = 1.0f - (screenRatio / ratio);
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      placement.cropY = 1.0f - (ratio / screenRatio);
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
    }
  } else {
    placement.x = (pageWidth - bitmap.getWidth()) / 2;
    placement.y = (pageHeight - bitmap.getHeight()) / 2;
  }

  return placement;
}

unsigned long ScreenSaverActivity::getIntervalMs() const {
  switch (static_cast<CrossPointSettings::SCREENSAVER_INTERVAL>(SETTINGS.screenSaverInterval)) {
    case CrossPointSettings::SCREENSAVER_1_MIN:   return 60000UL;
    case CrossPointSettings::SCREENSAVER_5_MIN:   return 300000UL;
    case CrossPointSettings::SCREENSAVER_15_MIN:  return 900000UL;
    case CrossPointSettings::SCREENSAVER_30_MIN:  return 1800000UL;
    case CrossPointSettings::SCREENSAVER_1_HOUR:  return 3600000UL;
    case CrossPointSettings::SCREENSAVER_2_HOURS: return 7200000UL;
    case CrossPointSettings::SCREENSAVER_4_HOURS: return 14400000UL;
    case CrossPointSettings::SCREENSAVER_8_HOURS: return 28800000UL;
    default: return 1800000UL;
  }
}

int ScreenSaverActivity::getMinBatteryPercent() const {
  // 0=10%, 1=20%, ..., 8=90%
  return (static_cast<int>(SETTINGS.screenSaverMinBattery) + 1) * 10;
}

bool ScreenSaverActivity::isWakeButtonPressed() const {
  const uint8_t wakeBtn = SETTINGS.screenSaverWakeButton;
  if (wakeBtn == CrossPointSettings::SCREENSAVER_WAKE_ANY) {
    static constexpr MappedInputManager::Button allButtons[] = {
        MappedInputManager::Button::Back,    MappedInputManager::Button::Confirm,
        MappedInputManager::Button::Left,    MappedInputManager::Button::Right,
        MappedInputManager::Button::Up,      MappedInputManager::Button::Down,
        MappedInputManager::Button::Power,   MappedInputManager::Button::PageBack,
        MappedInputManager::Button::PageForward,
    };
    for (auto btn : allButtons) {
      if (mappedInput.wasPressed(btn)) return true;
    }
    return false;
  }
  static constexpr MappedInputManager::Button wakeMap[] = {
      MappedInputManager::Button::Back,
      MappedInputManager::Button::Confirm,
      MappedInputManager::Button::Left,
      MappedInputManager::Button::Right,
      MappedInputManager::Button::Up,
      MappedInputManager::Button::Down,
      MappedInputManager::Button::Power,
      MappedInputManager::Button::PageBack,
      MappedInputManager::Button::PageForward,
  };
  int idx = static_cast<int>(wakeBtn) - 1;
  if (idx >= 0 && idx < static_cast<int>(sizeof(wakeMap) / sizeof(wakeMap[0]))) {
    return mappedInput.wasPressed(wakeMap[idx]);
  }
  return false;
}

void ScreenSaverActivity::onEnter() {
  Activity::onEnter();
  loadImages();

  int batPct = static_cast<int>(powerManager.getBatteryPercentage());
  int minPct = getMinBatteryPercent();
  if (minPct > 0 && batPct < minPct) {
    // Show error, then go home
    {
      RenderLock lock(*this);
      renderer.clearScreen();
      GUI.drawPopup(renderer, tr(STR_BATTERY_TOO_LOW));
      delay(2000);
    }
    if (returnToCaller_) {
      finish();
    } else {
      onGoHome();
    }
    return;
  }

  // Save a snapshot of the caller's framebuffer to a temp file so that
  // transparent PNGs can be drawn over the original caller background on
  // each image change.  We write to SD instead of keeping a memory buffer
  // to avoid competing heap with the PNG decoder (~44 KB).
  if (!callerFrameBufferPath_.empty() || Storage.exists(callerFrameBufferPath_.c_str())) {
    Storage.remove(callerFrameBufferPath_.c_str());
  }
  {
    FsFile f;
    if (Storage.openFileForWrite("SS", callerFrameBufferPath_, f)) {
      const uint8_t* buf = display.getFrameBuffer();
      const uint32_t size = display.getBufferSize();
      if (buf && size > 0) {
        f.write(buf, size);
      }
      f.close();
    }
  }

  intervalMs_ = getIntervalMs();
  lastChangeMs_ = millis();
  lastBatteryCheckMs_ = millis();
  firstRender_ = true;

  powerManager.setPowerSaving(true);
  requestUpdate();
}

void ScreenSaverActivity::onExit() {
  // Drain all pending input events so the button used to exit the
  // screensaver does not propagate to the caller (reader/home).
  // Keep updating until the wake button is physically released;
  // a safety counter prevents an infinite stall.
  for (int safety = 0; safety < 200; ++safety) {
    delay(5);
    mappedInput.update();
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.isPressed(MappedInputManager::Button::Left) &&
        !mappedInput.isPressed(MappedInputManager::Button::Right) &&
        !mappedInput.isPressed(MappedInputManager::Button::Up) &&
        !mappedInput.isPressed(MappedInputManager::Button::Down) &&
        !mappedInput.isPressed(MappedInputManager::Button::Power) &&
        !mappedInput.isPressed(MappedInputManager::Button::PageBack) &&
        !mappedInput.isPressed(MappedInputManager::Button::PageForward)) {
      break;
    }
  }
  // Restore the cached caller framebuffer so the transition back to the
  // underlying activity shows the original caller screen (home / reader)
  // without ghosting from the last screensaver frame.
  if (Storage.exists(callerFrameBufferPath_.c_str())) {
    bool restored = false;
    {
      FsFile f;
      if (Storage.openFileForRead("SS", callerFrameBufferPath_, f)) {
        const uint32_t bufSize = display.getBufferSize();
        uint8_t* target = const_cast<uint8_t*>(display.getFrameBuffer());
        if (bufSize > 0 && target) {
          f.read(target, bufSize);
          restored = true;
        }
        f.close();
      }
    }
    if (restored) {
      renderer.clearNextRefreshOverride();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    Storage.remove(callerFrameBufferPath_.c_str());
  }

  // When returning to the reader, reset the reading-stats interaction
  // timestamp so that the time spent looking at the screensaver is not
  // credited as reading time.
  if (returnToCaller_) {
    READING_STATS.resumeSession();
  }

  // Release the lazily-allocated PNG decoder (sizeof(PNG) ~44 KB) and free
  // font/text allocations so the heap returns to its pre-screensaver state
  // before the reader renders its next page.
  PngSleepRenderer::releaseDecoder();
  freeFontMemory();

  Activity::onExit();
  powerManager.setPowerSaving(false);
  currentImagePath_.clear();
  images_.clear();
}

void ScreenSaverActivity::loop() {
  if (isWakeButtonPressed()) {
    if (returnToCaller_) {
      finish();
    } else {
      onGoHome();
    }
    return;
  }
  if (currentImagePath_.empty()) {
    delay(500);
    if (isWakeButtonPressed()) { if (returnToCaller_) { finish(); } else { onGoHome(); } return; }
    return;
  }

  // Periodic battery check (every 30s)
  unsigned long now = millis();
  if (now - lastBatteryCheckMs_ >= 30000UL) {
    lastBatteryCheckMs_ = now;
    int batPct = static_cast<int>(powerManager.getBatteryPercentage());
    int minPct = getMinBatteryPercent();
    if (minPct > 0 && batPct < minPct) {
      // Battery dropped below threshold -> go to deep sleep
      powerManager.setPowerSaving(false);
      APP_STATE.lastSleepFromReader = false;
      APP_STATE.saveToFile();
      powerManager.startDeepSleep(gpio);
      return;
    }
  }

  if (now - lastChangeMs_ >= intervalMs_) {
    lastChangeMs_ = now;
    pickNextImage();
    requestUpdate();
    return;
  }
  delay(100);
}

void ScreenSaverActivity::render(RenderLock&&) {
  // Re-pick the random overlay text position for this frame so the image swap
  // can place the text somewhere new each cycle.
  overlayTextPosition_ = -1;
  if (currentImagePath_.empty()) {
    renderer.clearScreen();
    renderer.displayBuffer();
    return;
  }

  const std::string& imagePath = currentImagePath_;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // ---- Heap trace: before font unload ----
  LOG_DBG("SS", "RENDER start: free=%d maxAlloc=%d minFree=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getMinFreeHeap()));
  LOG_DBG("SS", "RENDER path=%s", imagePath.c_str());

  // Maximise contiguous heap for image decoding.
  // Font caches and decompressor hold ~40-48 KB; freeing them before
  // the image render makes room for the PNG decoder (~38 KB) and
  // grayscale copy buffers.  They are reloaded on demand for the
  // text overlay and will be rebuilt by the caller when needed.
  freeFontMemory();

  LOG_DBG("SS", "RENDER after freeFont: free=%d maxAlloc=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

  // Re-initialise the decompressor and pre-load the overlay glyphs into its
  // page buffer NOW, while heap is still ~60 KB contiguous. Doing it after the
  // image decode would force FontDecompressor::getBitmap() to allocate the hot
  // group on a fragmented heap (maxAlloc ~10 KB) and abort with std::bad_alloc.
  restoreFontMemory();
  const char* overlayText = SETTINGS.screenSaverText;
  if (overlayText != nullptr && overlayText[0] != '\0') {
    int overlayFontId = UI_10_FONT_ID;
    EpdFontFamily::Style overlayStyle = EpdFontFamily::REGULAR;
    getOverlayFont(overlayFontId, overlayStyle);
    // styleMask covers REGULAR(0) and BOLD(1); the overlay uses one of them.
    const uint8_t styleMask = (overlayStyle == EpdFontFamily::BOLD) ? 0x02 : 0x01;
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->prewarmCache(overlayFontId, overlayText, styleMask);
    }
  }

  // Use same grayscale rendering path as SleepActivity
  bool isPng = FsHelpers::hasPngExtension(imagePath);
  bool isBmp = FsHelpers::hasBmpExtension(imagePath);

  if (!isPng && !isBmp) {
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    LOG_DBG("SS", "RENDER done (unsupported format): free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  if (isPng) {
    // Restore the caller framebuffer from the temp file so that transparent
    // PNG draws over the original caller background.  On the first render
    // this shows the caller screen; on subsequent renders it clears residues
    // left by the previous image.
    {
      FsFile f;
      if (Storage.openFileForRead("SS", callerFrameBufferPath_, f)) {
        const uint32_t bufSize = display.getBufferSize();
        uint8_t* target = const_cast<uint8_t*>(display.getFrameBuffer());
        if (bufSize > 0 && target) {
          f.read(target, bufSize);
        }
        f.close();
      }
    }

    LOG_DBG("SS", "RENDER PNG before decode: free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // Try "SS" prefix first (screensaver directory), then "SLP" (sleep directory).
    bool pngOk = PngSleepRenderer::drawTransparentPng(imagePath, renderer, 0, 0, pageWidth, pageHeight, "SS");
    if (!pngOk) {
      pngOk = PngSleepRenderer::drawTransparentPng(imagePath, renderer, 0, 0, pageWidth, pageHeight, "SLP");
    }

    LOG_DBG("SS", "RENDER PNG after decode (ok=%d): free=%d maxAlloc=%d",
            static_cast<int>(pngOk), static_cast<int>(ESP.getFreeHeap()),
            static_cast<int>(ESP.getMaxAllocHeap()));

    // Fonts were already restored and prewarmed before the decode; the overlay
    // glyphs are in the page buffer so no hot-group alloc happens here.
    if (pngOk) {
      drawTextOverlay();
      renderer.clearNextRefreshOverride();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      // Release font caches now to keep maxAlloc high for the next wake-up.
      freeFontMemory();
      LOG_DBG("SS", "RENDER done (PNG OK): free=%d maxAlloc=%d",
              static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
      return;
    }
    // Fall through to white screen on PNG failure
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    freeFontMemory();
    LOG_DBG("SS", "RENDER done (PNG FAIL, white): free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  // BMP rendering with grayscale support (same as SleepActivity)
  FsFile file;
  if (!Storage.openFileForRead("SS", imagePath, file)) {
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    LOG_DBG("SS", "RENDER done (BMP open fail): free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  LOG_DBG("SS", "RENDER BMP parseHeaders: free=%d maxAlloc=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    LOG_DBG("SS", "RENDER done (BMP parse fail): free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  bool hasGreyscale = bitmap.hasGreyscale();

  // Skip grayscale rendering if heap is too fragmented for the
  // extra copy buffers needed by the LSB/MSB passes.  On ESP32-C3
  // with the reader's tiled grayscale buffers still allocated below,
  // MaxAlloc can drop below 10 KB — insufficient for grayscale ops.
  if (hasGreyscale && ESP.getMaxAllocHeap() < 10000) {
    LOG_DBG("SS", "RENDER BMP grayscale SKIPPED (maxAlloc=%d < 10000)",
            static_cast<int>(ESP.getMaxAllocHeap()));
    hasGreyscale = false;
  }

  LOG_DBG("SS", "RENDER BMP BW pass: free=%d maxAlloc=%d hasGreyscale=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(hasGreyscale));

  const BitmapPlacement placement = getBitmapPlacement(bitmap, pageWidth, pageHeight);

  // BW pass
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);

  // Fonts were already restored and prewarmed before the decode.
  drawTextOverlay();

  renderer.clearNextRefreshOverride();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    LOG_DBG("SS", "RENDER BMP grayscale LSB pass: free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // LSB pass — fonts already restored for overlay
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
    drawTextOverlay();
    renderer.copyGrayscaleLsbBuffers();

    LOG_DBG("SS", "RENDER BMP grayscale MSB pass: free=%d maxAlloc=%d",
            static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // MSB pass
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
    drawTextOverlay();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  file.close();
  LOG_DBG("SS", "RENDER done (BMP): free=%d maxAlloc=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
}

void ScreenSaverActivity::getOverlayFont(int& fontId, EpdFontFamily::Style& style) const {
  fontId = UI_10_FONT_ID;
  style = EpdFontFamily::REGULAR;
  // Use Bookerly (always available) at the size corresponding to the setting.
  // Regular for X_SMALL through MEDIUM, Bold for LARGE and X_LARGE.
  switch (SETTINGS.screenSaverFontSize) {
    case CrossPointSettings::SCREENSAVER_FONT_X_SMALL: fontId = BOOKERLY_10_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_SMALL:  fontId = BOOKERLY_12_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_MEDIUM: fontId = BOOKERLY_14_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_LARGE:  fontId = BOOKERLY_16_FONT_ID; style = EpdFontFamily::BOLD; break;
    case CrossPointSettings::SCREENSAVER_FONT_X_LARGE: fontId = BOOKERLY_18_FONT_ID; style = EpdFontFamily::BOLD; break;
  }
}

void ScreenSaverActivity::drawTextOverlay() {
  const char* text = SETTINGS.screenSaverText;
  if (text == nullptr || text[0] == '\0') return;

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int margin = 16;

  int fontId = UI_10_FONT_ID;
  EpdFontFamily::Style textStyle = EpdFontFamily::REGULAR;
  getOverlayFont(fontId, textStyle);

  const int lineHeight = renderer.getLineHeight(fontId);
  auto lines = renderer.wrappedText(fontId, text, pageWidth - 2 * margin, 4, textStyle);
  if (lines.empty()) return;

  const int textHeight = static_cast<int>(lines.size()) * lineHeight;

  bool drawPanel = SETTINGS.screenSaverShowPanel != 0;
  int pos;
  const uint8_t configuredPos = SETTINGS.screenSaverTextPosition;
  if (configuredPos == CrossPointSettings::SCREENSAVER_TEXT_POS_RANDOM) {
    // Resolve the random position ONCE per frame (not per pass). Rolled lazily
    // here and cached so the BW / LSB / MSB grayscale passes and any subsequent
    // drawTextOverlay() call in the same render all agree on the same spot,
    // otherwise the text would land in different random places between passes
    // and the discarded position would look like ghosting.
    if (overlayTextPosition_ < 0) {
      overlayTextPosition_ = random(CrossPointSettings::SCREENSAVER_TEXT_POSITION_COUNT - 1);
    }
    pos = overlayTextPosition_;
  } else {
    pos = configuredPos;
  }

  int baseX = margin, baseY = margin;
  switch (pos) {
    case CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_LEFT:     baseX = margin; baseY = margin; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT:    baseX = pageWidth - margin; baseY = margin; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_LEFT:  baseX = margin; baseY = pageHeight - margin - textHeight; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT: baseX = pageWidth - margin; baseY = pageHeight - margin - textHeight; break;
    case CrossPointSettings::SCREENSAVER_TEXT_POS_CENTER:       baseX = pageWidth / 2; baseY = (pageHeight - textHeight) / 2; break;
    default: break;
  }

  int panelW = 0;
  for (auto& ln : lines) {
    int w = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    if (w > panelW) panelW = w;
  }

  int panelPadding = drawPanel ? 16 : 4;
  int panelX, panelY = baseY;

  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT || pos == CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT) {
    panelX = pageWidth - margin - panelW - 2 * panelPadding;
  } else if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_CENTER) {
    panelX = (pageWidth - panelW) / 2 - panelPadding;
  } else {
    panelX = margin;
  }

  if (drawPanel) {
    renderer.fillRectDither(panelX, panelY, panelW + 2 * panelPadding, textHeight + 2 * panelPadding,
                            SETTINGS.screenSaverPanelColor == 0 ? Color::Black : Color::White);
  }

  int style = SETTINGS.screenSaverTextStyle;
  bool textBlack = (style == CrossPointSettings::SCREENSAVER_TEXT_BLACK || style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);
  bool outlined = (style == CrossPointSettings::SCREENSAVER_TEXT_WHITE_OUTLINED_BLACK || style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);

  int drawY = baseY + panelPadding;
  for (auto& ln : lines) {
    int tw = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    int dx = panelX + panelPadding + (panelW - tw) / 2;

    if (outlined) {
      renderer.drawText(fontId, dx - 2, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 2, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY - 2, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY + 2, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx - 1, drawY - 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 1, drawY - 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx - 1, drawY + 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 1, drawY + 1, ln.c_str(), !textBlack, textStyle);
    }
    renderer.drawText(fontId, dx, drawY, ln.c_str(), textBlack, textStyle);
    drawY += lineHeight;
  }
}