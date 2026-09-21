#pragma once

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class FlashcardsAppActivity final : public Activity {
  ListInputMapper listInputMapper;
  int selectedIndex = 0;
  int recentCount = 0;
  int deckCount = 0;

  void refreshCounts();
  void openSelectedEntry();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void releaseNav(void* ctx, int delta);
  static void continuousNav(void* ctx, int delta);

 public:
  explicit FlashcardsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardsApp", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
