#pragma once

#include <GfxRenderer.h>

#include <cstdint>

#include "CrossPointSettings.h"
#include "EpdFontFamily.h"
#include "fontIds.h"

namespace text_overlay {

// Text overlay styling pulled from CrossPointSettings (screenSaverText*).
// All values default to the same constants that ScreenSaverActivity uses.
struct OverlayConfig {
  int fontId = UI_10_FONT_ID;
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;

  // Position (CrossPointSettings::SCREENSAVER_TEXT_POSITION).
  uint8_t position = CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT;

  // Style (CrossPointSettings::SCREENSAVER_TEXT_STYLE).
  uint8_t textStyle = CrossPointSettings::SCREENSAVER_TEXT_WHITE_OUTLINED_BLACK;

  // Panel (CrossPointSettings::screenSaverShowPanel, screenSaverPanelColor).
  bool drawPanel = false;
  Color panelColor = Color::Black;

  // Layout
  int margin = 16;
  int panelPaddingWhenPanel = 16;
  int panelPaddingWhenNoPanel = 4;
  int maxLines = 4;

  // Random-position cache (pass the same int& across all draw passes for one
  // frame so the BW / LSB / MSB grayscale overlays stay aligned).
  int* cachedRandomPosition = nullptr;
};

// Translate CrossPointSettings screenSaverFontSize into a fontId/style pair.
void resolveFontFromSize(uint8_t size, int& fontId, EpdFontFamily::Style& style);

// True if the supplied text should produce any visible overlay. Cheap check
// (null/empty) used by callers to skip font loading entirely when the user
// has cleared the screenSaverText setting.
inline bool shouldDraw(const char* text) {
  return text != nullptr && text[0] != '\0';
}

// Main overlay drawer. Computes wrapped lines, panel geometry, position, and
// applies outlined text rendering. Returns false (and does no rendering) when
// text is empty/null so callers can skip font priming entirely.
bool draw(GfxRenderer& renderer, const char* text, const OverlayConfig& cfg);

}  // namespace text_overlay