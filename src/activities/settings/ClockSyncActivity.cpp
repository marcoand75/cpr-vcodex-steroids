#include "ClockSyncActivity.h"

#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClockSyncActivity::onEnter() {
  Activity::onEnter();
  statusText = "Press Confirm to sync";
}

void ClockSyncActivity::runSync() {
  HalPowerManager::Lock powerLock;
  syncStarted = true;

  if (WiFi.status() != WL_CONNECTED) {
    statusText = "WiFi not connected";
    syncDone = true;
    syncSuccess = false;
    return;
  }

  statusText = "Syncing via NTP...";
  syncSuccess = halClock.syncFromNTP();
  syncDone = true;
  statusText = syncSuccess ? "Sync successful!" : "Sync failed";
}

void ClockSyncActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      (syncDone && mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    finish();
    return;
  }

  if (!syncStarted && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    runSync();
    requestUpdate();
    return;
  }
}

void ClockSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int cy = renderer.getScreenHeight() / 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 "Sync Clock");

  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, cy, statusText.c_str());
  renderer.displayBuffer();
}
