#include "TextOverlay.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <HalDisplay.h>

namespace text_overlay {

void resolveFontFromSize(uint8_t size, int& fontId, EpdFontFamily::Style& style) {
  fontId = UI_10_FONT_ID;
  style = EpdFontFamily::REGULAR;
  switch (static_cast<CrossPointSettings::SCREENSAVER_FONT_SIZE>(size)) {
    case CrossPointSettings::SCREENSAVER_FONT_X_SMALL: fontId = BOOKERLY_10_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_SMALL:    fontId = BOOKERLY_12_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_MEDIUM:   fontId = BOOKERLY_14_FONT_ID; style = EpdFontFamily::REGULAR; break;
    case CrossPointSettings::SCREENSAVER_FONT_LARGE:    fontId = BOOKERLY_16_FONT_ID; style = EpdFontFamily::BOLD;   break;
    case CrossPointSettings::SCREENSAVER_FONT_X_LARGE:   fontId = BOOKERLY_18_FONT_ID; style = EpdFontFamily::BOLD;   break;
    default: break;
  }
}

bool draw(GfxRenderer& renderer, const char* text, const OverlayConfig& cfg) {
  if (!shouldDraw(text)) return false;

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int margin = cfg.margin;

  const int fontId = cfg.fontId;
  const EpdFontFamily::Style textStyle = cfg.fontStyle;

  const int lineHeight = renderer.getLineHeight(fontId);
  auto lines = renderer.wrappedText(fontId, text, pageWidth - 2 * margin, cfg.maxLines, textStyle);
  if (lines.empty()) return false;

  const int textHeight = static_cast<int>(lines.size()) * lineHeight;

  // Resolve position; random position cached across draws so multi-pass
  // grayscale overlays stay aligned.
  int pos = cfg.position;
  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_RANDOM) {
    if (cfg.cachedRandomPosition != nullptr && *cfg.cachedRandomPosition < 0) {
      *cfg.cachedRandomPosition = random(CrossPointSettings::SCREENSAVER_TEXT_POSITION_COUNT - 1);
    }
    if (cfg.cachedRandomPosition != nullptr) {
      pos = *cfg.cachedRandomPosition;
    }
  }

  int baseX = margin;
  int baseY = margin;
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
    const int w = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    if (w > panelW) panelW = w;
  }

  const int panelPadding = cfg.drawPanel ? cfg.panelPaddingWhenPanel : cfg.panelPaddingWhenNoPanel;
  int panelX = margin;
  int panelY = baseY;
  if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_TOP_RIGHT || pos == CrossPointSettings::SCREENSAVER_TEXT_POS_BOTTOM_RIGHT) {
    panelX = pageWidth - margin - panelW - 2 * panelPadding;
  } else if (pos == CrossPointSettings::SCREENSAVER_TEXT_POS_CENTER) {
    panelX = (pageWidth - panelW) / 2 - panelPadding;
  }

  if (cfg.drawPanel) {
    renderer.fillRectDither(panelX, panelY, panelW + 2 * panelPadding, textHeight + 2 * panelPadding, cfg.panelColor);
  }

  const bool textBlack = (cfg.textStyle == CrossPointSettings::SCREENSAVER_TEXT_BLACK ||
                           cfg.textStyle == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);
  const bool outlined = (cfg.textStyle == CrossPointSettings::SCREENSAVER_TEXT_WHITE_OUTLINED_BLACK ||
                          cfg.textStyle == CrossPointSettings::SCREENSAVER_TEXT_BLACK_OUTLINED_WHITE);

  int drawY = baseY + panelPadding;
  for (auto& ln : lines) {
    const int tw = renderer.getTextWidth(fontId, ln.c_str(), textStyle);
    const int dx = panelX + panelPadding + (panelW - tw) / 2;

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
  return true;
}

}  // namespace text_overlay