#include "ClippingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/PanelDrawHelper.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_CLIPPING_HOLD_MS = 1000;
}

int ClippingsActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

std::string ClippingsActivity::getItemLabel(int index) const {
  const auto& clipping = clippings[index];
  char buffer[64];

  if (!clipping.selectedText.empty()) {
    const std::string truncated = clipping.selectedText.substr(0, 40);
    snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, truncated.c_str());
    return buffer;
  }

  snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, tr(STR_UNNAMED));
  return buffer;
}

void ClippingsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ClippingsActivity::onExit() { Activity::onExit(); }

void ClippingsActivity::confirmDeleteSelectedClipping() {
  if (!onDeleteClipping || selectorIndex < 0 || selectorIndex >= static_cast<int>(clippings.size())) {
    return;
  }

  const auto label = getItemLabel(selectorIndex);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_CLIPPING), label),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (onDeleteClipping(static_cast<size_t>(selectorIndex))) {
          clippings.erase(clippings.begin() + static_cast<std::ptrdiff_t>(selectorIndex));

          if (clippings.empty()) {
            ActivityResult cancelResult;
            cancelResult.isCancelled = true;
            setResult(std::move(cancelResult));
            finish();
            return;
          }

          if (selectorIndex >= static_cast<int>(clippings.size())) {
            selectorIndex = static_cast<int>(clippings.size()) - 1;
          }
        }

        requestUpdate();
      });
}

void ClippingsActivity::loop() {
  const int totalItems = static_cast<int>(clippings.size());

  if (previewOpen) {
    // ---- Preview mode ----
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Single click again: return to the reader positioned on the clipping.
      const auto& selected = clippings[selectorIndex];
      setResult(BookmarkResult{selected.spineIndex, selected.startPage});
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      // Back: close the preview and return to the clippings list.
      previewOpen = false;
      previewLineOffset = 0;
      requestUpdate();
      return;
    }

    // Scroll the full wrapped clipping text.
    buttonNavigator.onNext([this] {
      previewLineOffset++;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      if (previewLineOffset > 0) previewLineOffset--;
      requestUpdate();
    });
    return;
  }

  // ---- List mode ----
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_CLIPPING_HOLD_MS) {
      confirmDeleteSelectedClipping();
      return;
    }

    if (!clippings.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(clippings.size())) {
      // Single click: open the clipping preview panel (does NOT jump to the book).
      previewOpen = true;
      previewLineOffset = 0;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  const int pageItems = getPageItems();
  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void ClippingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (previewOpen) {
    renderPreview();
  } else {
    renderList();
  }
}

void ClippingsActivity::renderList() {
  const int totalItems = static_cast<int>(clippings.size());
  const int pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  // Header title (same as populated list)
  const char* rawTitle = tr(STR_VIEW_CLIPPINGS);
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, rawTitle, contentWidth - 20);
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title.c_str(), true, EpdFontFamily::BOLD);

  if (totalItems == 0) {
    // Empty state: centered message + button hints
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_NO_CLIPPINGS), true, EpdFontFamily::BOLD);
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    renderer.displayBuffer();
    return;
  }

  const int startY = 60 + contentY;
  constexpr int lineHeight = 30;
  const int pageItems = getPageItems();
  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * 30 - 2, contentWidth - 1, lineHeight);

  for (int i = 0; i < pageItems; ++i) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int displayY = 60 + contentY + i * lineHeight;
    const bool isSelected = itemIndex == selectorIndex;
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, getItemLabel(itemIndex).c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, label.c_str(), !isSelected);
  }

  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void ClippingsActivity::renderPreview() {
  if (clippings.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(clippings.size())) {
    renderList();
    return;
  }

  const auto& clipping = clippings[selectorIndex];
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int margin = 20;
  const int pX = margin;
  const int pW = pageWidth - margin * 2;
  const int topPad = 8 + UITheme::getInstance().getMetrics().topPadding;
  const int pY = topPad;
  const int bottomPad = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int pHeight = pageHeight - pY - bottomPad - 12;
  const int textSize = 16;  // inner padding of the panel

  // Wikipedia-style cyberpunk panel: white bg, black border, black text.
  renderer.fillRect(pX, pY, pW, pHeight, 0);
  PanelDrawHelper::drawCyberpunkPanel(renderer, pX, pY, pW, pHeight, false);

  const int textX = pX + textSize;
  const int textW = pW - textSize * 2;

  // Use UI_10/UI_12 (full accent coverage) instead of SMALL_FONT so accented
  // letters and other glyphs render correctly.
  const int headerFont = UI_12_FONT_ID;
  const int bodyFont = UI_12_FONT_ID;
  const int bodyLh = renderer.getLineHeight(bodyFont);

  int y = pY + 12;

  // Header: page position + chapter title (large, bold).
  char head[96];
  if (clipping.chapterTitle[0] != '\0') {
    snprintf(head, sizeof(head), "%s  (p. %d)", clipping.chapterTitle, clipping.startPage + 1);
  } else {
    snprintf(head, sizeof(head), "%s (p. %d)", tr(STR_CLIPPING_PREVIEW), clipping.startPage + 1);
  }
  const int headLh = renderer.getLineHeight(headerFont);
  const std::string headTrunc = renderer.truncatedText(headerFont, head, textW, EpdFontFamily::BOLD);
  renderer.drawText(headerFont, textX, y, headTrunc.c_str(), true, EpdFontFamily::BOLD);
  y += headLh + 6;
  renderer.drawLine(textX, y, textX + textW, y, 1, true);
  y += 10;

  // Reserve the bottom hint area inside the panel (two lines).
  const int hintFont = UI_10_FONT_ID;
  const int hintLh = renderer.getLineHeight(hintFont);
  const int hintAreaHeight = hintLh * 2 + 16;             // 2 lines + padding
  const int hintY = pY + pHeight - hintAreaHeight + 6;    // fully inside the panel
  const int bodyBottom = hintY - 8;                       // body stops above the hints

  // Body: full clipping text, wrapped, scrollable with Up/Down.
  if (clipping.selectedText.empty()) {
    renderer.drawText(bodyFont, textX, y, tr(STR_UNNAMED), true);
  } else {
    int drawLine = 0;
    int lineY = y;
    for (const auto& wl : renderer.wrappedText(bodyFont, clipping.selectedText.c_str(), textW, 128)) {
      if (drawLine++ < previewLineOffset) continue;
      if (lineY + bodyLh > bodyBottom) break;
      renderer.drawText(bodyFont, textX, lineY, wl.c_str(), true);
      lineY += bodyLh + 3;
    }
  }

  // Bottom hints, aligned left, fully inside the panel.
  renderer.drawText(hintFont, textX, hintY, tr(STR_CLIPPING_READ_FULL), true, EpdFontFamily::BOLD);
  renderer.drawText(hintFont, textX, hintY + hintLh, tr(STR_CLIPPING_GO_TO), true);

  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}
