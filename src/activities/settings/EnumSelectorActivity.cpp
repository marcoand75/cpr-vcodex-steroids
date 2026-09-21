#include "EnumSelectorActivity.h"

#include <I18n.h>

#include <algorithm>

#include "components/PanelDrawHelper.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

void EnumSelectorActivity::onEnter() {
    const int safeIndex = std::min<int>(static_cast<int>(currentValue), static_cast<int>(optionLabels.size()) - 1);
    selectedIndex = safeIndex;
    startIndex = 0;
    Activity::onEnter();
    requestUpdate();
}

void EnumSelectorActivity::onExit() {}

void EnumSelectorActivity::loop() {
    const int itemCount = static_cast<int>(optionLabels.size());
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

void EnumSelectorActivity::confirmSelection() {
    PageResult result;
    result.page = static_cast<uint32_t>(selectedIndex);
    setResult(result);
    finish();
}

void EnumSelectorActivity::cancelSelection() {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
}

void EnumSelectorActivity::render(RenderLock&&) {
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();

    const int itemCount = static_cast<int>(optionLabels.size());
    const int visibleRows = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

    auto layout = PanelDrawHelper::calculatePanel(pageWidth, pageHeight, visibleRows);

    PanelDrawHelper::drawBackground(renderer, layout);

    // Title — use the currently selected item's label
    const int safeIdx = std::min<int>(selectedIndex, itemCount - 1);
    PanelDrawHelper::drawTitle(renderer, layout, I18N.get(optionLabels[safeIdx]));

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

        const char* label = I18N.get(optionLabels[i]);
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