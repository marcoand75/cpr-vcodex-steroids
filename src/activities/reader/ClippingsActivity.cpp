#include "ClippingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
  const int pageItems = getPageItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_CLIPPING_HOLD_MS) {
      confirmDeleteSelectedClipping();
      return;
    }

    if (!clippings.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(clippings.size())) {
      const auto& selected = clippings[selectorIndex];
      setResult(BookmarkResult{selected.spineIndex, selected.startPage});
      finish();
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

  const int totalItems = static_cast<int>(clippings.size());
  if (totalItems == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_NO_CLIPPINGS), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  const char* rawTitle = tr(STR_VIEW_CLIPPINGS);
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, rawTitle, contentWidth - 20);
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title.c_str(), true, EpdFontFamily::BOLD);

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

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
