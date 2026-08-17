#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "ReadingStatsStore.h"
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
  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int logoX = (pageWidth - BOOT_LOGO_WIDTH) / 2;
  const int logoY = (pageHeight - BOOT_LOGO_HEIGHT) / 2;
  const int titleY = logoY + BOOT_LOGO_HEIGHT + LOGO_TEXT_GAP;
  const int subtitleY = titleY + SUBTITLE_GAP;

  renderer.clearScreen();
  renderer.drawIcon(Logo, logoX, logoY, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT);
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CPR_VCODEX), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, subtitleY, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();

  // Load reading stats while the boot screen is visible. This keeps setup()
  // memory-light while ensuring stats are ready for HomeActivity/carousel
  // progress badges without fragmenting the heap later.
  READING_STATS.ensureLoaded();

  if (restoreDarkMode) {
    renderer.setDarkMode(true);
  }
}
