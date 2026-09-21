#pragma once

#include <GfxRenderer.h>
#include <EpdFontFamily.h>

#include <algorithm>
#include <cstdint>
#include <string>

// Shared text/panel drawing helpers extracted from SleepActivity's
// dashboard/cover-stats panels. They are pure stateless wrappers around
// renderer.drawText() / drawRect() / fillRect() — no theme override hooks
// (those still live on BaseTheme::drawProgressBar / drawButtonHints etc).
//
// Why these belong in a util: every Steroids panel-style screen (sleep
// dashboard, cover stats, achievement popup, library progress overlays,
// settings value previews) repeats the same "label on the left, value on
// the right, optional progress bar" pattern. Centralizing avoids the
// 5-copy-paste dance and keeps the font-id constants in one place.
namespace text_draw {

// Draw `text` clipped to `maxWidth` pixels (truncated with ellipsis when needed).
inline void drawTextClipped(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                            const std::string& text, const int maxWidth, const bool black = true,
                            const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(fontId, x, y, renderer.truncatedText(fontId, text.c_str(), maxWidth, style).c_str(), black, style);
}

// Draw `text` right-aligned so that its right edge ends at `right`.
inline void drawRightText(const GfxRenderer& renderer, const int fontId, const int right, const int y,
                          const std::string& text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(fontId, right - renderer.getTextWidth(fontId, text.c_str(), style), y, text.c_str(), true, style);
}

// Composite helper: left-aligned `text` clipped to leave room for the
// right-aligned `value`. Both strings live on the same baseline (y).
inline void drawTextWithRightValue(const GfxRenderer& renderer, const int fontId, const int x, const int right,
                                   const int y, const std::string& text, const std::string& value,
                                   const EpdFontFamily::Style textStyle = EpdFontFamily::REGULAR,
                                   const EpdFontFamily::Style valueStyle = EpdFontFamily::REGULAR) {
  const int valueWidth = renderer.getTextWidth(fontId, value.c_str(), valueStyle);
  const int textWidth = std::max(0, right - x - valueWidth - 8);
  drawTextClipped(renderer, fontId, x, y, text, textWidth, true, textStyle);
  drawRightText(renderer, fontId, right, y, value, valueStyle);
}

// Draw a small 16x16 checkmark checkbox at (x, y). Solid filled when checked.
inline void drawCheckBox(const GfxRenderer& renderer, const int x, const int y, const bool checked) {
  renderer.drawRect(x, y, 16, 16, 1, true);
  if (!checked) {
    return;
  }
  renderer.fillRect(x, y, 16, 16, true);
  renderer.drawLine(x + 4, y + 9, x + 7, y + 12, 2, false);
  renderer.drawLine(x + 7, y + 12, x + 12, y + 5, 2, false);
}

// Progress bar with integer percent (0..100) and configurable line width.
// Identical to the SleepActivity helper extracted from SleepActivity.cpp.
inline void drawProgressBar(const GfxRenderer& renderer, const Rect& rect, const int percent,
                            const int lineWidth = 2) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, lineWidth, true);
  const int innerX = rect.x + lineWidth + 2;
  const int innerY = rect.y + lineWidth + 2;
  const int innerW = std::max(0, rect.width - 2 * (lineWidth + 2));
  const int innerH = std::max(0, rect.height - 2 * (lineWidth + 2));
  const int fillW = std::clamp((innerW * std::clamp(percent, 0, 100) + 50) / 100, 0, innerW);
  if (fillW > 0 && innerH > 0) {
    renderer.fillRect(innerX, innerY, fillW, innerH, true);
  }
}

// Integer percent rounded to nearest (0..100) from value/target.
// Returns 0 if target is zero (avoids divide-by-zero in dashboards).
inline int percentOf(const uint64_t value, const uint64_t target) {
  if (target == 0) return 0;
  return static_cast<int>(std::clamp((value * 100 + target / 2) / target,
                                      static_cast<uint64_t>(0), static_cast<uint64_t>(100)));
}

// Format a percent (0..100) into a string with the trailing "%" sign.
inline std::string formatPercent(const int percent) {
  return std::to_string(std::clamp(percent, 0, 100)) + "%";
}

}  // namespace text_draw