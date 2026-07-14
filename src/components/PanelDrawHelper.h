#pragma once

#include <GfxRenderer.h>

#include "../fontIds.h"
#include "UITheme.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * Shared panel-style drawing helpers for popups, context menus, and overlays.
 *
 * Supports both the classic KOReader rounded-panel style and a cyberpunk
 * angular-panel style (active when the Marcoand75 theme is selected).
 *
 * All dimensions are expressed in logical pixels.
 */
class PanelDrawHelper {
 public:
  struct PanelLayout {
    int x;
    int y;
    int width;
    int height;
    int contentX;
    int contentY;
    int contentWidth;
    int contentHeight;
  };

  struct RowDrawContext {
    int y;
    int height;
    bool selected;
    const char* label;
    const uint8_t* iconPixels;
    int iconW;
    int iconH;
  };

  static constexpr int kCornerRadius = 12;
  static constexpr int kBorderWidth = 2;
  static constexpr int kPadX = 16;
  static constexpr int kPadY = 12;
  static constexpr int kTitleH = 22;
  static constexpr int kRowH = 44;
  static constexpr int kIconPad = 10;
  static constexpr int kSeparatorH = 1;
  static constexpr int kMaxVisibleRows = 8;
  static constexpr int kPanelWPercent = 80;

  static PanelLayout calculatePanel(int pageWidth, int pageHeight, int visibleRows) {
    PanelLayout layout;
    int contentH = kPadY + kTitleH + kSeparatorH + kPadY + visibleRows * kRowH + kPadY;
    int maxH = pageHeight * kPanelWPercent / 100;
    int panelH = std::min(contentH, maxH);
    int panelW = pageWidth * kPanelWPercent / 100;
    layout.x = (pageWidth - panelW) / 2;
    layout.y = (pageHeight - panelH) / 2;
    layout.width = panelW;
    layout.height = panelH;
    layout.contentX = layout.x + kPadX;
    layout.contentY = layout.y + kPadY;
    layout.contentWidth = panelW - 2 * kPadX;
    layout.contentHeight = panelH - 2 * kPadY;
    return layout;
  }

  static void drawAngularPanel(GfxRenderer& r, int x, int y, int w, int h) {
    constexpr int c = 4;
    constexpr int lw = 2;
    if (w < c * 2 || h < c * 2) { r.drawRect(x, y, w, h, lw, true); return; }
    int x2 = x + w, y2 = y + h;
    r.drawLine(x + c, y, x2 - c, y, lw, true);
    r.drawLine(x2, y + c, x2, y2 - c, lw, true);
    r.drawLine(x2 - c, y2, x + c, y2, lw, true);
    r.drawLine(x, y2 - c, x, y + c, lw, true);
    r.drawLine(x, y + c, x + c, y, 1, true);
    r.drawLine(x2 - c, y, x2, y + c, 1, true);
    r.drawLine(x2, y2 - c, x2 - c, y2, 1, true);
    r.drawLine(x + c, y2, x, y2 - c, 1, true);
    // Inner corner accent brackets (cyberpunk style)
    constexpr int bi = 10, bl = 6;
    r.drawLine(x + bi, y + bi, x + bi + bl, y + bi, 1, true);
    r.drawLine(x + bi, y + bi, x + bi, y + bi + bl, 1, true);
    r.drawLine(x + w - bi - bl, y + bi, x + w - bi, y + bi, 1, true);
    r.drawLine(x + w - bi, y + bi, x + w - bi, y + bi + bl, 1, true);
    r.drawLine(x + bi, y + h - bi, x + bi + bl, y + h - bi, 1, true);
    r.drawLine(x + bi, y + h - bi - bl, x + bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi - bl, y + h - bi, x + w - bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi, y + h - bi - bl, x + w - bi, y + h - bi, 1, true);
  }

