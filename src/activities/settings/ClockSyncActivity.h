#pragma once

#include "activities/Activity.h"

// Manual NTP resync action. Connects to WiFi if needed, runs a forced sync
// (bypassing the once-per-device debounce), reports success/failure, then waits
// for Back.
class ClockSyncActivity final : public Activity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return state == SYNCING; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;

  void beginSync();
  void openWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void runSync();
};
