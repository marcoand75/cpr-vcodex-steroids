#pragma once

#include <GfxRenderer.h>

#include "../fontIds.h"

#include <functional>
#include <string>
#include <vector>

/**
 * Reusable popup overlay for displaying scrollable selection lists.
 * Used by LibraryActivity for Sort/Filter popups.
 *
 * The visual style mirrors the homepage carousel book popup
 * (BookContextMenuActivity): a white rounded panel with a bold header/title,
 * an inverted (filled) highlight for the focused row, optional row icons, and
 * scroll indicators — so the whole UI feels consistent.
 *
 * The popup is drawn ON TOP OF the library screen (the caller renders the
 * library first, then this overlay), so the books stay visible behind the
 * panel rather than on a blank screen.
 *
 * Usage:
 *   LibraryPopupOverlay popup;
 *   popup.title = "Sort by";
 *   popup.items = {{"Title A→Z", false, Text24Icon, 24, 24}, ...};
 *   popup.selectedIndex = 0;
 *   popup.render(renderer, pageWidth, pageHeight);
 */

struct PopupItem {
  std::string label;
  bool selected = false;           // true if currently active (checkmark)
  const uint8_t* icon = nullptr;   // optional bitmap; MUST be drawn at iconW×iconH
  int iconW = 0;                   // native bitmap width  (0 = no icon)
  int iconH = 0;                   // native bitmap height (0 = no icon)
};

class LibraryPopupOverlay {
 public:
  std::string title;
  std::vector<PopupItem> items;
  int selectedIndex = 0;
  int startIndex = 0;  // scroll offset

  static constexpr int kRowH = 44;
  static constexpr int kTitleH = 22;
  static constexpr int kPadX = 16;
  static constexpr int kPadY = 12;
  static constexpr int kIconPad = 10;
  static constexpr int kCornerRadius = 12;
  static constexpr int kPanelWPercent = 80;  // matches homepage carousel popup
  static constexpr int kMaxVisibleRows = 8;

  int maxPanelH() const {
    int itemCount = static_cast<int>(items.size());
    int visible = std::min(itemCount, kMaxVisibleRows);
    return kPadY + kTitleH + 4 + kPadY + visible * kRowH + kPadY;
  }

  void render(GfxRenderer& renderer, int pageWidth, int pageHeight) const {
    int itemCount = static_cast<int>(items.size());
    int visibleRows = std::min(itemCount, kMaxVisibleRows);

    int panelW = pageWidth * kPanelWPercent / 100;
    int contentH = kPadY + kTitleH + 4 + kPadY + visibleRows * kRowH + kPadY;
    int maxH = pageHeight * kPanelWPercent / 100;
    int panelH = std::min(contentH, maxH);
    int panelX = (pageWidth - panelW) / 2;
    int panelY = (pageHeight - panelH) / 2;

    // NOTE: intentionally NO full-screen dim here. The popup is drawn on top of
    // the already-rendered library, so the books remain visible behind the
    // panel (a proper overlay, not a white screen).

    // Panel background (white fill + black border) — same look as the
    // homepage carousel book popup.
    renderer.fillRoundedRect(panelX, panelY, panelW, panelH, kCornerRadius, Color::White);
    renderer.drawRoundedRect(panelX, panelY, panelW, panelH, 2, kCornerRadius, true);

    // Title (bold), top-left.
    int titleX = panelX + kPadX;
    int titleY = panelY + kPadY;
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

    // Separator line under the title.
    int sepY = titleY + kTitleH + 4;
    renderer.drawLine(panelX + kPadX, sepY, panelX + panelW - kPadX, sepY, 1, true);

    // Items.
    int listY = sepY + kPadY;
    int endIdx = std::min(startIndex + visibleRows, itemCount);
    for (int i = startIndex; i < endIdx; ++i) {
      int rowY = listY + (i - startIndex) * kRowH;
      bool sel = (i == selectedIndex);

      // Highlight (filled black) for the focused row.
      if (sel) {
        renderer.fillRect(panelX + kPadX, rowY, panelW - 2 * kPadX, kRowH, true);
      }

      int textX = panelX + kPadX + kIconPad;
      // Optional row icon — drawn at its NATIVE bitmap size so 32px icons are
      // not corrupted by being blitted into a 24px box.
      if (items[i].icon && items[i].iconW > 0 && items[i].iconH > 0) {
        const int iw = items[i].iconW;
        const int ih = items[i].iconH;
        int iconX = panelX + kPadX + kIconPad;
        int iconY = rowY + (kRowH - ih) / 2;
        if (sel) {
          renderer.drawIconInverted(items[i].icon, iconX, iconY, iw, ih);
        } else {
          renderer.drawIcon(items[i].icon, iconX, iconY, iw, ih);
        }
        textX = iconX + iw + kIconPad;
      }

      std::string rowLabel = items[i].label;
      if (items[i].selected) {
        rowLabel = "* " + rowLabel;  // active filter/sort marker
      }

      int lh = renderer.getLineHeight(UI_10_FONT_ID);
      int textY = rowY + (kRowH - lh) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, textY, rowLabel.c_str(), sel ? false : true,
                        sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }

    // Scroll indicators.
    if (startIndex > 0) {
      int arrowY = panelY + kTitleH + kPadY + 2;
      renderer.drawLine(panelX + panelW / 2 - 6, arrowY + 6, panelX + panelW / 2, arrowY, 2, true);
      renderer.drawLine(panelX + panelW / 2, arrowY, panelX + panelW / 2 + 6, arrowY + 6, 2, true);
    }
    if (endIdx < itemCount) {
      int arrowY = panelY + panelH - kPadY - 8;
      renderer.drawLine(panelX + panelW / 2 - 6, arrowY, panelX + panelW / 2, arrowY + 6, 2, true);
      renderer.drawLine(panelX + panelW / 2, arrowY + 6, panelX + panelW / 2 + 6, arrowY, 2, true);
    }
  }
};
