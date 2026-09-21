#include "ButtonActionSelectorActivity.h"

#include <I18n.h>

#include <algorithm>

#include "components/PanelDrawHelper.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

static const std::vector<StrId> kButtonActionLabels = {
    StrId::STR_BTN_ACTION_OFF,
    StrId::STR_BTN_ACTION_ADD_CLIPPING,
    StrId::STR_BTN_ACTION_VIEW_CLIPPINGS,
    StrId::STR_BTN_ACTION_TOGGLE_BOOKMARK,
    StrId::STR_BTN_ACTION_VIEW_BOOKMARKS,
    StrId::STR_BTN_ACTION_LOOKUP_WORD,
    StrId::STR_BTN_ACTION_DICTIONARY,
    StrId::STR_BTN_ACTION_CHAPTER_SKIP,
    StrId::STR_BTN_ACTION_ORIENTATION,
    StrId::STR_BTN_ACTION_FONTSIZE,
    StrId::STR_BTN_ACTION_DARK_MODE,
    StrId::STR_BTN_ACTION_FULL_REFRESH,
    StrId::STR_BTN_ACTION_READER_SETTINGS,
    StrId::STR_BTN_ACTION_READING_TIME,
};

static const std::vector<StrId> kShortPwrBtnLabels = {
    StrId::STR_IGNORE,
    StrId::STR_SLEEP,
    StrId::STR_PAGE_TURN,
    StrId::STR_FORCE_REFRESH,
    StrId::STR_TOGGLE_STATUS_BAR,
    StrId::STR_BTN_ACTION_OFF,
    StrId::STR_BTN_ACTION_ADD_CLIPPING,
    StrId::STR_BTN_ACTION_VIEW_CLIPPINGS,
    StrId::STR_BTN_ACTION_TOGGLE_BOOKMARK,
    StrId::STR_BTN_ACTION_VIEW_BOOKMARKS,
    StrId::STR_BTN_ACTION_LOOKUP_WORD,
    StrId::STR_BTN_ACTION_DICTIONARY,
    StrId::STR_BTN_ACTION_CHAPTER_SKIP,
    StrId::STR_BTN_ACTION_ORIENTATION,
    StrId::STR_BTN_ACTION_DARK_MODE,
    StrId::STR_BTN_ACTION_READER_SETTINGS,
};

StrId ButtonActionSelectorActivity::actionToLabelId(CrossPointSettings::BUTTON_ACTION action) {
  const int idx = static_cast<int>(action);
  if (idx >= 0 && idx < static_cast<int>(kButtonActionLabels.size())) {
    return kButtonActionLabels[idx];
  }
  return StrId::STR_BTN_ACTION_OFF;
}

const std::vector<StrId>& ButtonActionSelectorActivity::getOptionLabels() const {
  if (mode == Mode::SHORT_PWRBTN) {
    return kShortPwrBtnLabels;
  }
  return kButtonActionLabels;
}

void ButtonActionSelectorActivity::onEnter() {
  const auto labels = getOptionLabels();
  const int safeIndex = std::min<int>(static_cast<int>(currentValue), static_cast<int>(labels.size()) - 1);
  selectedIndex = safeIndex;
  startIndex = 0;
  Activity::onEnter();
  requestUpdate();
}

void ButtonActionSelectorActivity::onExit() {}

void ButtonActionSelectorActivity::loop() {
  const auto& labels = getOptionLabels();
  const int itemCount = static_cast<int>(labels.size());
  const int itemsPerPage = PanelDrawHelper::kMaxVisibleRows;

  // Short press (release): single-item step with circular wrap-around
  buttonNavigator.onNextRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  // Long press (continuous): page-based jump with circular wrap-around
  buttonNavigator.onNextContinuous([this, itemCount, itemsPerPage] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, itemsPerPage);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, itemCount, itemsPerPage] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, itemsPerPage);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmSelection();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelSelection();
    return;
  }
}

void ButtonActionSelectorActivity::confirmSelection() {
  PageResult result;
  result.page = static_cast<uint32_t>(selectedIndex);
  setResult(result);
  finish();
}

void ButtonActionSelectorActivity::cancelSelection() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ButtonActionSelectorActivity::render(RenderLock&&) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const auto& labels = getOptionLabels();
  const int itemCount = static_cast<int>(labels.size());
  const int visibleRows = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

  auto layout = PanelDrawHelper::calculatePanel(pageWidth, pageHeight, visibleRows);

  PanelDrawHelper::drawBackground(renderer, layout);

  // Title — use the currently selected item's label as context
  if (mode == Mode::SHORT_PWRBTN) {
    const int safeIdx = std::min<int>(selectedIndex, itemCount - 1);
    PanelDrawHelper::drawTitle(renderer, layout, I18N.get(labels[safeIdx]));
  } else {
    PanelDrawHelper::drawTitle(renderer, layout, I18N.get(actionToLabelId(
        static_cast<CrossPointSettings::BUTTON_ACTION>(selectedIndex))));
  }

  PanelDrawHelper::drawSeparator(renderer, layout);

  // Keep selection visible: scroll startIndex if needed
  int startIdx = startIndex;
  if (selectedIndex < startIdx) {
    startIdx = selectedIndex;
  } else if (selectedIndex >= startIdx + PanelDrawHelper::kMaxVisibleRows) {
    startIdx = selectedIndex - PanelDrawHelper::kMaxVisibleRows + 1;
  }
  startIndex = startIdx;  // persist for next render
  const int endIdx = startIdx + visibleRows;

  for (int i = startIdx; i < endIdx; ++i) {
    const int rowIndex = i - startIdx;
    const bool isSelected = (i == selectedIndex);

    PanelDrawHelper::drawRowHighlight(renderer, layout, rowIndex, isSelected);

    const char* label = I18N.get(labels[i]);
    const int textX = PanelDrawHelper::getRowTextX(layout);
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowY = PanelDrawHelper::getSeparatorY(layout) + PanelDrawHelper::kPadY +
                     rowIndex * PanelDrawHelper::kRowH;
    const int textY = rowY + (PanelDrawHelper::kRowH - lh) / 2;

    renderer.drawText(UI_10_FONT_ID, textX, textY, label,
                      !isSelected,  // invert: false=black-on-white(highlighted), true=white-on-black
                      isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  PanelDrawHelper::drawScrollArrows(renderer, layout, startIdx > 0, endIdx < itemCount);

  renderer.displayBuffer();
}
