#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "fontIds.h"
#include "images/Logo.h"
#include "version.h"

namespace {
constexpr int BOOT_LOGO_WIDTH = 350;
constexpr int BOOT_LOGO_HEIGHT = 96;
constexpr int LOGO_TEXT_GAP = 10;
constexpr int SUBTITLE_GAP = 25;
}

void BootActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("BOOT", "BootActivity::onEnter() start");
  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  LOG_DBG("BOOT", "Screen: %dx%d", pageWidth, pageHeight);
  const int logoX = (pageWidth - BOOT_LOGO_WIDTH) / 2;
  const int logoY = (pageHeight - BOOT_LOGO_HEIGHT) / 2;
  const int titleY = logoY + BOOT_LOGO_HEIGHT + LOGO_TEXT_GAP;
  const int subtitleY = titleY + SUBTITLE_GAP;
  LOG_DBG("BOOT", "Logo pos: (%d,%d) size: %dx%d", logoX, logoY, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT);

  renderer.clearScreen();
  LOG_DBG("BOOT", "clearScreen done");
  renderer.drawIcon(Logo, logoX, logoY, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT);
  LOG_DBG("BOOT", "drawIcon done");
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CPR_VCODEX_STEROIDS), true, EpdFontFamily::BOLD);
  LOG_DBG("BOOT", "drawCenteredText STR_STEROIDS done");
  renderer.drawCenteredText(SMALL_FONT_ID, subtitleY, tr(STR_BOOTING));
  LOG_DBG("BOOT", "drawCenteredText STR_BOOTING done");
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  LOG_DBG("BOOT", "drawCenteredText version done");
  renderer.displayBuffer();
  LOG_DBG("BOOT", "displayBuffer done");

  if (restoreDarkMode) {
    renderer.setDarkMode(true);
  }
  LOG_DBG("BOOT", "BootActivity::onEnter() complete");
}
