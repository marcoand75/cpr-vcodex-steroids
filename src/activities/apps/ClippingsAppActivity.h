#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../reader/ClippingStore.h"
#include "util/ButtonNavigator.h"

class ClippingsAppActivity final : public Activity {
  struct BookEntry {
    std::string bookId;
    std::string path;
    std::string title;
    std::string author;
    std::vector<ClippingStore::Clipping> clippings;
  };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<BookEntry> entries;

  void refreshEntries();
  void openSelectedBook();
  bool clearClippingsForBook(const std::string& bookId, const std::string& bookPath) const;
  void confirmDeleteSelectedBook();

 public:
  explicit ClippingsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClippingsApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
