#include "ScreenSaverActivity.h"

#include "CrossPointState.h"
#include "ReadingStatsStore.h"

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

// Enable heap tracing only during development. In production, serial logging
// adds 5-20ms per call and significantly slows down screen refresh.
#ifdef SS_HEAP_TRACE
#define SS_TRACE(...) LOG_DBG(__VA_ARGS__)
#else
#define SS_TRACE(...) ((void)0)
#endif

namespace {

constexpr MappedInputManager::Button kAllButtons[] = {
    MappedInputManager::Button::Back,      MappedInputManager::Button::Confirm,
    MappedInputManager::Button::Left,      MappedInputManager::Button::Right,
    MappedInputManager::Button::Up,        MappedInputManager::Button::Down,
    MappedInputManager::Button::Power,     MappedInputManager::Button::PageBack,
    MappedInputManager::Button::PageForward,
};

}  // namespace

std::string ScreenSaverActivity::resolveScreensaverDir() const {
  const char* dir = returnToCaller_ ? SETTINGS.screenSaverReaderDir : SETTINGS.screenSaverDirectory;
  if (dir[0] != '\0') {
    return std::string(dir);
  }
  if (!returnToCaller_) {
    return SleepImageUtils::resolveConfiguredSleepDirectory();
  }
  // Reading activity fallback to general screensaver directory
  if (SETTINGS.screenSaverDirectory[0] != '\0') {
    return std::string(SETTINGS.screenSaverDirectory);
  }
  return SleepImageUtils::resolveConfiguredSleepDirectory();
}

void ScreenSaverActivity::loadImages() {
  images_.clear();
  currentImagePath_.clear();

  const std::string dirPath = resolveScreensaverDir();
  if (dirPath.empty()) return;

  images_ = SleepImageUtils::listImageFiles(dirPath);

  // Determine the first image. Sequential order starts at images_[0]; shuffle
  // picks a random image (anti-repetition) so the first shown image differs
  // between sessions instead of always being the same file.
  // NOTE: Do NOT call freeImageList() here — onEnter() needs images_ alive
  // for fallback selection and initial shuffle randomization.
  if (!images_.empty()) {
    const uint8_t order = returnToCaller_ ? SETTINGS.screenSaverReaderOrder : SETTINGS.screenSaverOrder;
    if (order == CrossPointSettings::SCREENSAVER_SHUFFLE && images_.size() > 1) {
      const size_t count = images_.size();
      const uint8_t window = static_cast<uint8_t>(
          std::min(static_cast<size_t>(APP_STATE.recentScreensaverFill), count - 1));
      size_t next = static_cast<size_t>(random(static_cast<int>(count)));
      for (uint8_t attempt = 0;
           attempt < 20 && APP_STATE.isRecentScreensaver(static_cast<uint16_t>(next), window);
           attempt++) {
        next = static_cast<size_t>(random(static_cast<int>(count)));
      }
      currentIndex_ = static_cast<int>(next);
      currentImagePath_ = images_[next];
      APP_STATE.pushRecentScreensaver(static_cast<uint16_t>(next));
      APP_STATE.saveToFile();
    } else {
      currentIndex_ = 0;
      currentImagePath_ = images_[0];
    }
  }
}

void ScreenSaverActivity::freeImageList() {
  // Free the heap used by the full image list (can be 20+ KB for many images).
  // PNG decoder needs ~38 KB contiguous; every byte matters on ESP32-C3.
  std::vector<std::string>().swap(images_);
}

