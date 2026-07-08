#pragma once

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public Activity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KOReaderSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  size_t selectedIndex = 0;

  void handleSelection();

 private:
  ListInputMapper listInputMapper;

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);
};
