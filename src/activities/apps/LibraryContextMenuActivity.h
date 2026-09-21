#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "../Activity.h"
#include "I18n.h"
#include "../util/ListInputMapper.h"

class LibraryContextMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    ScanAndOpen,     // Esegue scansiona SD per aggiornamento, poi apre libreria
    RebuildLibrary,  // Ricrea libreria da capo (invalida cache)
    ClearCorruptCovers,  // Rimuove BMP zero-size
  };

  explicit LibraryContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  std::vector<MenuItem> items_;
  int selectedIndex_ = 0;
  bool confirmed_ = false;
  ListInputMapper listInputMapper_;

  void onConfirm();
};
