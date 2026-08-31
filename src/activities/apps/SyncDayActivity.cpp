#include "SyncDayActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>

#include "CrossPointState.h"
#include "ManualDateActivity.h"
#include "ReadingStatsStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TimeZoneSelectActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"
#include "util/TimeZoneRegistry.h"
#include "util/WiFiUtils.h"

namespace {
constexpr int ACTION_COUNT = 4;
constexpr int HELP_TEXT_LINE_HEIGHT = 18;

int drawWrappedHelpLine(GfxRenderer& renderer, const int left, const int top, const int width, const char* text) {
  int currentTop = top;
  const auto wrappedLines = renderer.wrappedText(UI_10_FONT_ID, text, width, 3);
  for (const auto& line : wrappedLines) {
    renderer.drawText(UI_10_FONT_ID, left, currentTop, line.c_str());
    currentTop += HELP_TEXT_LINE_HEIGHT;
  }
  return currentTop;
}

int drawHowItWorksText(GfxRenderer& renderer, const int left, const int top, const int width) {
  int currentTop = top;
  renderer.drawText(UI_10_FONT_ID, left, currentTop, tr(STR_SYNC_DAY_INFO_TITLE), true, EpdFontFamily::BOLD);
  currentTop += 22;

  currentTop = drawWrappedHelpLine(renderer, left, currentTop, width, tr(STR_SYNC_DAY_INFO_1));
  currentTop += 2;
  currentTop = drawWrappedHelpLine(renderer, left, currentTop, width, tr(STR_SYNC_DAY_INFO_2));
  currentTop += 2;
  currentTop = drawWrappedHelpLine(renderer, left, currentTop, width, tr(STR_SYNC_DAY_INFO_3));

  return currentTop;
}

std::string getObtainedDateLabel() {
  const auto displayInfo = HeaderDateUtils::getDisplayDateInfo();
  if (!TimeUtils::isClockValid(displayInfo.timestamp)) {
    return tr(STR_NOT_SET);
  }

  return TimeUtils::formatDate(displayInfo.timestamp, displayInfo.usedFallback);
}

std::string getTimeZoneLabel() {
  return TimeZoneRegistry::getPresetLabel(TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset));
}

std::string getDateFormatLabel() {
  switch (static_cast<CrossPointSettings::DATE_FORMAT>(SETTINGS.dateFormat)) {
    case CrossPointSettings::DATE_MM_DD_YYYY:
      return tr(STR_DATE_FORMAT_MM_DD_YYYY);
    case CrossPointSettings::DATE_YYYY_MM_DD:
      return tr(STR_DATE_FORMAT_YYYY_MM_DD);
    case CrossPointSettings::DATE_DD_MM_YYYY:
    default:
      return tr(STR_DATE_FORMAT_DD_MM_YYYY);
  }
}

std::string getNetworkStatusLabel() {
  return WiFi.status() == WL_CONNECTED ? std::string(tr(STR_CONNECTED)) : std::string(tr(STR_NOT_CONNECTED));
}
}  // namespace

void SyncDayActivity::onEnter() {
  Activity::onEnter();
  TimeUtils::configureTimezone();
  wifiConnectedOnEnter = isWifiConnected();
  connectedInActivity = false;
  syncing = false;
  lastSyncSucceeded = false;
  lastSyncFailed = false;
  selectedIndex = ButtonNavigator::clampIndex(selectedIndex, ACTION_COUNT);
  requestUpdate();
}

void SyncDayActivity::onExit() {
  Activity::onExit();

  if (!wifiConnectedOnEnter && connectedInActivity) {
    WiFiUtils::wifiOff();
  }

  if (returnTarget_ == SilentRebootTarget::Home) {
    silentRestartToHome();
  } else if (returnTarget_ == SilentRebootTarget::Apps) {
    silentRestartToApps();
  }
}

void SyncDayActivity::loop() {
  if (syncing) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      if (isWifiConnected()) {
        syncTime();
      } else {
        openWifiSelection();
      }
    } else if (selectedIndex == 1) {
      openManualDateSelection();
    } else if (selectedIndex == 2) {
      openTimeZoneSelection();
    } else {
      SETTINGS.dateFormat = (SETTINGS.dateFormat + 1) % CrossPointSettings::DATE_FORMAT_COUNT;
      SETTINGS.saveToFile();
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });
}

void SyncDayActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SYNC_DAY));

  if (syncing) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_SYNCING_TIME), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 8, tr(STR_SYNC_DAY_HINT));
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listTop = contentTop;
  const int listHeight = metrics.listWithSubtitleRowHeight * ACTION_COUNT;
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, ACTION_COUNT, selectedIndex,
      [](int index) {
        if (index == 0) return std::string(tr(STR_SYNC_NOW));
        if (index == 1) return std::string(tr(STR_SET_DATE));
        if (index == 2) return std::string(tr(STR_TIME_ZONE));
        return std::string(tr(STR_DATE_FORMAT));
      },
      [](int index) {
        if (index == 0) return getObtainedDateLabel();
        if (index == 1) return std::string(tr(STR_MANUAL));
        if (index == 2) return getTimeZoneLabel();
        return getDateFormatLabel();
      },
      [](int index) {
        if (index == 0) return UIIcon::Wifi;
        if (index == 1) return UIIcon::Recent;
        if (index == 2) return UIIcon::Settings;
        return UIIcon::Recent;
      },
      [](int index) { return index == 0 ? getNetworkStatusLabel() : std::string(); }, false);

  int infoTop = listTop + listHeight + metrics.verticalSpacing;
  const int infoWidth = pageWidth - sidePadding * 2;
  if (selectedIndex == 0) {
    const std::string helperText = renderer.truncatedText(UI_10_FONT_ID, getStatusMessage().c_str(), infoWidth);
    renderer.drawText(UI_10_FONT_ID, sidePadding, infoTop, helperText.c_str());
    infoTop += HELP_TEXT_LINE_HEIGHT + 10;
  }
  drawHowItWorksText(renderer, sidePadding, infoTop, infoWidth);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

bool SyncDayActivity::isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }

std::string SyncDayActivity::getStatusMessage() const {
  if (lastSyncSucceeded) {
    return tr(STR_TIME_SYNCED);
  }

  if (lastSyncFailed) {
    return tr(STR_TIME_SYNC_FAILED);
  }

  const auto displayInfo = HeaderDateUtils::getDisplayDateInfo();
  if (displayInfo.usedFallback || !TimeUtils::isClockValid(displayInfo.timestamp)) {
    return tr(STR_SYNC_DAY_WIFI_HINT);
  }

  return tr(STR_SYNC_DAY_HINT);
}

void SyncDayActivity::openTimeZoneSelection() {
  startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void SyncDayActivity::openManualDateSelection() {
  const uint32_t previousValidTimestamp = TimeUtils::getCurrentValidTimestamp();
  startActivityForResult(std::make_unique<ManualDateActivity>(renderer, mappedInput),
                         [this, previousValidTimestamp](const ActivityResult&) {
                           const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
                           if (currentValidTimestamp != previousValidTimestamp) {
                             createDueReadingStatsBackupWithFeedback();
                             createSyncDateBackupIfDayChanged(previousValidTimestamp, currentValidTimestamp);
                           }
                           requestUpdate();
                         });
}

void SyncDayActivity::openWifiSelection() {
  startActivityForResult(WifiSelectionActivity::createNetworkOperation(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || !isWifiConnected()) {
                             requestUpdate();
                             return;
                           }

                           if (!wifiConnectedOnEnter) {
                             connectedInActivity = true;
                           }
                           syncTime();
                         });
}

void SyncDayActivity::syncTime() {
  syncing = true;
  lastSyncSucceeded = false;
  lastSyncFailed = false;
  requestUpdate(true);

  const bool hadValidTimeBefore = TimeUtils::isClockValid();
  const uint32_t previousValidTimestamp = hadValidTimeBefore ? TimeUtils::getCurrentValidTimestamp() : 0;
  const bool ntpSuccess = TimeUtils::syncTimeWithNtp();
  const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
  const bool effectiveSuccess = ntpSuccess || (!hadValidTimeBefore && currentValidTimestamp > 0);
  if (effectiveSuccess && currentValidTimestamp > 0) {
    APP_STATE.registerValidTimeSync(currentValidTimestamp);
    APP_STATE.saveToFile();
  }

  syncing = false;
  lastSyncSucceeded = effectiveSuccess;
  lastSyncFailed = !effectiveSuccess;
  requestUpdate(true);
  if (effectiveSuccess) {
    createDueReadingStatsBackupWithFeedback();
    createSyncDateBackupIfDayChanged(previousValidTimestamp, currentValidTimestamp);
    requestUpdate(true);
  }
}

void SyncDayActivity::createDueReadingStatsBackupWithFeedback() {
  if (!READING_STATS.isAutoBackupDue()) {
    return;
  }

  PopupUtils::showTransientPopup(*this,tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
  const bool backupReady = READING_STATS.createDueAutoBackup();
  PopupUtils::showTransientPopup(*this,backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                     backupReady ? 100 : -1, backupReady ? 350 : 700);
}

void SyncDayActivity::createSyncDateBackupIfDayChanged(const uint32_t previousTimestamp,
                                                       const uint32_t currentTimestamp) {
  // New convenience backup (separate from the interval auto-backup): whenever
  // a date update changes the calendar day, export the reading stats to a
  // file named after the newly synced date (/exports/stats_syncdate_YYYY-MM-DD.json).
  if (!TimeUtils::isClockValid(previousTimestamp) || !TimeUtils::isClockValid(currentTimestamp)) {
    return;
  }
  const uint32_t prevDay = TimeUtils::getLocalDayOrdinal(previousTimestamp);
  const uint32_t curDay = TimeUtils::getLocalDayOrdinal(currentTimestamp);
  if (prevDay == 0 || curDay == 0 || prevDay == curDay) {
    return;  // no day change (or invalid)
  }

  if (READING_STATS.createSyncDateBackup(currentTimestamp)) {
    LOG_DBG("SYNC", "Day changed (%u -> %u): created syncdate backup", prevDay, curDay);
  }
}
