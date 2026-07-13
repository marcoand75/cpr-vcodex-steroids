#pragma once

#include <GfxRenderer.h>

#include "../fontIds.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * Shared panel-style drawing helpers for popups, context menus, and overlays.
 *
 * The visual language mirrors KOReader's rounded white panels: white fill,
 * thin black border, bold title, separator, filled-black row highlight,
 * optional row icons, and scroll chevrons.
 *
 * All dimensions are expressed in logical pixels; the caller is responsible
 * for passing coordinates in the same logical space as the rest of the UI.
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

  static void drawBackground(GfxRenderer& renderer, const PanelLayout& layout) {
    renderer.fillRoundedRect(layout.x, layout.y, layout.width, layout.height, kCornerRadius, Color::White);
    renderer.drawRoundedRect(layout.x, layout.y, layout.width, layout.height, kBorderWidth, kCornerRadius, true);
  }

  static void drawTitle(GfxRenderer& renderer, const PanelLayout& layout, const char* title) {
    int titleX = layout.x + kPadX;
    int titleY = layout.y + kPadY;
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD);
  }

  static void drawSeparator(GfxRenderer& renderer, const PanelLayout& layout) {
    int sepY = layout.y + kPadY + kTitleH + kSeparatorH;
    renderer.drawLine(layout.x + kPadX, sepY, layout.x + layout.width - kPadX, sepY, kSeparatorH, true);
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
