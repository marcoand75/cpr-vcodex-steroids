#include "FavoritesOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_FAVORITE_HOLD_MS = 1000;

std::string favoriteTitle(const FavoriteBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }

  const auto slashPos = book.path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? book.path : book.path.substr(slashPos + 1);
  const auto dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}
}  // namespace

void FavoritesOrderActivity::reloadEntries() {
  entries_ = FAVORITES.getBooks();
  moveMode_ = !entries_.empty() && moveMode_;
  selectedIndex_ = ButtonNavigator::clampIndex(selectedIndex_, static_cast<int>(entries_.size()));
}

void FavoritesOrderActivity::moveSelectedEntry(int delta) {
  const int targetIndex = selectedIndex_ + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries_.size()) || targetIndex == selectedIndex_) {
    return;
  }

  if (!FAVORITES.moveBook(selectedIndex_, targetIndex)) {
    return;
  }

  std::swap(entries_[selectedIndex_], entries_[targetIndex]);
  selectedIndex_ = targetIndex;
  requestUpdate();
}

bool FavoritesOrderActivity::handleConfirmHold(unsigned long heldMs) {
  if (heldMs < DELETE_FAVORITE_HOLD_MS) {
    return false;
  }
  confirmDeleteSelectedEntry();
  return true;
}

void FavoritesOrderActivity::confirmDeleteSelectedEntry() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(entries_.size())) {
    return;
  }

  const FavoriteBook selectedEntry = entries_[selectedIndex_];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_FAVORITES),
                                             favoriteTitle(selectedEntry)),
      [this, entryPath = selectedEntry.path](const ActivityResult& result) {
        if (!result.isCancelled) {
          FAVORITES.removeBook(entryPath);
          reloadEntries();
        }
        requestUpdate(true);
      });
}

void FavoritesOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ORDER_FAVORITES), tr(STR_FAVORITES_SORT_DESC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (entries_.empty()) {
    ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_FAVORITES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, listHeight}, static_cast<int>(entries_.size()), selectedIndex_,
                 [this](const int index) { return favoriteTitle(entries_[index]); },
                 [this](const int index) {
                   if (!entries_[index].author.empty()) {
                     return entries_[index].author;
                   }
                   return entries_[index].path;
                 },
                 [](const int) { return UIIcon::Heart; });
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK),
                              moveMode_ ? tr(STR_DONE) : tr(STR_SELECT),
                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}

std::string FavoritesOrderActivity::getEntryTitle(FavoriteBook entry) const {
  return favoriteTitle(entry);
}