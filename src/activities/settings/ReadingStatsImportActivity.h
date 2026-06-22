#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReadingStatsImportActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> importPaths;
  size_t selectedIndex = 0;

  std::string getDisplayName(int index) const;
  void finishWithSelection();

 public:
  explicit ReadingStatsImportActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsImport", renderer, mappedInput) {}

  static std::vector<std::string> getImportPaths();

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
