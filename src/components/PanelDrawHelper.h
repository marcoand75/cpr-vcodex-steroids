#pragma once

#include <GfxRenderer.h>
#include <algorithm>

#include "../fontIds.h"
#include "UITheme.h"

#include <cstdint>

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

  /**
   * Unified cyberpunk-style panel border: chamfered outer rect, corner accent
   * brackets, and inner corner brackets. Used by the Marcoand75 theme throughout
   * homepage stats, icon bar, carousel, and popup overlays.
   *
   * @param sel If true draws a thicker border + inner outline for selected state.
   */
  static void drawCyberpunkPanel(const GfxRenderer& r, int x, int y, int w, int h, bool sel = false) {
    constexpr int c = 6;       // more pronounced chamfer
    constexpr int lw_out = 2;  // outer line width when sel
    constexpr int lw_in = 1;   // inner line width when not sel
    const int lw = sel ? lw_out : lw_in;
    if (w < c * 2 || h < c * 2) { r.drawRect(x, y, w, h, lw, true); return; }
    int x2 = x + w, y2 = y + h;
    
    // Outer chamfered border
    r.drawLine(x + c, y, x2 - c, y, lw, true);
    r.drawLine(x2, y + c, x2, y2 - c, lw, true);
    r.drawLine(x2 - c, y2, x + c, y2, lw, true);
    r.drawLine(x, y2 - c, x, y + c, lw, true);
    // Chamfer corners
    r.drawLine(x, y + c, x + c, y, 1, true);
    r.drawLine(x2 - c, y, x2, y + c, 1, true);
    r.drawLine(x2, y2 - c, x2 - c, y2, 1, true);
    r.drawLine(x + c, y2, x, y2 - c, 1, true);
    
    // Selection: inner outline with matching chamfer
    if (sel) {
      int si = 4;
      r.drawLine(x + si + c, y + si, x2 - si - c, y + si, 1, true);
      r.drawLine(x2 - si, y + si + c, x2 - si, y2 - si - c, 1, true);
      r.drawLine(x2 - si - c, y2 - si, x + si + c, y2 - si, 1, true);
      r.drawLine(x + si, y2 - si - c, x + si, y + si + c, 1, true);
      // Inner chamfer corners
      r.drawLine(x + si, y + si + c, x + si + c, y + si, 1, true);
      r.drawLine(x2 - si - c, y + si, x2 - si, y + si + c, 1, true);
      r.drawLine(x2 - si, y2 - si - c, x2 - si - c, y2 - si, 1, true);
      r.drawLine(x + si + c, y2 - si, x + si, y2 - si - c, 1, true);
    }
    
    // Outer corner accent brackets (moved closer to physical corners)
    constexpr int cg = 1, cl = 8;
    // Top-left
    r.drawLine(x + cg, y + cg, x + cg + cl, y + cg, 1, true);
    r.drawLine(x + cg, y + cg, x + cg, y + cg + cl, 1, true);
    r.drawLine(x + cg + 2, y + cg + 2, x + cg + 4, y + cg + 2, 1, true);
    // Top-right
    r.drawLine(x + w - cg - cl, y + cg, x + w - cg, y + cg, 1, true);
    r.drawLine(x + w - cg, y + cg, x + w - cg, y + cg + cl, 1, true);
    r.drawLine(x + w - cg - 4, y + cg + 2, x + w - cg - 2, y + cg + 2, 1, true);
    // Bottom-left
    r.drawLine(x + cg, y + h - cg, x + cg + cl, y + h - cg, 1, true);
    r.drawLine(x + cg, y + h - cg - cl, x + cg, y + h - cg, 1, true);
    r.drawLine(x + cg + 2, y + h - cg - 2, x + cg + 4, y + h - cg - 2, 1, true);
    // Bottom-right
    r.drawLine(x + w - cg - cl, y + h - cg, x + w - cg, y + h - cg, 1, true);
    r.drawLine(x + w - cg, y + h - cg - cl, x + w - cg, y + h - cg, 1, true);
    r.drawLine(x + w - cg - 4, y + h - cg - 2, x + w - cg - 2, y + h - cg - 2, 1, true);
    
    // Inner corner brackets (moved closer to the outer chamfered edge)
    constexpr int bi = 8, bl = 8;
    // Top-left
    r.drawLine(x + bi, y + bi, x + bi + bl, y + bi, 1, true);
    r.drawLine(x + bi, y + bi, x + bi, y + bi + bl, 1, true);
    r.drawLine(x + bi + 1, y + bi + 1, x + bi + 2, y + bi + 1, 1, true); // tiny tech dash
    // Top-right
    r.drawLine(x + w - bi - bl, y + bi, x + w - bi, y + bi, 1, true);
    r.drawLine(x + w - bi, y + bi, x + w - bi, y + bi + bl, 1, true);
    r.drawLine(x + w - bi - 2, y + bi + 1, x + w - bi - 1, y + bi + 1, 1, true);
    // Bottom-left
    r.drawLine(x + bi, y + h - bi, x + bi + bl, y + h - bi, 1, true);
    r.drawLine(x + bi, y + h - bi - bl, x + bi, y + h - bi, 1, true);
    r.drawLine(x + bi + 1, y + h - bi - 1, x + bi + 2, y + h - bi - 1, 1, true);
    // Bottom-right
    r.drawLine(x + w - bi - bl, y + h - bi, x + w - bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi, y + h - bi - bl, x + w - bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi - 2, y + h - bi - 1, x + w - bi - 1, y + h - bi - 1, 1, true);
    
    // Decorative edge ticks (cyberpunk "ruler" or "circuit" marks)
    if (w > 40 && h > 40) {
        // Top edge ticks
        r.drawLine(x + w / 2 - 4, y + 1, x + w / 2 - 4, y + 3, 1, true);
        r.drawLine(x + w / 2, y + 1, x + w / 2, y + 4, 1, true);
        r.drawLine(x + w / 2 + 4, y + 1, x + w / 2 + 4, y + 3, 1, true);
        // Bottom edge ticks
        r.drawLine(x + w / 2 - 4, y2 - 3, x + w / 2 - 4, y2 - 1, 1, true);
        r.drawLine(x + w / 2, y2 - 4, x + w / 2, y2 - 1, 1, true);
        r.drawLine(x + w / 2 + 4, y2 - 3, x + w / 2 + 4, y2 - 1, 1, true);
    }
  }

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
    constexpr int c = 6;
    constexpr int lw = 2;
    if (w < c * 2 || h < c * 2) { r.drawRect(x, y, w, h, lw, true); return; }
    int x2 = x + w, y2 = y + h;
    
    // Main chamfered border
    r.drawLine(x + c, y, x2 - c, y, lw, true);
    r.drawLine(x2, y + c, x2, y2 - c, lw, true);
    r.drawLine(x2 - c, y2, x + c, y2, lw, true);
    r.drawLine(x, y2 - c, x, y + c, lw, true);
    // Chamfer corners
    r.drawLine(x, y + c, x + c, y, 1, true);
    r.drawLine(x2 - c, y, x2, y + c, 1, true);
    r.drawLine(x2, y2 - c, x2 - c, y2, 1, true);
    r.drawLine(x + c, y2, x, y2 - c, 1, true);

    // Inner corner accent brackets (moved closer to outer corners)
    constexpr int bi = 8, bl = 8;
    r.drawLine(x + bi, y + bi, x + bi + bl, y + bi, 1, true);
    r.drawLine(x + bi, y + bi, x + bi, y + bi + bl, 1, true);
    r.drawLine(x + w - bi - bl, y + bi, x + w - bi, y + bi, 1, true);
    r.drawLine(x + w - bi, y + bi, x + w - bi, y + bi + bl, 1, true);
    r.drawLine(x + bi, y + h - bi, x + bi + bl, y + h - bi, 1, true);
    r.drawLine(x + bi, y + h - bi - bl, x + bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi - bl, y + h - bi, x + w - bi, y + h - bi, 1, true);
    r.drawLine(x + w - bi, y + h - bi - bl, x + w - bi, y + h - bi, 1, true);
    
    // Decorative center ticks
    if (w > 40) {
        r.drawLine(x + w / 2, y + 2, x + w / 2, y + 5, 1, true);
        r.drawLine(x + w / 2, y2 - 5, x + w / 2, y2 - 2, 1, true);
    }
  }

  static void drawBackground(GfxRenderer& renderer, const PanelLayout& layout) {
    UITheme& theme = UITheme::getInstance();
    if (theme.isMarcoand75()) {
      renderer.fillRect(layout.x, layout.y, layout.width, layout.height, false);
      drawCyberpunkPanel(renderer, layout.x, layout.y, layout.width, layout.height);
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
      // Cyberpunk data-bus separator
      int startX = layout.x + kPadX;
      int endX = layout.x + layout.width - kPadX;
      for (int cx = startX; cx + 8 < endX; cx += 8) {
        renderer.drawLine(cx, sepY, cx + 5, sepY, 1, true);
        renderer.drawLine(cx + 6, sepY, cx + 6, sepY + 1, 1, true); // vertical tick
        renderer.drawLine(cx + 1, sepY + 2, cx + 4, sepY + 2, 1, true);
      }
      // End caps
      renderer.drawLine(startX, sepY + 1, startX, sepY + 2, 1, true);
      renderer.drawLine(endX - 1, sepY + 1, endX - 1, sepY + 2, 1, true);
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
    int cx = layout.x + layout.width / 2;
    if (showUp) {
      int arrowY = layout.y + kPadY + kTitleH + kSeparatorH + kPadY + 2;
      // Outer chevron
      renderer.drawLine(cx - 7, arrowY + 6, cx, arrowY, 2, true);
      renderer.drawLine(cx, arrowY, cx + 7, arrowY + 6, 2, true);
      // Inner detail
      renderer.drawLine(cx - 3, arrowY + 4, cx, arrowY + 2, 1, true);
      renderer.drawLine(cx, arrowY + 2, cx + 3, arrowY + 4, 1, true);
    }
    if (showDown) {
      int arrowY = layout.y + layout.height - kPadY - 8;
      // Outer chevron
      renderer.drawLine(cx - 7, arrowY, cx, arrowY + 6, 2, true);
      renderer.drawLine(cx, arrowY + 6, cx + 7, arrowY, 2, true);
      // Inner detail
      renderer.drawLine(cx - 3, arrowY + 2, cx, arrowY + 4, 1, true);
      renderer.drawLine(cx, arrowY + 4, cx + 3, arrowY + 2, 1, true);
    }
  }
};