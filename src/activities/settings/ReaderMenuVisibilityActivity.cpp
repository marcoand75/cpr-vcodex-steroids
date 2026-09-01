#include "ReaderMenuVisibilityActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/ButtonNavigator.h"

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
       selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(entries.size()));
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
   requestUpdate();

   listInputMapper.setBackHandler([](void* ctx) {
     auto* self = static_cast<ReaderMenuVisibilityActivity*>(ctx);
     self->finish();
   }, this, false);

   listInputMapper.setConfirmHandler([](void* ctx) {
     auto* self = static_cast<ReaderMenuVisibilityActivity*>(ctx);
     self->toggleSelectedEntry();
   }, this, false);

   auto onNav = [](void* ctx, int delta) {
     auto* self = static_cast<ReaderMenuVisibilityActivity*>(ctx);
     if (self->entries.empty()) {
         return;
     }
     if (delta > 0) {
         self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
     } else {
         self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, static_cast<int>(self->entries.size()));
     }
     self->requestUpdate();
   };

   listInputMapper.setNavReleaseAndContinuous(onNav, onNav, this);
}

void ReaderMenuVisibilityActivity::loop() {
   listInputMapper.loop(mappedInput);
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
     ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_ENTRIES));
   } else {
     GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
                  [this](const int index) { return std::string(I18N.get(entries[index]->nameId)); }, nullptr, nullptr,
                  [this](const int index) { return std::string(getVisibilityLabel(*entries[index])); }, true);
   }

   ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

   renderer.displayBuffer();
}
