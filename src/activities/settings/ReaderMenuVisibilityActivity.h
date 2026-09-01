#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ReaderMenuRegistry.h"
#include "../util/ListInputMapper.h"

class ReaderMenuVisibilityActivity final : public Activity {
 public:
  explicit ReaderMenuVisibilityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderMenuVisibility", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ListInputMapper listInputMapper;
  std::vector<const ReaderMenuItemDefinition*> entries;
  int selectedIndex = 0;

  void reloadEntries();
  void toggleSelectedEntry();
};