#include "BookContextMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/PanelDrawHelper.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/heart.h"
#include "components/icons/trophy.h"
#include "components/icons/readingstats.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/image.h"
#include "fontIds.h"

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
    items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE, CleanMonitorIcon32, 32, 32});
  }
  if (!isLibraryMode) {
    items.push_back({MenuAction::CLEAR_THEME_CACHE, StrId::STR_CLEAR_THEME_CACHE, CleanMonitorIcon32, 32, 32});
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

  const int itemCount = static_cast<int>(menuItems.size());
  const int visibleRows = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

  auto layout = PanelDrawHelper::calculatePanel(pageWidth, pageHeight, visibleRows);

  PanelDrawHelper::drawBackground(renderer, layout);
  PanelDrawHelper::drawTitle(renderer, layout, bookTitle.c_str());
  PanelDrawHelper::drawSeparator(renderer, layout);

  int startIdx = 0;
  if (selectedIndex >= visibleRows) {
    startIdx = selectedIndex - visibleRows / 2;
    if (startIdx + visibleRows > itemCount) startIdx = itemCount - visibleRows;
    if (startIdx < 0) startIdx = 0;
  }
  const int endIdx = std::min(startIdx + visibleRows, itemCount);

  for (int i = startIdx; i < endIdx; ++i) {
    const int rowIndex = i - startIdx;
    PanelDrawHelper::drawRowHighlight(renderer, layout, rowIndex, i == selectedIndex);
    PanelDrawHelper::drawRowIcon(renderer, layout, rowIndex, menuItems[i].iconPixels, menuItems[i].iconW,
                                 menuItems[i].iconH, i == selectedIndex);

    const char* label = I18N.get(menuItems[i].labelId);
    int textX = PanelDrawHelper::getRowTextX(layout);
    if (menuItems[i].iconPixels && menuItems[i].iconW > 0 && menuItems[i].iconH > 0) {
      textX += menuItems[i].iconW + PanelDrawHelper::kIconPad;
    }
    int lh = renderer.getLineHeight(UI_10_FONT_ID);
    int rowY = PanelDrawHelper::getSeparatorY(layout) + PanelDrawHelper::kPadY + rowIndex * PanelDrawHelper::kRowH;
    int textY = rowY + (PanelDrawHelper::kRowH - lh) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, i == selectedIndex ? false : true,
                      i == selectedIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  PanelDrawHelper::drawScrollArrows(renderer, layout, startIdx > 0, endIdx < itemCount);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
