#include "ReaderMenuVisibilityActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
    const char* getVisibilityLabel(const ReaderMenuItemDefinition& definition) {
       return getReaderMenuItemVisibility(definition.id, SETTINGS) ? tr(STR_SHOW) : tr(STR_HIDDEN);
    }
}

void ReaderMenuVisibilityActivity::reloadEntries() {
   entries.clear();
   const auto& definitions = getReaderMenuDefinitions();
   entries.reserve(definitions.size());
   for (const auto& definition : definitions) {
       // No always-visible items for reader menu
       entries.push_back(&definition);
   }

   // Sort by name (alphabetical)
   std::stable_sort(entries.begin(), entries.end(), [](const ReaderMenuItemDefinition* lhs, const ReaderMenuItemDefinition* rhs) {
       return std::string(I18N.get(lhs->nameId)) < std::string(I18N.get(rhs->nameId));
   });

   if (entries.empty()) {
       selectedIndex = 0;
   } else {
       selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(entries.size()) - 1);
   }
}

void ReaderMenuVisibilityActivity::toggleSelectedEntry() {
   if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
       return;
   }

   auto& mask = getReaderMenuVisibilityMaskRef(SETTINGS);
   const size_t index = static_cast<size_t>(entries[selectedIndex]->id);
   const bool currentlyVisible = (mask & (1u << index)) != 0;
   if (currentlyVisible) {
       mask &= ~(1u << index); // hide
   } else {
       mask |= (1u << index); // show
   }
   requestUpdate();
}

void ReaderMenuVisibilityActivity::onEnter() {
   Activity::onEnter();
   reloadEntries();
   waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
   requestUpdate();
}

void ReaderMenuVisibilityActivity::loop() {
   if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
       finish();
       return;
   }

   if (waitForConfirmRelease) {
       if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
           waitForConfirmRelease = false;
       }
       return;
   }

   if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
       toggleSelectedEntry();
       return;
   }

   buttonNavigator.onNextRelease([this] {
       if (entries.empty()) {
           return;
       }
       selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
       requestUpdate();
   });

   buttonNavigator.onPreviousRelease([this] {
       if (entries.empty()) {
           return;
       }
       selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
       requestUpdate();
   });
}

void ReaderMenuVisibilityActivity::render(RenderLock&&) {
   renderer.clearScreen();

   const auto& metrics = UITheme::getInstance().getMetrics();
   const auto pageWidth = renderer.getScreenWidth();
   const auto pageHeight = renderer.getScreenHeight();

   GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READER_MENU_VISIBILITY));

   const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
   const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

   if (entries.empty()) {
     renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
   } else {
     GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
                  [this](const int index) { return std::string(I18N.get(entries[index]->nameId)); }, nullptr, nullptr,
                  [this](const int index) { return std::string(getVisibilityLabel(*entries[index])); }, true);
   }

   const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
   GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

   renderer.displayBuffer();
}