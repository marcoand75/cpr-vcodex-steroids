#pragma once

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class FavoritesAppActivity final : public Activity {
  ListInputMapper listInputMapper;
  int selectedIndex = 0;
  int favoriteCount = 0;

  void refreshEntries();
  void openSelectedEntry();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void releaseNav(void* ctx, int delta);
  static void continuousNav(void* ctx, int delta);

 public:
  explicit FavoritesAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FavoritesApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