  static void drawBackground(GfxRenderer& renderer, const PanelLayout& layout) {
    UITheme& theme = UITheme::getInstance();
    if (theme.isMarcoand75()) {
      renderer.fillRect(layout.x, layout.y, layout.width, layout.height, false);
      drawAngularPanel(renderer, layout.x, layout.y, layout.width, layout.height);
    } else {
      renderer.fillRoundedRect(layout.x, layout.y, layout.width, layout.height, kCornerRadius, Color::White);
      renderer.drawRoundedRect(layout.x, layout.y, layout.width, layout.height, kBorderWidth, kCornerRadius, true);
    }
  }

  static void drawTitle(GfxRenderer& renderer, const PanelLayout& layout, const char* title) {
    int titleX = layout.x + kPadX;
    int titleY = layout.y + kPadY;
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD);
  }

  static void drawSeparator(GfxRenderer& renderer, const PanelLayout& layout) {
    int sepY = layout.y + kPadY + kTitleH + kSeparatorH;
    UITheme& theme = UITheme::getInstance();
    if (theme.isMarcoand75()) {
      // Cyberpunk scanline separator
      for (int cx = layout.x + kPadX; cx + 7 < layout.x + layout.width - kPadX; cx += 7) {
        renderer.drawLine(cx, sepY, cx + 4, sepY, 1, true);
        renderer.drawLine(cx + 1, sepY + 2, cx + 3, sepY + 2, 1, true);
      }
      renderer.drawLine(layout.x + kPadX, sepY + 1, layout.x + kPadX, sepY + 2, 1, true);
      renderer.drawLine(layout.x + layout.width - kPadX - 1, sepY + 1, layout.x + layout.width - kPadX - 1, sepY + 2, 1, true);
    } else {
      renderer.drawLine(layout.x + kPadX, sepY, layout.x + layout.width - kPadX, sepY, kSeparatorH, true);
    }
  }

  static int getSeparatorY(const PanelLayout& layout) {
    return layout.y + kPadY + kTitleH + kSeparatorH;
  }

  static void drawRowHighlight(GfxRenderer& renderer, const PanelLayout& layout, int rowIndex, bool selected) {
    int rowY = getSeparatorY(layout) + kPadY + rowIndex * kRowH;
    if (selected) {
      renderer.fillRect(layout.x + kPadX, rowY, layout.contentWidth, kRowH, true);
    }
  }

  static void drawRowIcon(GfxRenderer& renderer, const PanelLayout& layout, int rowIndex, const uint8_t* iconPixels,
                          int iconW, int iconH, bool selected) {
    if (iconPixels == nullptr || iconW <= 0 || iconH <= 0) return;
    int rowY = getSeparatorY(layout) + kPadY + rowIndex * kRowH;
    int iconX = layout.x + kPadX + kIconPad;
    int iconY = rowY + (kRowH - iconH) / 2;
    if (selected) {
      renderer.drawIconInverted(iconPixels, iconX, iconY, iconW, iconH);
    } else {
      renderer.drawIcon(iconPixels, iconX, iconY, iconW, iconH);
    }
  }

  static int getRowTextX(const PanelLayout& layout) {
    return layout.x + kPadX + kIconPad;
  }

  static void drawScrollArrows(GfxRenderer& renderer, const PanelLayout& layout, bool showUp, bool showDown) {
    if (showUp) {
      int arrowY = layout.y + kPadY + kTitleH + kSeparatorH + kPadY + 2;
      renderer.drawLine(layout.x + layout.width / 2 - 6, arrowY + 6, layout.x + layout.width / 2, arrowY, 2, true);
      renderer.drawLine(layout.x + layout.width / 2, arrowY, layout.x + layout.width / 2 + 6, arrowY + 6, 2, true);
    }
    if (showDown) {
      int arrowY = layout.y + layout.height - kPadY - 8;
      renderer.drawLine(layout.x + layout.width / 2 - 6, arrowY, layout.x + layout.width / 2, arrowY + 6, 2, true);
      renderer.drawLine(layout.x + layout.width / 2, arrowY + 6, layout.x + layout.width / 2 + 6, arrowY, 2, true);
    }
  }
};