void ScreenSaverActivity::pickNextImage() {
  const std::string dirPath = resolveScreensaverDir();
  if (dirPath.empty()) return;

  // Re-scan the directory to get all current images, pick one, then free.
  auto scanned = SleepImageUtils::listImageFiles(dirPath);
  if (scanned.empty()) {
    currentImagePath_.clear();
    return;
  }

  const uint8_t order = returnToCaller_ ? SETTINGS.screenSaverReaderOrder : SETTINGS.screenSaverOrder;
  int next = 0;

  if (order == CrossPointSettings::SCREENSAVER_SEQUENTIAL) {
    // Find current image index in the new scan to determine next sequentially.
    for (int i = 0; i < static_cast<int>(scanned.size()); ++i) {
      if (scanned[i] == currentImagePath_) {
        next = (i + 1) % static_cast<int>(scanned.size());
        break;
      }
    }
  } else {
    // Shuffle with anti-repetition: avoid the last N recent images.
    const uint8_t window = static_cast<uint8_t>(
        std::min(static_cast<size_t>(APP_STATE.recentScreensaverFill), scanned.size() - 1));
    next = random(static_cast<int>(scanned.size()));
    for (uint8_t attempt = 0;
         attempt < 20 && APP_STATE.isRecentScreensaver(static_cast<uint16_t>(next), window);
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
    for (auto btn : kAllButtons) {
      if (mappedInput.wasPressed(btn)) return true;
    }
    return false;
  }

  const int idx = static_cast<int>(wakeBtn) - 1;
  if (idx >= 0 && idx < static_cast<int>(sizeof(kAllButtons) / sizeof(kAllButtons[0]))) {
    return mappedInput.wasPressed(kAllButtons[idx]);
  }
  return false;
}

void ScreenSaverActivity::onEnter() {
  Activity::onEnter();
  loadImages();

  // Fallback: if shuffle/randomization didn't select an image, use the first.
  // images_ is still alive here (freeImageList not yet called).
  if (currentImagePath_.empty() && !images_.empty()) {
    currentImagePath_ = images_[0];
    currentIndex_ = 0;
  }

  // Randomize first image in shuffle mode (only if not already set by loadImages)
  const uint8_t order = returnToCaller_ ? SETTINGS.screenSaverReaderOrder : SETTINGS.screenSaverOrder;
  if (!images_.empty() && order == CrossPointSettings::SCREENSAVER_SHUFFLE && currentIndex_ == 0) {
    currentIndex_ = random(static_cast<int>(images_.size()));
    if (currentIndex_ < static_cast<int>(images_.size())) {
      currentImagePath_ = images_[currentIndex_];
    }
  }

  // NOW free the image list — all selection logic is complete.
  freeImageList();

  // Battery check: refuse to start if below minimum
  int batPct = static_cast<int>(powerManager.getBatteryPercentage());
  int minPct = getMinBatteryPercent();
  if (minPct > 0 && batPct < minPct) {
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
  // each image change. We write to SD instead of keeping a memory buffer
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
  bool drained = false;
  for (int safety = 0; safety < 100; ++safety) {
    delay(5);
    mappedInput.update();

    bool anyPressed = false;
    for (auto btn : kAllButtons) {
      if (mappedInput.isPressed(btn)) {
        anyPressed = true;
        break;
      }
    }
    if (!anyPressed) {
      drained = true;
      break;
    }
  }
  if (!drained) {
    LOG_DBG("SS", "Wake button drain timeout after 500ms");
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
          const int bytesRead = f.read(target, bufSize);
          restored = (bytesRead == static_cast<int>(bufSize));
        }
        f.close();
      }
    }
    if (restored) {
      renderer.clearNextRefreshOverride();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    } else {
      LOG_ERR("SS", "Failed to restore caller framebuffer");
    }
    Storage.remove(callerFrameBufferPath_.c_str());
  }

  // When returning to the reader, reset the reading-stats interaction
  // timestamp so that the time spent looking at the screensaver is not
  // credited as reading time.
  if (returnToCaller_) {
    READING_STATS.resumeSession();
  }

  // Release the PNG decoder and free any font/text allocations so the heap
  // returns to its pre-screensaver state before the reader renders its next
  // page. This order matters: release the big block first, then compact
  // whatever font/text left behind.
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
    if (isWakeButtonPressed()) {
      if (returnToCaller_) { finish(); } else { onGoHome(); }
    }
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
  if (currentImagePath_.empty()) {
    renderer.clearScreen();
    renderer.displayBuffer();
    return;
  }

  const std::string& imagePath = currentImagePath_;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  SS_TRACE("SS", "RENDER start: free=%d maxAlloc=%d minFree=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
           static_cast<int>(ESP.getMinFreeHeap()));
  SS_TRACE("SS", "RENDER path=%s", imagePath.c_str());

  // Maximise contiguous heap for image decoding.
  // Font caches and decompressor hold ~40-48 KB; freeing them before
  // the image render makes room for the PNG decoder (~38 KB) and
  // grayscale copy buffers. They are reloaded on demand for the
  // text overlay and will be rebuilt by the caller when needed.
  freeFontMemory();

  SS_TRACE("SS", "RENDER after freeFont: free=%d maxAlloc=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

  bool isPng = FsHelpers::hasPngExtension(imagePath);
  bool isBmp = FsHelpers::hasBmpExtension(imagePath);

  if (!isPng && !isBmp) {
    renderer.clearScreen();
    restoreFontMemory();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    SS_TRACE("SS", "RENDER done (unsupported format): free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  if (isPng) {
    // Restore the caller framebuffer from the temp file so that transparent
    // PNG draws over the original caller background. On the first render
    // this shows the caller screen; on subsequent renders it clears residues
    // left by the previous image.
    bool bgRestored = false;
    {
      FsFile f;
      if (Storage.openFileForRead("SS", callerFrameBufferPath_, f)) {
        const uint32_t bufSize = display.getBufferSize();
        uint8_t* target = const_cast<uint8_t*>(display.getFrameBuffer());
        if (bufSize > 0 && target) {
          const int bytesRead = f.read(target, bufSize);
          bgRestored = (bytesRead == static_cast<int>(bufSize));
        }
        f.close();
      }
    }
    if (!bgRestored) {
      renderer.clearScreen();
      SS_TRACE("SS", "RENDER PNG: caller FB restore failed, using clear screen");
    }

    SS_TRACE("SS", "RENDER PNG before decode: free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // Try "SS" prefix first (screensaver directory), then "SLP" (sleep directory).
    bool pngOk = PngSleepRenderer::drawTransparentPng(imagePath, renderer, 0, 0, pageWidth, pageHeight, "SS");
    if (!pngOk) {
      pngOk = PngSleepRenderer::drawTransparentPng(imagePath, renderer, 0, 0, pageWidth, pageHeight, "SLP");
    }

    SS_TRACE("SS", "RENDER PNG after decode (ok=%d): free=%d maxAlloc=%d",
             static_cast<int>(pngOk), static_cast<int>(ESP.getFreeHeap()),
             static_cast<int>(ESP.getMaxAllocHeap()));

    // Restore fonts for text overlay
    restoreFontMemory();

    SS_TRACE("SS", "RENDER after restoreFont: free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    if (pngOk) {
      drawTextOverlay();
      renderer.clearNextRefreshOverride();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      SS_TRACE("SS", "RENDER done (PNG OK): free=%d maxAlloc=%d",
               static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
      return;
    }

    // Fall through to white screen on PNG failure
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    SS_TRACE("SS", "RENDER done (PNG FAIL, white): free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  // BMP rendering with grayscale support (same as SleepActivity)
  FsFile file;
  if (!Storage.openFileForRead("SS", imagePath, file)) {
    restoreFontMemory();
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    SS_TRACE("SS", "RENDER done (BMP open fail): free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  SS_TRACE("SS", "RENDER BMP parseHeaders: free=%d maxAlloc=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    restoreFontMemory();
    renderer.clearScreen();
    drawTextOverlay();
    renderer.clearNextRefreshOverride();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    SS_TRACE("SS", "RENDER done (BMP parse fail): free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
    return;
  }

  float cropX = 0, cropY = 0;
  int x = 0, y = 0;
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
    if (ratio > screenRatio) {
      cropX = 1.0f - (screenRatio / ratio);
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      cropY = 1.0f - (ratio / screenRatio);
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  bool hasGreyscale = bitmap.hasGreyscale();

  // Skip grayscale rendering if heap is too fragmented for the
  // extra copy buffers needed by the LSB/MSB passes.
  if (hasGreyscale && ESP.getMaxAllocHeap() < 10000) {
    SS_TRACE("SS", "RENDER BMP grayscale SKIPPED (maxAlloc=%d < 10000)",
             static_cast<int>(ESP.getMaxAllocHeap()));
    hasGreyscale = false;
  }

  SS_TRACE("SS", "RENDER BMP BW pass: free=%d maxAlloc=%d hasGreyscale=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
           static_cast<int>(hasGreyscale));

  // BW pass
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  // Restore fonts for text overlay on BW
  restoreFontMemory();

  SS_TRACE("SS", "RENDER BMP after restoreFont: free=%d maxAlloc=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

  drawTextOverlay();

  renderer.clearNextRefreshOverride();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    SS_TRACE("SS", "RENDER BMP grayscale LSB pass: free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // LSB pass — fonts already restored for overlay
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawTextOverlay();
    renderer.copyGrayscaleLsbBuffers();

    SS_TRACE("SS", "RENDER BMP grayscale MSB pass: free=%d maxAlloc=%d",
             static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));

    // MSB pass
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawTextOverlay();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  file.close();
  SS_TRACE("SS", "RENDER done (BMP): free=%d maxAlloc=%d",
           static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()));
}

void ScreenSaverActivity::drawTextOverlay() {
  const char* text = SETTINGS.screenSaverText;
  if (text == nullptr || text[0] == '\0') return;

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int margin = 16;

  int fontId = UI_10_FONT_ID;
  EpdFontFamily::Style textStyle = EpdFontFamily::REGULAR;
  switch (SETTINGS.screenSaverFontSize) {
    case CrossPointSettings::SCREENSAVER_FONT_X_SMALL: fontId = BOOKERLY_10_FONT_ID; textStyle = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_SMALL:  fontId = BOOKERLY_12_FONT_ID; textStyle = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_MEDIUM: fontId = BOOKERLY_14_FONT_ID; textStyle = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_LARGE:  fontId = BOOKERLY_16_FONT_ID; textStyle = EpdFontFamily::BOLD; break;
    case CrossPointSettings::SCREENSAVER_FONT_X_LARGE: fontId = BOOKERLY_18_FONT_ID; textStyle = EpdFontFamily::BOLD; break;
  }

  const int lineHeight = renderer.getLineHeight(fontId);
  auto lines = renderer.wrappedText(fontId, text, pageWidth - 2 * margin, 4, textStyle);
  if (lines.empty()) return;

  const int textHeight = static_cast<int>(lines.size()) * lineHeight;

  bool drawPanel = SETTINGS.screenSaverShowPanel != 0;
  int pos = SETTINGS.screenSaverTextPosition;
  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_RANDOM) {
    pos = random(CrossPointSettings::SCREENSAVER_TEXT_POSITION_COUNT - 1);
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
  for (const auto& ln : lines) {
    int w = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    if (w > panelW) panelW = w;
  }

  int panelPadding = drawPanel ? 16 : 4;
  int panelX, panelY = baseY;

  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT ||
      pos == CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT) {
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
  bool textBlack = (style == CrossPointSettings::SCREENSAVER_TEXT_BLACK ||
                    style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);
  bool outlined = (style == CrossPointSettings::SCREENSAVER_TEXT_WHITE_OUTLINED_BLACK ||
                   style == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);

  int drawY = baseY + panelPadding;
  for (const auto& ln : lines) {
    int tw = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    int dx = panelX + panelPadding + (panelW - tw) / 2;

    if (outlined) {
      // 4-direction outline (N/S/E/W) — sufficient visual quality on EPD,
      // avoids 4 extra drawText calls vs 8-direction diagonal outline.
      renderer.drawText(fontId, dx - 1, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx + 1, drawY, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY - 1, ln.c_str(), !textBlack, textStyle);
      renderer.drawText(fontId, dx, drawY + 1, ln.c_str(), !textBlack, textStyle);
    }
    renderer.drawText(fontId, dx, drawY, ln.c_str(), textBlack, textStyle);
    drawY += lineHeight;
  }
}