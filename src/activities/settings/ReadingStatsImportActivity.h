#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"

class ReadingStatsImportActivity final : public Activity {
 public:
  explicit ReadingStatsImportActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsImport", renderer, mappedInput) {}

  static std::vector<std::string> getImportPaths();

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }

  std::vector<std::string> importPaths;
  size_t selectedIndex = 0;
  int pageItems = 0;

  void finishWithSelection();

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNavRelease(void* ctx, int delta);
  static void onNavContinuous(void* ctx, int delta);

 private:
  ListInputMapper listInputMapper;
  std::string getDisplayName(int index) const;
};
