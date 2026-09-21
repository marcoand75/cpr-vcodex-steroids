#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "../Activity.h"
#include "components/UITheme.h"
#include "../util/ListInputMapper.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public Activity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguageSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void handleSelection();
  int selectedIndex = 0;
  int pageItems = 0;
  constexpr static uint8_t totalItems = getLanguageCount();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNavRelease(void* ctx, int delta);
  static void onNavContinuous(void* ctx, int delta);

 private:
  ListInputMapper listInputMapper;
};
