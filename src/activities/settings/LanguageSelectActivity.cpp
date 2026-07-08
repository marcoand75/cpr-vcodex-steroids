#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "UiFontSelection.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

static void s_onBack(void* ctx) {
  static_cast<LanguageSelectActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<LanguageSelectActivity*>(ctx);
  self->handleSelection();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<LanguageSelectActivity*>(ctx);
  const int total = static_cast<int>(self->totalItems);
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, total);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, total);
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<LanguageSelectActivity*>(ctx);
  const int total = static_cast<int>(self->totalItems);
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextPageIndex(self->selectedIndex, total, self->pageItems);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousPageIndex(self->selectedIndex, total, self->pageItems);
  }
  self->requestUpdate();
}

void LanguageSelectActivity::onEnter() {
  Activity::onEnter();
  useLanguageSelectionUiFonts();

  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  selectedIndex = (it != end) ? static_cast<int>(std::distance(begin, it)) : 0;

  pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);

  requestUpdate();
}

void LanguageSelectActivity::onExit() {
  refreshUiFontsForCurrentLanguage();
  Activity::onExit();
}

void LanguageSelectActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void LanguageSelectActivity::handleSelection() {
  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(SORTED_LANGUAGE_INDICES[selectedIndex]));
    refreshUiFontsForCurrentLanguage();
  }

  finish();
}

void LanguageSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());

  ListRenderHelper::drawHeader(renderer, tr(STR_LANGUAGE));
  ListRenderHelper::drawList(renderer, layout, static_cast<int>(totalItems), selectedIndex,
                             [this](int index) {
                               return I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[index]));
                             },
                             nullptr, nullptr,
                             [this, currentLang](int index) {
                               return SORTED_LANGUAGE_INDICES[index] == currentLang ? tr(STR_SELECTED) : std::string();
                             },
                             true);

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
