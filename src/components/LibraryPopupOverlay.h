#pragma once

#include <GfxRenderer.h>

#include "PanelDrawHelper.h"
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

  int maxPanelH() const {
    int itemCount = static_cast<int>(items.size());
    int visible = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);
    return PanelDrawHelper::kPadY + PanelDrawHelper::kTitleH + 4 + PanelDrawHelper::kPadY + visible * PanelDrawHelper::kRowH + PanelDrawHelper::kPadY;
  }

  void render(GfxRenderer& renderer, int pageWidth, int pageHeight) const {
    int itemCount = static_cast<int>(items.size());
    int visibleRows = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

    auto layout = PanelDrawHelper::calculatePanel(pageWidth, pageHeight, visibleRows);

    // NOTE: intentionally NO full-screen dim here. The popup is drawn on top of
    // the already-rendered library, so the books remain visible behind the
    // panel (a proper overlay, not a white screen).

    PanelDrawHelper::drawBackground(renderer, layout);
    PanelDrawHelper::drawTitle(renderer, layout, title.c_str());
    PanelDrawHelper::drawSeparator(renderer, layout);

    int endIdx = std::min(startIndex + visibleRows, itemCount);
    for (int i = startIndex; i < endIdx; ++i) {
      int rowIndex = i - startIndex;
      PanelDrawHelper::drawRowHighlight(renderer, layout, rowIndex, i == selectedIndex);
      PanelDrawHelper::drawRowIcon(renderer, layout, rowIndex, items[i].icon, items[i].iconW, items[i].iconH,
                                   i == selectedIndex);

      std::string rowLabel = items[i].label;
      if (items[i].selected) {
        rowLabel = "* " + rowLabel;  // active filter/sort marker
      }

      int textX = PanelDrawHelper::getRowTextX(layout);
      if (items[i].icon && items[i].iconW > 0 && items[i].iconH > 0) {
        textX += items[i].iconW + PanelDrawHelper::kIconPad;
      }

      int lh = renderer.getLineHeight(UI_10_FONT_ID);
      int rowY = PanelDrawHelper::getSeparatorY(layout) + PanelDrawHelper::kPadY + rowIndex * PanelDrawHelper::kRowH;
      int textY = rowY + (PanelDrawHelper::kRowH - lh) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, textY, rowLabel.c_str(), i == selectedIndex ? false : true,
                        i == selectedIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }

    PanelDrawHelper::drawScrollArrows(renderer, layout, startIndex > 0, endIdx < itemCount);
  }
};
