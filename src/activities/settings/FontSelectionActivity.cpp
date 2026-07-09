#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

static void s_onBack(void* ctx) {
  static_cast<FontSelectionActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<FontSelectionActivity*>(ctx);
  self->handleSelection();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<FontSelectionActivity*>(ctx);
  const int itemCount = static_cast<int>(self->fonts_.size());
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, itemCount);
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, itemCount);
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<FontSelectionActivity*>(ctx);
  const int itemCount = static_cast<int>(self->fonts_.size());
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextPageIndex(self->selectedIndex_, itemCount, self->pageItems_);
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousPageIndex(self->selectedIndex_, itemCount, self->pageItems_);
  }
  self->requestUpdate();
}

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));

  fonts_.push_back({I18N.get(StrId::STR_BOOKERLY), true, CrossPointSettings::BOOKERLY});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, CrossPointSettings::NOTOSANS});
#ifndef OMIT_LEXEND
  fonts_.push_back({I18N.get(StrId::STR_LEXEND), true, CrossPointSettings::LEXEND});
#endif

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  selectedIndex_ = 0;
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        selectedIndex_ = CrossPointSettings::BUILTIN_FONT_COUNT + i;
        break;
      }
    }
  } else {
    selectedIndex_ = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  pageItems_ = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);

  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void FontSelectionActivity::handleSelection() {
  const auto& font = fonts_[selectedIndex_];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  finish();
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);

  ListRenderHelper::drawHeader(renderer, tr(STR_FONT_FAMILY));

  int currentFontIndex = 0;
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        currentFontIndex = CrossPointSettings::BUILTIN_FONT_COUNT + i;
        break;
      }
    }
  } else {
    currentFontIndex = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  ListRenderHelper::drawList(renderer, layout, static_cast<int>(fonts_.size()), selectedIndex_,
                             [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
                             [this, currentFontIndex](int index) -> std::string {
                               return index == currentFontIndex ? tr(STR_SELECTED) : std::string();
                             },
                             true);

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
