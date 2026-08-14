#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Stream.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "KOReaderCredentialStore.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SettingsList.h"
#include "WifiCredentialStore.h"
#include "util/BookIdentity.h"
#include "util/CprVcodexLogs.h"
#include "util/ShortcutRegistry.h"
#include "util/TimeZoneRegistry.h"

#include "JsonSettingsIOShared.inc"


// Convert legacy settings.
void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

bool loadSettingsDirect(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };
  auto loadToggle = [&](const char* key, uint8_t& field) {
    field = clamp(doc[key] | field, static_cast<uint8_t>(2), field);
  };
  auto loadEnum = [&](const char* key, uint8_t& field, const uint8_t count) {
    field = clamp(doc[key] | field, count, field);
  };
  auto loadValue = [&](const char* key, uint8_t& field, const uint8_t minValue, const uint8_t maxValue) {
    uint8_t value = doc[key] | field;
    if (value < minValue) {
      value = minValue;
    } else if (value > maxValue) {
      value = maxValue;
    }
    field = value;
  };
  auto loadString = [&](const char* key, char* dest, const size_t maxLen) {
    const std::string value = doc[key] | std::string(dest);
    strncpy(dest, value.c_str(), maxLen - 1);
    dest[maxLen - 1] = '\0';
  };

  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  loadEnum("sleepScreen", s.sleepScreen, CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
  loadEnum("sleepScreenCoverMode", s.sleepScreenCoverMode, CrossPointSettings::SLEEP_SCREEN_COVER_MODE_COUNT);
  loadEnum("sleepScreenCoverFilter", s.sleepScreenCoverFilter, CrossPointSettings::SLEEP_SCREEN_COVER_FILTER_COUNT);
  loadToggle("cleanSleepRefresh", s.cleanSleepRefresh);
  loadEnum("hideBatteryPercentage", s.hideBatteryPercentage, CrossPointSettings::HIDE_BATTERY_PERCENTAGE_COUNT);
  loadEnum("refreshFrequency", s.refreshFrequency, CrossPointSettings::REFRESH_FREQUENCY_COUNT);
  loadToggle("fadingFix", s.fadingFix);
  loadToggle("darkMode", s.darkMode);
  loadToggle("antiGhostingExperimental", s.antiGhostingExperimental);

  loadEnum("fontSize", s.fontSize, CrossPointSettings::FONT_SIZE_COUNT);
  const uint8_t fontSizeSchemaVersion = doc["fontSizeSchemaVersion"] | static_cast<uint8_t>(0);
  if (fontSizeSchemaVersion < FONT_SIZE_SCHEMA_VERSION && !doc["fontSize"].isNull()) {
    const uint8_t legacyFontSize = doc["fontSize"] | static_cast<uint8_t>(CrossPointSettings::MEDIUM - 1);
    if (legacyFontSize < static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)) {
      s.fontSize = static_cast<uint8_t>(legacyFontSize + 1);
      if (needsResave) *needsResave = true;
    }
  }

  loadEnum("lineSpacing", s.lineSpacing, CrossPointSettings::LINE_COMPRESSION_COUNT);
  loadValue("screenMargin", s.screenMargin, 5, 40);
  loadEnum("paragraphAlignment", s.paragraphAlignment, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
  loadToggle("embeddedStyle", s.embeddedStyle);
  loadToggle("hyphenationEnabled", s.hyphenationEnabled);
  loadEnum("bionicReading", s.bionicReading, CrossPointSettings::BIONIC_READING_MODE_COUNT);
  loadEnum("orientation", s.orientation, CrossPointSettings::ORIENTATION_COUNT);
  loadToggle("extraParagraphSpacing", s.extraParagraphSpacing);
  loadToggle("forceParagraphIndents", s.forceParagraphIndents);
  loadToggle("textAntiAliasing", s.textAntiAliasing);
  {
    const uint8_t textDarknessSchemaVersion = doc["textDarknessSchemaVersion"] | static_cast<uint8_t>(0);
    const uint8_t rawTextDarkness = doc["textDarkness"] | s.textDarkness;
    if (textDarknessSchemaVersion < TEXT_DARKNESS_SCHEMA_VERSION && !doc["textDarkness"].isNull()) {
      if (rawTextDarkness == 1) {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_DARK;
        if (needsResave) *needsResave = true;
      } else if (rawTextDarkness == 2) {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_EXTRA_DARK;
        if (needsResave) *needsResave = true;
      } else {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_NORMAL;
      }
    } else if (rawTextDarkness < static_cast<uint8_t>(CrossPointSettings::TEXT_DARKNESS_COUNT)) {
      s.textDarkness = rawTextDarkness;
    } else {
      s.textDarkness = CrossPointSettings::TEXT_DARKNESS_NORMAL;
      if (needsResave) *needsResave = true;
    }
  }
  loadEnum("readerRefreshMode", s.readerRefreshMode, CrossPointSettings::READER_REFRESH_MODE_COUNT);
  loadEnum("imageRendering", s.imageRendering, CrossPointSettings::IMAGE_RENDERING_COUNT);

  loadEnum("sideButtonLayout", s.sideButtonLayout, CrossPointSettings::SIDE_BUTTON_LAYOUT_COUNT);
  loadToggle("frontButtonFollowOrientation", s.frontButtonFollowOrientation);
  {
    const uint8_t rawShortPwrBtn = doc["shortPwrBtn"] | s.shortPwrBtn;
    if (rawShortPwrBtn < static_cast<uint8_t>(CrossPointSettings::SHORT_PWRBTN_COUNT)) {
      s.shortPwrBtn = rawShortPwrBtn;
    } else {
      s.shortPwrBtn = CrossPointSettings::IGNORE;
      if (needsResave) *needsResave = true;
    }
  }
  loadEnum("tiltPageTurn", s.tiltPageTurn, CrossPointSettings::TILT_PAGE_TURN_COUNT);
  loadEnum("sleepTimeout", s.sleepTimeout, CrossPointSettings::SLEEP_TIMEOUT_COUNT);
  loadToggle("showHiddenFiles", s.showHiddenFiles);

  loadString("opdsServerUrl", s.opdsServerUrl, sizeof(s.opdsServerUrl));
  loadString("opdsUsername", s.opdsUsername, sizeof(s.opdsUsername));
  loadEnum("opdsFilenameFormat", s.opdsFilenameFormat, CrossPointSettings::OPDS_FILENAME_FORMAT_COUNT);
  loadToggle("koSyncAutoPullOnOpen", s.koSyncAutoPullOnOpen);
  loadToggle("koSyncAutoPushOnClose", s.koSyncAutoPushOnClose);
  {
    bool ok = false;
    std::string password = obfuscation::deobfuscateFromBase64(doc["opdsPassword_obf"] | "", &ok);
    if (!ok || password.empty()) {
      password = doc["opdsPassword"] | std::string(s.opdsPassword);
      if (password != s.opdsPassword && needsResave) *needsResave = true;
    }
    strncpy(s.opdsPassword, password.c_str(), sizeof(s.opdsPassword) - 1);
    s.opdsPassword[sizeof(s.opdsPassword) - 1] = '\0';
  }

  loadToggle("statusBarChapterPageCount", s.statusBarChapterPageCount);
  loadToggle("statusBarBookProgressPercentage", s.statusBarBookProgressPercentage);
  loadEnum("statusBarProgressBar", s.statusBarProgressBar, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_COUNT);
  loadEnum("statusBarProgressBarThickness", s.statusBarProgressBarThickness,
           CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT);
  loadEnum("statusBarTitle", s.statusBarTitle, CrossPointSettings::STATUS_BAR_TITLE_COUNT);
  loadToggle("statusBarBattery", s.statusBarBattery);
  loadEnum("statusBarClock", s.statusBarClock, CrossPointSettings::STATUS_BAR_CLOCK_COUNT);
  loadToggle("clockHasBeenSynced", s.clockHasBeenSynced);
  loadEnum("xtcStatusBarMode", s.xtcStatusBarMode, CrossPointSettings::XTC_STATUS_BAR_MODE_COUNT);

  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  s.homeBookSource = clamp(doc["homeBookSource"] | s.homeBookSource, S::HOME_BOOK_SOURCE_COUNT, s.homeBookSource);
  s.autoSyncDay = clamp(doc["autoSyncDay"] | s.autoSyncDay, static_cast<uint8_t>(2), s.autoSyncDay);
  s.syncDayWifiChoice =
      clamp(doc["syncDayWifiChoice"] | s.syncDayWifiChoice, S::SYNC_DAY_WIFI_CHOICE_COUNT, s.syncDayWifiChoice);
  s.syncDayReminderStarts = clamp(doc["syncDayReminderStarts"] | s.syncDayReminderStarts,
                                  S::SYNC_DAY_REMINDER_STARTS_COUNT, s.syncDayReminderStarts);
  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | static_cast<uint8_t>(S::SLEEP_IMAGE_SHUFFLE),
                            S::SLEEP_IMAGE_ORDER_COUNT, S::SLEEP_IMAGE_SHUFFLE);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
  s.dateFormat = clamp(doc["dateFormat"] | s.dateFormat, S::DATE_FORMAT_COUNT, s.dateFormat);
  s.dailyGoalTarget = clamp(doc["dailyGoalTarget"] | s.dailyGoalTarget, S::DAILY_GOAL_TARGET_COUNT, s.dailyGoalTarget);
  s.readingStatsAutoBackup = clamp(doc["readingStatsAutoBackup"] | s.readingStatsAutoBackup,
                                   S::READING_STATS_AUTOBACKUP_COUNT, s.readingStatsAutoBackup);
  {
    const uint8_t rawFlashcardStudyMode = doc["flashcardStudyMode"] | s.flashcardStudyMode;
    const uint8_t flashcardStudyModeSchemaVersion = doc["flashcardStudyModeSchemaVersion"] | static_cast<uint8_t>(0);
    s.flashcardStudyMode = migrateStoredFlashcardStudyMode(rawFlashcardStudyMode, flashcardStudyModeSchemaVersion,
                                                           s.flashcardStudyMode, needsResave);
  }
  s.flashcardSessionSize = clamp(doc["flashcardSessionSize"] | s.flashcardSessionSize, S::FLASHCARD_SESSION_SIZE_COUNT,
                                 s.flashcardSessionSize);
  s.showStatsAfterReading =
      clamp(doc["showStatsAfterReading"] | s.showStatsAfterReading, static_cast<uint8_t>(2), s.showStatsAfterReading);
  s.moveCompletedBooks =
      clamp(doc["moveCompletedBooks"] | s.moveCompletedBooks, static_cast<uint8_t>(2), s.moveCompletedBooks);
  s.achievementsEnabled =
      clamp(doc["achievementsEnabled"] | s.achievementsEnabled, static_cast<uint8_t>(2), s.achievementsEnabled);
  s.achievementPopups =
      clamp(doc["achievementPopups"] | s.achievementPopups, static_cast<uint8_t>(2), s.achievementPopups);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder =
      clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount, s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clamp(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcut = clamp(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clamp(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clamp(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clamp(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut =
      clamp(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount, s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut =
      clamp(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount, s.readingProfileShortcut);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clamp(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clamp(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clamp(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clamp(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clamp(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clamp(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clamp(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clamp(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clamp(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.dictionaryShortcut =
      clamp(doc["dictionaryShortcut"] | s.dictionaryShortcut, shortcutLocationCount, s.dictionaryShortcut);
  s.dictionaryShortcutOrder =
      clamp(doc["dictionaryShortcutOrder"] | s.dictionaryShortcutOrder, shortcutOrderCount, s.dictionaryShortcutOrder);
  s.fileTransferShortcut =
      clamp(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.screenCleanShortcut =
      clamp(doc["screenCleanShortcut"] | s.screenCleanShortcut, shortcutLocationCount, s.screenCleanShortcut);
  s.screenCleanShortcutOrder = clamp(doc["screenCleanShortcutOrder"] | s.screenCleanShortcutOrder, shortcutOrderCount,
                                     s.screenCleanShortcutOrder);
  s.sleepShortcut = clamp(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
  s.sleepShortcutOrder =
      clamp(doc["sleepShortcutOrder"] | s.sleepShortcutOrder, shortcutOrderCount, s.sleepShortcutOrder);
  s.opdsBrowserShortcut =
      clamp(doc["opdsBrowserShortcut"] | s.opdsBrowserShortcut, shortcutLocationCount, s.opdsBrowserShortcut);
  s.opdsBrowserShortcutOrder = clamp(doc["opdsBrowserShortcutOrder"] | s.opdsBrowserShortcutOrder, shortcutOrderCount,
                                     s.opdsBrowserShortcutOrder);

  s.browseFilesShortcutVisible = clamp(doc["browseFilesShortcutVisible"] | s.browseFilesShortcutVisible,
                                       static_cast<uint8_t>(2), s.browseFilesShortcutVisible);
  s.statsShortcutVisible =
      clamp(doc["statsShortcutVisible"] | s.statsShortcutVisible, static_cast<uint8_t>(2), s.statsShortcutVisible);
  s.syncDayShortcutVisible = clamp(doc["syncDayShortcutVisible"] | s.syncDayShortcutVisible, static_cast<uint8_t>(2),
                                   s.syncDayShortcutVisible);
  s.settingsShortcutVisible = clamp(doc["settingsShortcutVisible"] | s.settingsShortcutVisible, static_cast<uint8_t>(2),
                                    s.settingsShortcutVisible);
  s.readingStatsShortcutVisible = clamp(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible,
                                        static_cast<uint8_t>(2), s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible = clamp(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingHeatmapShortcutVisible);
  s.readingProfileShortcutVisible = clamp(doc["readingProfileShortcutVisible"] | s.readingProfileShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingProfileShortcutVisible);
  s.achievementsShortcutVisible = clamp(doc["achievementsShortcutVisible"] | s.achievementsShortcutVisible,
                                        static_cast<uint8_t>(2), s.achievementsShortcutVisible);
  s.ifFoundShortcutVisible = clamp(doc["ifFoundShortcutVisible"] | s.ifFoundShortcutVisible, static_cast<uint8_t>(2),
                                   s.ifFoundShortcutVisible);
  s.readMeShortcutVisible =
      clamp(doc["readMeShortcutVisible"] | s.readMeShortcutVisible, static_cast<uint8_t>(2), s.readMeShortcutVisible);
  s.recentBooksShortcutVisible = clamp(doc["recentBooksShortcutVisible"] | s.recentBooksShortcutVisible,
                                       static_cast<uint8_t>(2), s.recentBooksShortcutVisible);
  s.bookmarksShortcutVisible = clamp(doc["bookmarksShortcutVisible"] | s.bookmarksShortcutVisible,
                                     static_cast<uint8_t>(2), s.bookmarksShortcutVisible);
  s.favoritesShortcutVisible = clamp(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible,
                                     static_cast<uint8_t>(2), s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible = clamp(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible,
                                      static_cast<uint8_t>(2), s.flashcardsShortcutVisible);
  s.dictionaryShortcutVisible = clamp(doc["dictionaryShortcutVisible"] | s.dictionaryShortcutVisible,
                                      static_cast<uint8_t>(2), s.dictionaryShortcutVisible);
  s.fileTransferShortcutVisible = clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible,
                                        static_cast<uint8_t>(2), s.fileTransferShortcutVisible);
  s.screenCleanShortcutVisible = clamp(doc["screenCleanShortcutVisible"] | s.screenCleanShortcutVisible,
                                       static_cast<uint8_t>(2), s.screenCleanShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);
  s.opdsBrowserShortcutVisible = clamp(doc["opdsBrowserShortcutVisible"] | s.opdsBrowserShortcutVisible,
                                       static_cast<uint8_t>(2), s.opdsBrowserShortcutVisible);

  migrateLegacyStatsShortcut(s, doc, needsResave);
  CrossPointSettings::validateFrontButtonMapping(s);

  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  JsonArray recentArr = doc["recentSleepImages"].to<JsonArray>();
  for (int i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; i++) recentArr.add(s.recentSleepImages[i]);
  doc["recentSleepPos"] = s.recentSleepPos;
  doc["recentSleepFill"] = s.recentSleepFill;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["lastKnownValidTimestamp"] = s.lastKnownValidTimestamp;
  doc["lastReadingStatsBackupDayOrdinal"] = s.lastReadingStatsBackupDayOrdinal;
  doc["syncDayReminderStartCount"] = s.syncDayReminderStartCount;
  doc["syncDayReminderLatched"] = s.syncDayReminderLatched;
  JsonObject sync = doc["koReaderSyncSession"].to<JsonObject>();
  sync["active"] = s.koReaderSyncSession.active;
  sync["epubPath"] = s.koReaderSyncSession.epubPath;
  sync["spineIndex"] = s.koReaderSyncSession.spineIndex;
  sync["page"] = s.koReaderSyncSession.page;
  sync["totalPagesInSpine"] = s.koReaderSyncSession.totalPagesInSpine;
  sync["paragraphIndex"] = s.koReaderSyncSession.paragraphIndex;
  sync["hasParagraphIndex"] = s.koReaderSyncSession.hasParagraphIndex;
  sync["xhtmlSeekHint"] = s.koReaderSyncSession.xhtmlSeekHint;
  sync["hasLocalKoReaderPosition"] = s.koReaderSyncSession.hasLocalKoReaderPosition;
  sync["localKoReaderProgress"] = s.koReaderSyncSession.localKoReaderProgress;
  sync["localKoReaderPercentage"] = s.koReaderSyncSession.localKoReaderPercentage;
  sync["localChapterLabel"] = s.koReaderSyncSession.localChapterLabel;
  sync["intent"] = static_cast<uint8_t>(s.koReaderSyncSession.intent);
  sync["outcome"] = static_cast<uint8_t>(s.koReaderSyncSession.outcome);
  sync["resultSpineIndex"] = s.koReaderSyncSession.resultSpineIndex;
  sync["resultPage"] = s.koReaderSyncSession.resultPage;
  sync["resultParagraphIndex"] = s.koReaderSyncSession.resultParagraphIndex;
  sync["resultHasParagraphIndex"] = s.koReaderSyncSession.resultHasParagraphIndex;
  sync["resultListItemIndex"] = s.koReaderSyncSession.resultListItemIndex;
  sync["resultHasListItemIndex"] = s.koReaderSyncSession.resultHasListItemIndex;
  sync["exitToHomeAfterSync"] = s.koReaderSyncSession.exitToHomeAfterSync;
  sync["autoPullEpubPath"] = s.koReaderSyncSession.autoPullEpubPath;
  JsonObject jump = doc["pendingBookmarkJump"].to<JsonObject>();
  jump["active"] = s.pendingBookmarkJump.active;
  jump["bookPath"] = s.pendingBookmarkJump.bookPath;
  jump["spineIndex"] = s.pendingBookmarkJump.spineIndex;
  jump["pageNumber"] = s.pendingBookmarkJump.pageNumber;
  // Screensaver anti-repetition history
  JsonArray ssRecentArr = doc["recentScreensaverImages"].to<JsonArray>();
  for (int i = 0; i < CrossPointState::SCREENSAVER_RECENT_COUNT; i++) ssRecentArr.add(s.recentScreensaverImages[i]);
  doc["recentScreensaverPos"] = s.recentScreensaverPos;
  doc["recentScreensaverFill"] = s.recentScreensaverFill;
  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("CPS", std::string("Settings JSON parse error: ") + error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  memset(s.recentSleepImages, 0, sizeof(s.recentSleepImages));
  JsonArrayConst recentArr = doc["recentSleepImages"];
  const int actualCount = recentArr.isNull() ? 0
                                             : std::min(static_cast<int>(recentArr.size()),
                                                        static_cast<int>(CrossPointState::SLEEP_RECENT_COUNT));
  for (int i = 0; i < actualCount; i++) s.recentSleepImages[i] = recentArr[i] | static_cast<uint16_t>(0);
  s.recentSleepPos = doc["recentSleepPos"] | static_cast<uint8_t>(0);
  if (s.recentSleepPos >= CrossPointState::SLEEP_RECENT_COUNT)
    s.recentSleepPos = actualCount > 0 ? s.recentSleepPos % CrossPointState::SLEEP_RECENT_COUNT : 0;
  s.recentSleepFill = doc["recentSleepFill"] | static_cast<uint8_t>(0);
  s.recentSleepFill = static_cast<uint8_t>(std::min(static_cast<int>(s.recentSleepFill), actualCount));
  if (s.recentSleepFill == 0 && !doc["lastSleepImage"].isNull()) {
    const uint8_t legacy = doc["lastSleepImage"] | static_cast<uint8_t>(UINT8_MAX);
    if (legacy != UINT8_MAX) s.pushRecentSleep(static_cast<uint16_t>(legacy));
  }
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.lastKnownValidTimestamp = doc["lastKnownValidTimestamp"] | static_cast<uint32_t>(0);
  s.lastReadingStatsBackupDayOrdinal = doc["lastReadingStatsBackupDayOrdinal"] | static_cast<uint32_t>(0);
  s.syncDayReminderStartCount = doc["syncDayReminderStartCount"] | (uint8_t)0;
  s.syncDayReminderLatched = doc["syncDayReminderLatched"] | false;
  {
    JsonObjectConst sync = doc["koReaderSyncSession"];
    if (!sync.isNull()) {
      s.koReaderSyncSession.active = sync["active"] | false;
      s.koReaderSyncSession.epubPath = sync["epubPath"] | std::string("");
      s.koReaderSyncSession.spineIndex = sync["spineIndex"] | 0;
      s.koReaderSyncSession.page = sync["page"] | 0;
      s.koReaderSyncSession.totalPagesInSpine = sync["totalPagesInSpine"] | 0;
      s.koReaderSyncSession.paragraphIndex = sync["paragraphIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.hasParagraphIndex = sync["hasParagraphIndex"] | false;
      s.koReaderSyncSession.xhtmlSeekHint = sync["xhtmlSeekHint"] | static_cast<uint32_t>(0);
      s.koReaderSyncSession.hasLocalKoReaderPosition = sync["hasLocalKoReaderPosition"] | false;
      s.koReaderSyncSession.localKoReaderProgress = sync["localKoReaderProgress"] | std::string("");
      s.koReaderSyncSession.localKoReaderPercentage = sync["localKoReaderPercentage"] | 0.0f;
      s.koReaderSyncSession.localChapterLabel = sync["localChapterLabel"] | std::string("");
      s.koReaderSyncSession.intent = static_cast<KOReaderSyncIntentState>(sync["intent"] | static_cast<uint8_t>(0));
      s.koReaderSyncSession.outcome = static_cast<KOReaderSyncOutcomeState>(sync["outcome"] | static_cast<uint8_t>(0));
      s.koReaderSyncSession.resultSpineIndex = sync["resultSpineIndex"] | 0;
      s.koReaderSyncSession.resultPage = sync["resultPage"] | 0;
      s.koReaderSyncSession.resultParagraphIndex = sync["resultParagraphIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.resultHasParagraphIndex = sync["resultHasParagraphIndex"] | false;
      s.koReaderSyncSession.resultListItemIndex = sync["resultListItemIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.resultHasListItemIndex = sync["resultHasListItemIndex"] | false;
      s.koReaderSyncSession.exitToHomeAfterSync = sync["exitToHomeAfterSync"] | false;
      s.koReaderSyncSession.autoPullEpubPath = sync["autoPullEpubPath"] | std::string("");
    } else {
      s.koReaderSyncSession.clear();
    }
    JsonObjectConst jump = doc["pendingBookmarkJump"];
    if (!jump.isNull()) {
      s.pendingBookmarkJump.active = jump["active"] | false;
      s.pendingBookmarkJump.bookPath = jump["bookPath"] | std::string("");
      s.pendingBookmarkJump.spineIndex = jump["spineIndex"] | static_cast<uint16_t>(0);
      s.pendingBookmarkJump.pageNumber = jump["pageNumber"] | static_cast<uint16_t>(0);
    } else {
      s.pendingBookmarkJump.clear();
    }
    // Screensaver anti-repetition history
    memset(s.recentScreensaverImages, 0, sizeof(s.recentScreensaverImages));
    JsonArrayConst ssRecentArr = doc["recentScreensaverImages"];
    const int ssActualCount = ssRecentArr.isNull() ? 0
        : std::min(static_cast<int>(ssRecentArr.size()),
                   static_cast<int>(CrossPointState::SCREENSAVER_RECENT_COUNT));
    for (int i = 0; i < ssActualCount; i++) s.recentScreensaverImages[i] = ssRecentArr[i] | static_cast<uint16_t>(0);
    s.recentScreensaverPos = doc["recentScreensaverPos"] | static_cast<uint8_t>(0);
    if (s.recentScreensaverPos >= CrossPointState::SCREENSAVER_RECENT_COUNT)
      s.recentScreensaverPos = ssActualCount > 0 ? s.recentScreensaverPos % CrossPointState::SCREENSAVER_RECENT_COUNT : 0;
    s.recentScreensaverFill = doc["recentScreensaverFill"] | static_cast<uint8_t>(0);
    s.recentScreensaverFill = static_cast<uint8_t>(std::min(static_cast<int>(s.recentScreensaverFill), ssActualCount));
  }
  return true;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["cleanSleepRefresh"] = s.cleanSleepRefresh;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["fadingFix"] = s.fadingFix;
  doc["darkMode"] = s.darkMode;
  doc["antiGhostingExperimental"] = s.antiGhostingExperimental;

  doc["fontSize"] = s.fontSize;
  doc["fontSizeSchemaVersion"] = FONT_SIZE_SCHEMA_VERSION;
  doc["lineSpacing"] = s.lineSpacing;
  doc["screenMargin"] = s.screenMargin;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["bionicReading"] = s.bionicReading;
  doc["orientation"] = s.orientation;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["forceParagraphIndents"] = s.forceParagraphIndents;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["textDarkness"] = s.textDarkness;
  doc["textDarknessSchemaVersion"] = TEXT_DARKNESS_SCHEMA_VERSION;
  doc["readerRefreshMode"] = s.readerRefreshMode;
  doc["imageRendering"] = s.imageRendering;

  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonFollowOrientation"] = s.frontButtonFollowOrientation;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["tiltPageTurn"] = s.tiltPageTurn;

  doc["sleepTimeout"] = s.sleepTimeout;
  doc["showHiddenFiles"] = s.showHiddenFiles;
  doc["syncDayWifiChoice"] = s.syncDayWifiChoice;
  doc["syncDayReminderStarts"] = s.syncDayReminderStarts;
  doc["dateFormat"] = s.dateFormat;
  doc["dailyGoalTarget"] = s.dailyGoalTarget;
  doc["readingStatsAutoBackup"] = s.readingStatsAutoBackup;
  doc["flashcardStudyModeSchemaVersion"] = FLASHCARD_STUDY_MODE_SCHEMA_VERSION;
  doc["flashcardStudyMode"] = s.flashcardStudyMode;
  doc["flashcardSessionSize"] = s.flashcardSessionSize;
  doc["showStatsAfterReading"] = s.showStatsAfterReading;
  doc["moveCompletedBooks"] = s.moveCompletedBooks;
  doc["achievementsEnabled"] = s.achievementsEnabled;
  doc["achievementPopups"] = s.achievementPopups;

  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword_obf"] = obfuscation::obfuscateToBase64(s.opdsPassword);
  doc["opdsFilenameFormat"] = s.opdsFilenameFormat;
  doc["koSyncAutoPullOnOpen"] = s.koSyncAutoPullOnOpen;
  doc["koSyncAutoPushOnClose"] = s.koSyncAutoPushOnClose;

  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["statusBarClock"] = s.statusBarClock;
  doc["clockHasBeenSynced"] = s.clockHasBeenSynced;
  doc["xtcStatusBarMode"] = s.xtcStatusBarMode;

  // Front button remap - managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["homeBookSource"] = s.homeBookSource;
  doc["autoSyncDay"] = s.autoSyncDay;
  doc["sleepDirectory"] = s.sleepDirectory;
  doc["sleepImageOrder"] = s.sleepImageOrder;
  doc["timeZonePreset"] = TimeZoneRegistry::clampPresetIndex(s.timeZonePreset);
  doc["appsHubShortcutOrder"] = s.appsHubShortcutOrder;
  doc["browseFilesShortcut"] = s.browseFilesShortcut;
  doc["browseFilesShortcutOrder"] = s.browseFilesShortcutOrder;
  doc["syncDayShortcut"] = s.syncDayShortcut;
  doc["syncDayShortcutOrder"] = s.syncDayShortcutOrder;
  doc["settingsShortcut"] = s.settingsShortcut;
  doc["settingsShortcutOrder"] = s.settingsShortcutOrder;
  doc["readingStatsShortcut"] = s.readingStatsShortcut;
  doc["readingStatsShortcutOrder"] = s.readingStatsShortcutOrder;
  doc["readingHeatmapShortcut"] = s.readingHeatmapShortcut;
  doc["readingHeatmapShortcutOrder"] = s.readingHeatmapShortcutOrder;
  doc["readingProfileShortcut"] = s.readingProfileShortcut;
  doc["readingProfileShortcutOrder"] = s.readingProfileShortcutOrder;
  doc["achievementsShortcut"] = s.achievementsShortcut;
  doc["achievementsShortcutOrder"] = s.achievementsShortcutOrder;
  doc["ifFoundShortcut"] = s.ifFoundShortcut;
  doc["ifFoundShortcutOrder"] = s.ifFoundShortcutOrder;
  doc["readMeShortcut"] = s.readMeShortcut;
  doc["readMeShortcutOrder"] = s.readMeShortcutOrder;
  doc["recentBooksShortcut"] = s.recentBooksShortcut;
  doc["recentBooksShortcutOrder"] = s.recentBooksShortcutOrder;
  doc["bookmarksShortcut"] = s.bookmarksShortcut;
  doc["bookmarksShortcutOrder"] = s.bookmarksShortcutOrder;
  doc["favoritesShortcut"] = s.favoritesShortcut;
  doc["favoritesShortcutOrder"] = s.favoritesShortcutOrder;
  doc["flashcardsShortcut"] = s.flashcardsShortcut;
  doc["flashcardsShortcutOrder"] = s.flashcardsShortcutOrder;
  doc["dictionaryShortcut"] = s.dictionaryShortcut;
  doc["dictionaryShortcutOrder"] = s.dictionaryShortcutOrder;
  doc["fileTransferShortcut"] = s.fileTransferShortcut;
  doc["fileTransferShortcutOrder"] = s.fileTransferShortcutOrder;
  doc["screenCleanShortcut"] = s.screenCleanShortcut;
  doc["screenCleanShortcutOrder"] = s.screenCleanShortcutOrder;
  doc["sleepShortcut"] = s.sleepShortcut;
  doc["sleepShortcutOrder"] = s.sleepShortcutOrder;
  doc["opdsBrowserShortcut"] = s.opdsBrowserShortcut;
  doc["opdsBrowserShortcutOrder"] = s.opdsBrowserShortcutOrder;
  doc["browseFilesShortcutVisible"] = s.browseFilesShortcutVisible;
  doc["syncDayShortcutVisible"] = s.syncDayShortcutVisible;
  doc["settingsShortcutVisible"] = s.settingsShortcutVisible;
  doc["readingStatsShortcutVisible"] = s.readingStatsShortcutVisible;
  doc["readingHeatmapShortcutVisible"] = s.readingHeatmapShortcutVisible;
  doc["readingProfileShortcutVisible"] = s.readingProfileShortcutVisible;
  doc["achievementsShortcutVisible"] = s.achievementsShortcutVisible;
  doc["ifFoundShortcutVisible"] = s.ifFoundShortcutVisible;
  doc["readMeShortcutVisible"] = s.readMeShortcutVisible;
  doc["recentBooksShortcutVisible"] = s.recentBooksShortcutVisible;
  doc["bookmarksShortcutVisible"] = s.bookmarksShortcutVisible;
  doc["favoritesShortcutVisible"] = s.favoritesShortcutVisible;
  doc["flashcardsShortcutVisible"] = s.flashcardsShortcutVisible;
  doc["dictionaryShortcutVisible"] = s.dictionaryShortcutVisible;
  doc["fileTransferShortcutVisible"] = s.fileTransferShortcutVisible;
  doc["screenCleanShortcutVisible"] = s.screenCleanShortcutVisible;
  doc["sleepShortcutVisible"] = s.sleepShortcutVisible;
  doc["opdsBrowserShortcutVisible"] = s.opdsBrowserShortcutVisible;

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("CPS", std::string("State JSON parse error: ") + error.c_str());
    return false;
  }

  return loadSettingsDirect(s, doc, needsResave);
}


// ---- KOReaderCredentialStore ----

bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;

  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const auto& profile : store.profiles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = profile.name;
    obj["username"] = profile.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(profile.password);
    obj["serverUrl"] = profile.serverUrl;
    obj["matchMethod"] = static_cast<uint8_t>(profile.matchMethod);
    obj["sendMetadata"] = profile.sendMetadata;
    obj["syncBehavior"] = static_cast<uint8_t>(profile.syncBehavior);
  }
  doc["activeIndex"] = store.activeIndex;

  return saveJsonDocumentToFile("KRS", path, doc);
}

bool JsonSettingsIO::saveKOReaderLegacyMirror(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["username"] = store.getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(store.getPassword());
  doc["serverUrl"] = store.getServerUrl();
  doc["matchMethod"] = static_cast<uint8_t>(store.getMatchMethod());
  doc["sendMetadata"] = store.getSendMetadata();
  doc["syncBehavior"] = static_cast<uint8_t>(store.getSyncBehavior());
  return saveJsonDocumentToFile("KRS", path, doc);
}

bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("KRS", std::string("KOReader JSON parse error: ") + error.c_str());
    return false;
  }

  if (!doc["profiles"].isNull()) {
    // Current multi-profile format.
    store.profiles.clear();
    JsonArray arr = doc["profiles"].as<JsonArray>();
    for (JsonObject obj : arr) {
      if (store.profiles.size() >= KOReaderCredentialStore::MAX_PROFILES) break;
      KOReaderProfile profile;
      profile.name = obj["name"] | std::string("");
      profile.username = obj["username"] | std::string("");
      bool ok = false;
      profile.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
      if (!ok || profile.password.empty()) {
        profile.password = obj["password"] | std::string("");
        if (!profile.password.empty() && needsResave) *needsResave = true;
      }
      profile.serverUrl = obj["serverUrl"] | std::string("");
      uint8_t method = obj["matchMethod"] | (uint8_t)0;
      profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                                ? static_cast<DocumentMatchMethod>(method)
                                : DocumentMatchMethod::FILENAME;
      profile.sendMetadata = obj["sendMetadata"] | false;
      const uint8_t behavior = obj["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
      profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                                 ? static_cast<KOReaderSyncBehavior>(behavior)
                                 : KOReaderSyncBehavior::ASK_EVERY_TIME;
      store.profiles.push_back(std::move(profile));
    }
    const int active = doc["activeIndex"] | 0;
    store.setActiveIndex(active);
    LOG_DBG("KRS", "Loaded %u KOReader profiles (active=%d)", store.profiles.size(), store.getActiveIndex());
  } else if (store.profiles.empty()) {
    // Single legacy profile — migrate into the new multi-profile store.
    KOReaderProfile profile;
    profile.name = "Default";
    profile.username = doc["username"] | std::string("");
    bool ok2 = false;
    profile.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok2);
    if (!ok2 || profile.password.empty()) {
      profile.password = doc["password"] | std::string("");
      if (!profile.password.empty() && needsResave) *needsResave = true;
    }
    profile.serverUrl = doc["serverUrl"] | std::string("");
    const uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(DocumentMatchMethod::FILENAME);
    profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                              ? static_cast<DocumentMatchMethod>(method)
                              : DocumentMatchMethod::FILENAME;
    profile.sendMetadata = doc["sendMetadata"] | false;
    const uint8_t behavior =
        doc["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
    profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                               ? static_cast<KOReaderSyncBehavior>(behavior)
                               : KOReaderSyncBehavior::ASK_EVERY_TIME;
    store.profiles.push_back(std::move(profile));
    if (needsResave) *needsResave = true;
    LOG_DBG("KRS", "Migrated single legacy KOReader profile into multi-profile store");
  }

  return true;
}

bool JsonSettingsIO::loadKOReaderLegacyProfile(KOReaderProfile& profile, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "Legacy koreader.json parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("KRS", std::string("Legacy koreader.json parse error: ") + error.c_str());
    return false;
  }

  profile.username = doc["username"] | std::string("");
  bool ok = false;
  profile.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || profile.password.empty()) {
    profile.password = doc["password"] | std::string("");
  }
  profile.serverUrl = doc["serverUrl"] | std::string("");
  const uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(DocumentMatchMethod::FILENAME);
  profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                            ? static_cast<DocumentMatchMethod>(method)
                            : DocumentMatchMethod::FILENAME;
  profile.sendMetadata = doc["sendMetadata"] | false;
  const uint8_t behavior =
      doc["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
  profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                             ? static_cast<KOReaderSyncBehavior>(behavior)
                             : KOReaderSyncBehavior::ASK_EVERY_TIME;

  LOG_DBG("KRS", "Loaded legacy KOReader credentials for user: %s", profile.username.c_str());
  return true;
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = store.getLastConnectedSsid();

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : store.getCredentials()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }

  return saveJsonDocumentToFile("WCS", path, doc);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("WCS", std::string("WiFi JSON parse error: ") + error.c_str());
    return false;
  }

  store.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");

  store.credentials.clear();
  JsonArray arr = doc["credentials"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.credentials.size() >= store.MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    bool ok = false;
    cred.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty() && needsResave) *needsResave = true;
    }
    store.credentials.push_back(cred);
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", store.credentials.size());
  return true;
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("RBS", path, doc);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  const int js0Free = static_cast<int>(ESP.getFreeHeap());
  const int js0Max = static_cast<int>(ESP.getMaxAllocHeap());
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  LOG_DBG("HCR-FRAG", "JsonDocument deserialize: free=%d->%d maxA=%d->%d frag=%d", js0Free,
          static_cast<int>(ESP.getFreeHeap()), js0Max, static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
  if (error) {
    LOG_ERR("RBS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("RBS", std::string("Recent books JSON parse error: ") + error.c_str());
    return false;
  }
  const int js1Free = static_cast<int>(ESP.getFreeHeap());
  const int js1Max = static_cast<int>(ESP.getMaxAllocHeap());

  store.recentBooks.clear();
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);
  JsonArray arr = doc["books"].as<JsonArray>();
  int count = 0;
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    if (formatVersion < 2) {
      book.bookId.clear();
    }
    store.recentBooks.push_back(book);
    count++;
    if ((count & 0x3) == 0) {
      LOG_DBG("HCR-FRAG", "  RBS after %d books: free=%d maxA=%d frag=%d", count,
              static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
              static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
    }
  }
  LOG_DBG("HCR-FRAG", "RBS %d books: free=%d->%d maxA=%d->%d frag=%d", count, js1Free,
          static_cast<int>(ESP.getFreeHeap()), js1Max, static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));

  store.normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- FavoritesStore ----

bool JsonSettingsIO::saveFavorites(const FavoritesStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 1;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("FAV", path, doc);
}

bool JsonSettingsIO::loadFavorites(FavoritesStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("FAV", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("FAV", std::string("Favorites JSON parse error: ") + error.c_str());
    return false;
  }

  store.favoriteBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    FavoriteBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.favoriteBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("FAV", "Favorites loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- ReadingStatsStore ----

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 6;

  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.getReadingDays()) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray legacyDays = doc["legacyReadingDays"].to<JsonArray>();
  for (const auto& day : store.legacyReadingDays) {
    JsonObject dayObj = legacyDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray sessionLog = doc["sessionLog"].to<JsonArray>();
  for (const auto& session : store.getSessionLog()) {
    JsonObject sessionObj = sessionLog.add<JsonObject>();
    sessionObj["dayOrdinal"] = session.dayOrdinal;
    sessionObj["sessionMs"] = session.sessionMs;
    if (!session.bookId.empty()) {
      sessionObj["bookId"] = session.bookId;
    }
    if (!session.path.empty()) {
      sessionObj["path"] = session.path;
    }
  }

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = books.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    JsonArray knownPaths = obj["knownPaths"].to<JsonArray>();
    for (const auto& knownPath : book.knownPaths) {
      knownPaths.add(knownPath);
    }
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["chapterTitle"] = book.chapterTitle;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["firstReadAt"] = book.firstReadAt;
    obj["lastReadAt"] = book.lastReadAt;
    obj["completedAt"] = book.completedAt;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["chapterProgressPercent"] = book.chapterProgressPercent;
    obj["completed"] = book.completed;
    if (book.avgSecondsPerForwardPage > 0) {
      obj["avgSecondsPerForwardPage"] = book.avgSecondsPerForwardPage;
      obj["paceSampleCount"] = book.paceSampleCount;
    }

    JsonArray bookDays = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = bookDays.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }

  return saveJsonDocumentToFile("RST", path, doc);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error || doc.overflowed()) {
    const char* message = error ? error.c_str() : "document overflow";
    LOG_ERR("RST", "JSON parse error: %s", message);
    CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats JSON parse error: ") + message);
    return false;
  }
  return loadReadingStatsDocument(store, doc);
}

bool JsonSettingsIO::loadReadingStatsDocument(ReadingStatsStore& store, const JsonDocument& doc) {
  // Validate document structure (upstream 1.5.0.3: JsonObjectConst/JsonArrayConst checks)
  if (!doc.is<JsonObjectConst>()) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats root is not a JSON object");
    return false;
  }

  const JsonVariantConst formatValue = doc["formatVersion"];
  if (!formatValue.isNull() && !formatValue.is<uint32_t>()) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats formatVersion is not an unsigned integer");
    return false;
  }
  const uint32_t formatVersion = formatValue | static_cast<uint32_t>(1);
  if (formatVersion == 0 || formatVersion > 6) {
    CPR_VCODEX_LOG_EVENT("RST", std::string("Unsupported reading stats formatVersion: ") +
                                   std::to_string(formatVersion));
    return false;
  }

  static constexpr const char* ARRAY_KEYS[] = {"readingDays", "legacyReadingDays", "sessionLog", "books"};
  bool hasStatsArray = false;
  bool missingCurrentArray = false;
  for (const char* key : ARRAY_KEYS) {
    const JsonVariantConst value = doc[key];
    if (value.isNull()) {
      missingCurrentArray = missingCurrentArray || formatVersion >= 6;
      continue;
    }
    if (!value.is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats field is not an array: ") + key);
      return false;
    }
    hasStatsArray = true;
  }
  if (!hasStatsArray) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats document has no recognized data arrays");
    return false;
  }

  for (JsonVariantConst value : doc["books"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats books contains a non-object entry");
      return false;
    }
    const JsonObjectConst obj = value.as<JsonObjectConst>();
    if (!obj["knownPaths"].isNull() && !obj["knownPaths"].is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats knownPaths is not an array");
      return false;
    }
    if (formatVersion >= 2 && !obj["readingDays"].isNull() && !obj["readingDays"].is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats book readingDays is not an array");
      return false;
    }
  }
  for (JsonVariantConst value : doc["sessionLog"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats sessionLog contains a non-object entry");
      return false;
    }
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = missingCurrentArray;

  // FRAGMENTATION FIX: reserve the top-level containers for the actual number
  // of parsed elements so each vector grows once (contiguously) instead of
  // reallocating repeatedly in the middle of the heap and fragmenting it.
  store.books.reserve(doc["books"].size());
  store.readingDays.reserve(doc["readingDays"].size());
  store.legacyReadingDays.reserve(doc["legacyReadingDays"].size());
  store.sessionLog.reserve(doc["sessionLog"].size());

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArrayConst source) {
    for (JsonVariantConst value : source) {
      ReadingDayStats day;
      if (value.is<JsonObjectConst>()) {
        JsonObjectConst obj = value.as<JsonObjectConst>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArrayConst>());
  std::vector<ReadingDayStats> declaredReadingDays = store.readingDays;
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArrayConst>());
    if (formatVersion < 6 && store.legacyReadingDays.empty()) {
      store.legacyReadingDays = store.readingDays;
    }
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObjectConst sessionObj : doc["sessionLog"].as<JsonArrayConst>()) {
      ReadingSessionLogEntry session;
      session.dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      session.sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      session.bookId = sessionObj["bookId"] | std::string("");
      session.path = BookIdentity::normalizePath(sessionObj["path"] | std::string(""));
      if (session.dayOrdinal != 0 && session.sessionMs != 0) {
        store.sessionLog.push_back(session);
      }
    }
  } else {
    store.dirty = true;
  }

  JsonArrayConst books = doc["books"].as<JsonArrayConst>();
  int loadedBookCount = 0;
  for (JsonObjectConst obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    book.knownPaths.reserve(obj["knownPaths"].size());  // avoid realloc churn in the hot path
    for (JsonVariantConst value : obj["knownPaths"].as<JsonArrayConst>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.completedAt = obj["completedAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    book.avgSecondsPerForwardPage = obj["avgSecondsPerForwardPage"] | static_cast<uint16_t>(0);
    book.paceSampleCount = obj["paceSampleCount"] | static_cast<uint16_t>(0);
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArrayConst>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
    ++loadedBookCount;
    if ((loadedBookCount & 0x5) == 0) {
      LOG_DBG("HCR-FRAG", "  RST %d/%d books: free=%d maxA=%d frag=%d", loadedBookCount,
              static_cast<int>(store.books.capacity()), static_cast<int>(ESP.getFreeHeap()),
              static_cast<int>(ESP.getMaxAllocHeap()),
              static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
    }
  }

  if (formatVersion < 6) {
    store.convertLegacyReadingDaysToUnassigned();
    store.dirty = true;
  }
  store.rebuildAggregatedReadingDays();

  // Upstream reconciliation: detect and recover aggregate mismatches without
  // discarding stored data. Surplus from declared days is forwarded to
  // legacyReadingDays so it remains visible for manual correction tools.
  if (formatVersion >= 6) {
    auto normalizeDays = [](std::vector<ReadingDayStats>& days) {
      std::sort(days.begin(), days.end(), [](const ReadingDayStats& left, const ReadingDayStats& right) {
        return left.dayOrdinal < right.dayOrdinal;
      });
      size_t writeIndex = 0;
      for (const auto& day : days) {
        if (day.dayOrdinal == 0 || day.readingMs == 0) {
          continue;
        }
        if (writeIndex > 0 && days[writeIndex - 1].dayOrdinal == day.dayOrdinal) {
          days[writeIndex - 1].readingMs += day.readingMs;
        } else {
          days[writeIndex++] = day;
        }
      }
      days.resize(writeIndex);
    };
    normalizeDays(declaredReadingDays);

    bool aggregateMismatch = declaredReadingDays.size() != store.readingDays.size();
    for (const auto& declaredDay : declaredReadingDays) {
      const auto rebuiltIt =
          std::lower_bound(store.readingDays.begin(), store.readingDays.end(), declaredDay.dayOrdinal,
                           [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
      const bool hasRebuiltDay =
          rebuiltIt != store.readingDays.end() && rebuiltIt->dayOrdinal == declaredDay.dayOrdinal;
      const uint64_t rebuiltMs = hasRebuiltDay ? rebuiltIt->readingMs : 0;
      if (rebuiltMs != declaredDay.readingMs) {
        aggregateMismatch = true;
      }
      if (declaredDay.readingMs > rebuiltMs) {
        store.legacyReadingDays.push_back(
            ReadingDayStats{declaredDay.dayOrdinal, declaredDay.readingMs - rebuiltMs});
      }
    }

    if (aggregateMismatch) {
      normalizeDays(store.legacyReadingDays);
      store.rebuildAggregatedReadingDays();
      store.dirty = true;
      CPR_VCODEX_LOG_EVENT("RST", "Reconciled reading stats aggregate totals without discarding stored data");
    }
  }

  // Sort sessionLog by day ordinal for consistent iteration order (upstream 1.5.0.3)
  std::stable_sort(store.sessionLog.begin(), store.sessionLog.end(),
                   [](const ReadingSessionLogEntry& left, const ReadingSessionLogEntry& right) {
                     return left.dayOrdinal < right.dayOrdinal;
                   });
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  JsonDocument doc;
  const bool parsed = loadJsonDocumentFromFile("RST", path, doc);
  const bool loaded = parsed && !doc.overflowed() && loadReadingStatsDocument(store, doc);
  if (!loaded) {
    CPR_VCODEX_LOG_EVENT("RST", std::string("Failed to load reading stats from ") + path);
  }
  return loaded;
}

// ---- AchievementsStore ----

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  doc["accumulatedReadingMs"] = store.accumulatedReadingMs;
  doc["countedSessions"] = store.countedSessions;
  doc["totalBookmarksAdded"] = store.totalBookmarksAdded;
  doc["longestSessionMs"] = store.longestSessionMs;
  doc["goalDaysCount"] = store.goalDaysCount;
  doc["currentGoalStreak"] = store.currentGoalStreak;
  doc["maxGoalStreak"] = store.maxGoalStreak;
  doc["lastGoalDayOrdinal"] = store.lastGoalDayOrdinal;
  doc["resetDayOrdinal"] = store.resetDayOrdinal;
  doc["resetDayBaselineMs"] = store.resetDayBaselineMs;

  JsonArray states = doc["states"].to<JsonArray>();
  for (const auto& state : store.states) {
    JsonObject obj = states.add<JsonObject>();
    obj["unlocked"] = state.unlocked;
    obj["unlockedAt"] = state.unlockedAt;
  }

  JsonArray startedBooks = doc["startedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.startedBooks) {
    startedBooks.add(pathValue);
  }

  JsonArray finishedBooks = doc["finishedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.finishedBooks) {
    finishedBooks.add(pathValue);
  }

  return saveJsonDocumentToFile("ACH", path, doc);
}

bool JsonSettingsIO::loadAchievements(AchievementsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ACH", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Achievements JSON parse error: ") + error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  store.dirty = false;
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  // Session serials are runtime-only; persisted values collide after ReadingStatsStore resets on reboot.
  store.lastProcessedSessionSerial = 0;

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}

bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Achievements file empty or unreadable: ") + path);
    return false;
  }
  const bool loaded = loadAchievements(store, json.c_str());
  if (!loaded) {
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Failed to load achievements from ") + path);
  }
  return loaded;
}

// ---- OpdsServerStore ----
// Follows the same save/load pattern as WifiCredentialStore above.
// Passwords are XOR-obfuscated with the device MAC and base64-encoded ("password_obf" key).

bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument doc;

  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : store.getServers()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("OPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.servers.clear();
  JsonArray arr = doc["servers"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.servers.size() >= OpdsServerStore::MAX_SERVERS) break;
    OpdsServer server;
    server.name = obj["name"] | std::string("");
    server.url = obj["url"] | std::string("");
    server.username = obj["username"] | std::string("");
    // Try the obfuscated key first; fall back to plaintext "password" for
    // files written before obfuscation was added (or hand-edited JSON).
    bool ok = false;
    server.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || server.password.empty()) {
      server.password = obj["password"] | std::string("");
      if (!server.password.empty() && needsResave) *needsResave = true;
    }
    store.servers.push_back(std::move(server));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", store.servers.size());
  return true;
}
