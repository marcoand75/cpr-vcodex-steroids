#include "FavoritesAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FavoritesBrowserActivity.h"
#include "FavoritesOrderActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ACTION_COUNT = 2;
}  // namespace

void FavoritesAppActivity::refreshEntries() {
  favoriteCount = static_cast<int>(FAVORITES.getBooks().size());
  selectedIndex = std::clamp(selectedIndex, 0, ACTION_COUNT - 1);
}

void FavoritesAppActivity::openSelectedEntry() {
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<FavoritesBrowserActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             refreshEntries();
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<FavoritesOrderActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             refreshEntries();
                             requestUpdate();
                           });
    return;
  }
}

void FavoritesAppActivity::onBack(void* ctx) {
  static_cast<FavoritesAppActivity*>(ctx)->finish();
}

void FavoritesAppActivity::onConfirm(void* ctx) {
  static_cast<FavoritesAppActivity*>(ctx)->openSelectedEntry();
}

void FavoritesAppActivity::releaseNav(void* ctx, int delta) {
  auto* self = static_cast<FavoritesAppActivity*>(ctx);
  if (delta > 0) self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, ACTION_COUNT);
  else if (delta < 0) self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, ACTION_COUNT);
  self->requestUpdate();
}

void FavoritesAppActivity::continuousNav(void* ctx, int delta) {
  auto* self = static_cast<FavoritesAppActivity*>(ctx);
  const int pageItems = UITheme::getNumberOfItemsPerPage(self->renderer, true, false, true, true);
  if (delta > 0) self->selectedIndex = ButtonNavigator::nextPageIndex(self->selectedIndex, ACTION_COUNT, pageItems);
  else if (delta < 0) self->selectedIndex = ButtonNavigator::previousPageIndex(self->selectedIndex, ACTION_COUNT, pageItems);
  self->requestUpdate();
}

void FavoritesAppActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();

  listInputMapper.setBackHandler(onBack, this, true);
  listInputMapper.setConfirmHandler(onConfirm, this, true);
  listInputMapper.setNavReleaseAndContinuous(releaseNav, continuousNav, this);

  requestUpdate();
}

void FavoritesAppActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void FavoritesAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, true, metrics.verticalSpacing);
  const std::string headerSubtitle = std::to_string(favoriteCount);

  ListRenderHelper::drawHeader(renderer, tr(STR_FAVORITES), headerSubtitle.c_str(), true);

  ListRenderHelper::drawList(
      renderer, layout, ACTION_COUNT, selectedIndex,
      [this](const int index) {
        if (index == 0) return std::string(tr(STR_BROWSE_FILES));
        if (index == 1) return std::string(tr(STR_ORDER_FAVORITES));
        return std::string();
      },
      [this](const int index) {
        if (index == 0) return std::string(tr(STR_FAVORITES_BROWSER_DESC));
        if (index == 1) return std::string(tr(STR_FAVORITES_SORT_DESC));
        return std::string();
      },
      [this](const int index) {
        if (index == 0) return UIIcon::Folder;
        if (index == 1) return UIIcon::Settings;
        return UIIcon::Heart;
      });

  if (favoriteCount == 0) {
    renderer.drawCenteredText(SMALL_FONT_ID, layout.contentTop + layout.contentHeight - 14, tr(STR_NO_FAVORITES));
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
