#pragma once

#include "../Activity.h"
#include "SilentRestart.h"
#include "util/ButtonNavigator.h"
#include "util/PopupUtils.h"

class SyncDayActivity final : public Activity {
  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;
  bool syncing = false;
  bool lastSyncSucceeded = false;
  bool lastSyncFailed = false;
  SilentRebootTarget returnTarget_ = SilentRebootTarget::Home;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void openWifiSelection();
  void openManualDateSelection();
  void openTimeZoneSelection();
  void syncTime();
  void createDueReadingStatsBackupWithFeedback();
  void createSyncDateBackupIfDayChanged(uint32_t previousTimestamp, uint32_t currentTimestamp);
  bool isWifiConnected() const;
  std::string getStatusMessage() const;

 public:
  explicit SyncDayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           SilentRebootTarget returnTarget = SilentRebootTarget::Home)
      : Activity("SyncDay", renderer, mappedInput), returnTarget_(returnTarget) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return syncing; }
};
