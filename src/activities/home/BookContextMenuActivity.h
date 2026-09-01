#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class BookContextMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    OPEN_BOOK,
    REMOVE_FROM_RECENTS,
    ADD_TO_FAVORITES,
    VIEW_METADATA,
    VIEW_STATS,
    MARK_READ_UNREAD,
    DELETE_CACHE,
    DELETE_COVER_THUMB,
    DELETE_PAGE_COVER_THUMBS,
    DELETE_ALL_LIBRARY_COVERS,
    REINDEX_LIBRARY,
    CLEAR_THEME_CACHE,
    HIDE_BOOK,           // Hide from library shelf
    DELETE_BOOK_FILE     // Permanently delete book file + cache
  };

  explicit BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::string& bookTitle, bool isFavorite, bool isCompleted,
                                    bool isEpubFormat, bool isLibraryMode = false, bool isHidden = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
    const uint8_t* iconPixels;  // bitmap data
    int iconW;                  // native width
    int iconH;                  // native height
  };

  static std::vector<MenuItem> buildMenuItems(bool isFavorite, bool isCompleted, bool isEpubFormat,
                                                bool isLibraryMode = false, bool isHidden = false);

  const std::vector<MenuItem> menuItems;
  const std::string bookTitle;
  int selectedIndex = 0;
  ListInputMapper listInputMapper;
};