#include "BookContextMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/heart.h"
#include "components/icons/trophy.h"
#include "components/icons/readingstats.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/image.h"
#include "fontIds.h"

namespace {
// Visual constants mirror LibraryPopupOverlay so the two popups share the
// same look: a white rounded panel with an inline bold title, separator line,
// inverted highlight on the focused row, native-size row icons, and scroll
// indicators.
constexpr int kCornerRadius = 12;
constexpr int kRowH = 44;
constexpr int kPadX = 16;
constexpr int kPadY = 12;
constexpr int kIconPad = 10;
constexpr int kTitleH = 22;
constexpr int kPanelWPercent = 80;
constexpr int kMaxVisibleRows = 8;
}  // namespace

BookContextMenuActivity::BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::string& bookTitle, const bool isFavorite,
                                                 const bool isCompleted, const bool isEpubFormat, const bool isLibraryMode)
    : Activity("BookContextMenu", renderer, mappedInput),
      menuItems(buildMenuItems(isFavorite, isCompleted, isEpubFormat, isLibraryMode)),
      bookTitle(bookTitle) {}

std::vector<BookContextMenuActivity::MenuItem> BookContextMenuActivity::buildMenuItems(const bool isFavorite,
                                                                                       const bool isCompleted,
                                                                                       const bool isEpubFormat,
                                                                                       const bool isLibraryMode) {
  std::vector<MenuItem> items;
  items.reserve(isLibraryMode ? 15 : 8);
  // All icons use their native 32×32 bitmaps to match the LibraryPopupOverlay style.
  // Icon choices mirror the semantic conventions of the library sort/filter popups.
  items.push_back({MenuAction::OPEN_BOOK, StrId::STR_OPEN, BookIcon, 32, 32});
  if (!isLibraryMode) {
    items.push_back({MenuAction::REMOVE_FROM_RECENTS, StrId::STR_DELETE_FROM_RECENTS, RecentIcon, 32, 32});
  }
  items.push_back({MenuAction::VIEW_STATS, StrId::STR_READING_STATS, ReadingStatsIcon32, 32, 32});
  items.push_back({MenuAction::VIEW_METADATA, StrId::STR_VIEW_METADATA, Settings2Icon, 32, 32});
  items.push_back({MenuAction::ADD_TO_FAVORITES,
                   isFavorite ? StrId::STR_REMOVE_FROM_FAVORITES : StrId::STR_ADD_TO_FAVORITES,
                   HeartIcon, 32, 32});
  items.push_back({MenuAction::MARK_READ_UNREAD,
                   isCompleted ? StrId::STR_MARK_AS_NOT_FINISHED : StrId::STR_MARK_AS_FINISHED,
                   TrophyIcon, 32, 32});
  if (isEpubFormat) {
    items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE, TransferIcon, 32, 32});
  }
  if (!isLibraryMode) {
    items.push_back({MenuAction::CLEAR_THEME_CACHE, StrId::STR_CLEAR_THEME_CACHE, TransferIcon, 32, 32});
  }
  if (isLibraryMode) {
    items.push_back({MenuAction::DELETE_COVER_THUMB, StrId::STR_LIBRARY_DELETE_COVER, ImageIcon, 32, 32});
    items.push_back({MenuAction::DELETE_PAGE_COVER_THUMBS, StrId::STR_LIBRARY_DELETE_PAGE_COVERS, ImageIcon, 32, 32});
    items.push_back({MenuAction::DELETE_ALL_LIBRARY_COVERS, StrId::STR_LIBRARY_DELETE_ALL_COVERS, ImageIcon, 32, 32});
    items.push_back({MenuAction::REINDEX_LIBRARY, StrId::STR_REINDEX_LIBRARY, LibraryIcon, 32, 32});
  }
  return items;
}

void BookContextMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BookContextMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    setResult(MenuResult{static_cast<int>(selectedAction), 0, 0});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
}

void BookContextMenuActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // White background — same clean look as the original BookContextMenu.

  const int itemCount = static_cast<int>(menuItems.size());
  const int visibleRows = std::min(itemCount, kMaxVisibleRows);

  // Panel — same sizing as LibraryPopupOverlay: 80 % wide, dynamic height
  // capped at 80 % of the screen.
  const int panelW = pageWidth * kPanelWPercent / 100;
  const int contentH = kPadY + kTitleH + 4 + kPadY + visibleRows * kRowH + kPadY;
  const int maxH = pageHeight * kPanelWPercent / 100;
  const int panelH = std::min(contentH, maxH);
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = (pageHeight - panelH) / 2;

  // Rounded white panel with black border.
  renderer.fillRoundedRect(panelX, panelY, panelW, panelH, kCornerRadius, Color::White);
  renderer.drawRoundedRect(panelX, panelY, panelW, panelH, 2, kCornerRadius, true);

  // Inline title (bold) — same style as LibraryPopupOverlay.
  const int titleX = panelX + kPadX;
  const int titleY = panelY + kPadY;
  renderer.drawText(UI_10_FONT_ID, titleX, titleY, bookTitle.c_str(), true, EpdFontFamily::BOLD);

  // Separator line under the title.
  const int sepY = titleY + kTitleH + 4;
  renderer.drawLine(panelX + kPadX, sepY, panelX + panelW - kPadX, sepY, 1, true);

  // Scroll range.
  int startIdx = 0;
  if (selectedIndex >= visibleRows) {
    startIdx = selectedIndex - visibleRows / 2;
    if (startIdx + visibleRows > itemCount) startIdx = itemCount - visibleRows;
    if (startIdx < 0) startIdx = 0;
  }

  const int listY = sepY + kPadY;
  const int endIdx = std::min(startIdx + visibleRows, itemCount);
  for (int i = startIdx; i < endIdx; ++i) {
    const int rowY = listY + (i - startIdx) * kRowH;
    const bool sel = (i == selectedIndex);

    // Inverted highlight for the focused row.
    if (sel) {
      renderer.fillRect(panelX + kPadX, rowY, panelW - 2 * kPadX, kRowH, true);
    }

    // Icon at its native size, vertically centered in the row.
    const int iw = menuItems[i].iconW;
    const int ih = menuItems[i].iconH;
    int textX = panelX + kPadX + kIconPad;
    if (menuItems[i].iconPixels && iw > 0 && ih > 0) {
      const int iconX = panelX + kPadX + kIconPad;
      const int iconY = rowY + (kRowH - ih) / 2;
      if (sel) {
        renderer.drawIconInverted(menuItems[i].iconPixels, iconX, iconY, iw, ih);
      } else {
        renderer.drawIcon(menuItems[i].iconPixels, iconX, iconY, iw, ih);
      }
      textX = iconX + iw + kIconPad;
    }

    // Row label.
    const char* label = I18N.get(menuItems[i].labelId);
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY = rowY + (kRowH - lh) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, sel ? false : true,
                      sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  // Scroll indicators (triangles) — same as LibraryPopupOverlay.
  if (startIdx > 0) {
    const int arrowY = panelY + kTitleH + kPadY + 2;
    renderer.drawLine(panelX + panelW / 2 - 6, arrowY + 6, panelX + panelW / 2, arrowY, 2, true);
    renderer.drawLine(panelX + panelW / 2, arrowY, panelX + panelW / 2 + 6, arrowY + 6, 2, true);
  }
  if (endIdx < itemCount) {
    const int arrowY = panelY + panelH - kPadY - 8;
    renderer.drawLine(panelX + panelW / 2 - 6, arrowY, panelX + panelW / 2, arrowY + 6, 2, true);
    renderer.drawLine(panelX + panelW / 2, arrowY + 6, panelX + panelW / 2 + 6, arrowY, 2, true);
  }

  // Button hints at the bottom.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
