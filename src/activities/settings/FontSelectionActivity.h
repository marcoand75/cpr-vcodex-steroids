#pragma once

#include <SdCardFontRegistry.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"

class FontSelectionActivity final : public Activity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void handleSelection();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNavRelease(void* ctx, int delta);
  static void onNavContinuous(void* ctx, int delta);

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;  // index used by valueSetter
  };

  std::vector<FontEntry> fonts_;
  int selectedIndex_ = 0;
  int pageItems_ = 0;

 private:
  const SdCardFontRegistry* registry_;
  ListInputMapper listInputMapper;
};
