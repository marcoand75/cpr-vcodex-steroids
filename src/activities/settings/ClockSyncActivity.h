#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"

class ClockSyncActivity final : public Activity {
  bool syncStarted = false;
  bool syncDone = false;
  bool syncSuccess = false;
  std::string statusText;

 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockSync", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void runSync();
};
