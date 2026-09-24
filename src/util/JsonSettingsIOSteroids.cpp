#include <ArduinoJson.h>
#include <cstring>

#include "JsonSettingsIOSteroids.h"

#include "CrossPointSettings.h"
#include "util/ShortcutRegistry.h"

namespace JsonSettingsIOSteroids {

namespace {

uint8_t clampEnum(const uint8_t value, const uint8_t count, const uint8_t def) {
  return value < count ? value : def;
}

void copyString(char* dst, const size_t cap, const char* src) {
  if (cap == 0) return;
  std::strncpy(dst, src != nullptr ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

void loadString(const JsonDocument& doc, const char* key, char* dest, const size_t maxLen) {
  const std::string value = doc[key] | std::string(dest);
  std::strncpy(dest, value.c_str(), maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

// Migrate the legacy `statsShortcut` location to the current `readingStatsShortcut`
// field. Older Steroids builds stored the Reading Stats shortcut under the generic
// `stats*` keys; newer builds use `readingStats*`. When the legacy keys are present
// but the current keys are absent, copy the values over and request a resave.
void migrateLegacyStatsShortcut(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  const bool hasLegacyStatsShortcut =
      !doc["statsShortcut"].isNull() || !doc["statsShortcutOrder"].isNull() || !doc["statsShortcutVisible"].isNull();
  if (!hasLegacyStatsShortcut) {
    return;
  }

  const bool legacyVisible = s.statsShortcutVisible != 0;
  const auto legacyLocation = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(s.statsShortcut);
  if (legacyVisible && (legacyLocation == CrossPointSettings::SHORTCUT_HOME || legacyLocation == CrossPointSettings::SHORTCUT_APPS)) {
    if (doc["readingStatsShortcut"].isNull() && doc["readingStatsShortcutOrder"].isNull() &&
        doc["readingStatsShortcutVisible"].isNull()) {
      s.readingStatsShortcut = s.statsShortcut;
      s.readingStatsShortcutOrder = s.statsShortcutOrder;
      s.readingStatsShortcutVisible = s.statsShortcutVisible;
      if (needsResave) *needsResave = true;
    }
  }
}

void loadSteroidsSettings(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  const uint8_t shortcutLocationCount = CrossPointSettings::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);

  // Shortcuts
  s.appsHubShortcutOrder =
      clampEnum(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount, s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clampEnum(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder =
      clampEnum(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount, s.browseFilesShortcutOrder);
  s.statsShortcut = clampEnum(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clampEnum(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clampEnum(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clampEnum(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clampEnum(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clampEnum(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clampEnum(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder =
      clampEnum(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder, shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut =
      clampEnum(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount, s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder =
      clampEnum(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder, shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut =
      clampEnum(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount, s.readingProfileShortcut);
  s.readingProfileShortcutOrder =
      clampEnum(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder, shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clampEnum(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder =
      clampEnum(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder, shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clampEnum(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clampEnum(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clampEnum(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clampEnum(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clampEnum(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder =
      clampEnum(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount, s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clampEnum(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clampEnum(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clampEnum(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clampEnum(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clampEnum(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clampEnum(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.dictionaryShortcut =
      clampEnum(doc["dictionaryShortcut"] | s.dictionaryShortcut, shortcutLocationCount, s.dictionaryShortcut);
  s.dictionaryShortcutOrder =
      clampEnum(doc["dictionaryShortcutOrder"] | s.dictionaryShortcutOrder, shortcutOrderCount, s.dictionaryShortcutOrder);
  s.fileTransferShortcut =
      clampEnum(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder =
      clampEnum(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder, shortcutOrderCount, s.fileTransferShortcutOrder);
  s.screenCleanShortcut =
      clampEnum(doc["screenCleanShortcut"] | s.screenCleanShortcut, shortcutLocationCount, s.screenCleanShortcut);
  s.screenCleanShortcutOrder =
      clampEnum(doc["screenCleanShortcutOrder"] | s.screenCleanShortcutOrder, shortcutOrderCount, s.screenCleanShortcutOrder);
  s.sleepShortcut = clampEnum(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
  s.sleepShortcutOrder =
      clampEnum(doc["sleepShortcutOrder"] | s.sleepShortcutOrder, shortcutOrderCount, s.sleepShortcutOrder);
  s.opdsBrowserShortcut =
      clampEnum(doc["opdsBrowserShortcut"] | s.opdsBrowserShortcut, shortcutLocationCount, s.opdsBrowserShortcut);
  s.opdsBrowserShortcutOrder =
      clampEnum(doc["opdsBrowserShortcutOrder"] | s.opdsBrowserShortcutOrder, shortcutOrderCount, s.opdsBrowserShortcutOrder);
  s.quickCardsShortcut =
      clampEnum(doc["quickCardsShortcut"] | s.quickCardsShortcut, shortcutLocationCount, s.quickCardsShortcut);
  s.quickCardsShortcutOrder =
      clampEnum(doc["quickCardsShortcutOrder"] | s.quickCardsShortcutOrder, shortcutOrderCount, s.quickCardsShortcutOrder);
  s.clippingsShortcut =
      clampEnum(doc["clippingsShortcut"] | s.clippingsShortcut, shortcutLocationCount, s.clippingsShortcut);
  s.clippingsShortcutOrder =
      clampEnum(doc["clippingsShortcutOrder"] | s.clippingsShortcutOrder, shortcutOrderCount, s.clippingsShortcutOrder);
  s.wikipediaShortcut =
      clampEnum(doc["wikipediaShortcut"] | s.wikipediaShortcut, shortcutLocationCount, s.wikipediaShortcut);
  s.wikipediaShortcutOrder =
      clampEnum(doc["wikipediaShortcutOrder"] | s.wikipediaShortcutOrder, shortcutOrderCount, s.wikipediaShortcutOrder);
  s.pluginsShortcut = clampEnum(doc["pluginsShortcut"] | s.pluginsShortcut, shortcutLocationCount, s.pluginsShortcut);
  s.pluginsShortcutOrder =
      clampEnum(doc["pluginsShortcutOrder"] | s.pluginsShortcutOrder, shortcutOrderCount, s.pluginsShortcutOrder);

  s.browseFilesShortcutVisible = clampEnum(doc["browseFilesShortcutVisible"] | s.browseFilesShortcutVisible, 2, s.browseFilesShortcutVisible);
  s.statsShortcutVisible = clampEnum(doc["statsShortcutVisible"] | s.statsShortcutVisible, 2, s.statsShortcutVisible);
  s.syncDayShortcutVisible = clampEnum(doc["syncDayShortcutVisible"] | s.syncDayShortcutVisible, 2, s.syncDayShortcutVisible);
  s.settingsShortcutVisible = clampEnum(doc["settingsShortcutVisible"] | s.settingsShortcutVisible, 2, s.settingsShortcutVisible);
  s.readingStatsShortcutVisible =
      clampEnum(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible, 2, s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible =
      clampEnum(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible, 2, s.readingHeatmapShortcutVisible);
  s.readingProfileShortcutVisible =
      clampEnum(doc["readingProfileShortcutVisible"] | s.readingProfileShortcutVisible, 2, s.readingProfileShortcutVisible);
  s.achievementsShortcutVisible =
      clampEnum(doc["achievementsShortcutVisible"] | s.achievementsShortcutVisible, 2, s.achievementsShortcutVisible);
  s.ifFoundShortcutVisible = clampEnum(doc["ifFoundShortcutVisible"] | s.ifFoundShortcutVisible, 2, s.ifFoundShortcutVisible);
  s.readMeShortcutVisible = clampEnum(doc["readMeShortcutVisible"] | s.readMeShortcutVisible, 2, s.readMeShortcutVisible);
  s.recentBooksShortcutVisible =
      clampEnum(doc["recentBooksShortcutVisible"] | s.recentBooksShortcutVisible, 2, s.recentBooksShortcutVisible);
  s.bookmarksShortcutVisible =
      clampEnum(doc["bookmarksShortcutVisible"] | s.bookmarksShortcutVisible, 2, s.bookmarksShortcutVisible);
  s.favoritesShortcutVisible =
      clampEnum(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible, 2, s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible =
      clampEnum(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible, 2, s.flashcardsShortcutVisible);
  s.dictionaryShortcutVisible =
      clampEnum(doc["dictionaryShortcutVisible"] | s.dictionaryShortcutVisible, 2, s.dictionaryShortcutVisible);
  s.fileTransferShortcutVisible =
      clampEnum(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible, 2, s.fileTransferShortcutVisible);
  s.screenCleanShortcutVisible =
      clampEnum(doc["screenCleanShortcutVisible"] | s.screenCleanShortcutVisible, 2, s.screenCleanShortcutVisible);
  s.sleepShortcutVisible = clampEnum(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, 2, s.sleepShortcutVisible);
  s.opdsBrowserShortcutVisible =
      clampEnum(doc["opdsBrowserShortcutVisible"] | s.opdsBrowserShortcutVisible, 2, s.opdsBrowserShortcutVisible);
  s.quickCardsShortcutVisible =
      clampEnum(doc["quickCardsShortcutVisible"] | s.quickCardsShortcutVisible, 2, s.quickCardsShortcutVisible);
  s.clippingsShortcutVisible =
      clampEnum(doc["clippingsShortcutVisible"] | s.clippingsShortcutVisible, 2, s.clippingsShortcutVisible);
  s.wikipediaShortcutVisible =
      clampEnum(doc["wikipediaShortcutVisible"] | s.wikipediaShortcutVisible, 2, s.wikipediaShortcutVisible);
  s.pluginsShortcutVisible = clampEnum(doc["pluginsShortcutVisible"] | s.pluginsShortcutVisible, 2, s.pluginsShortcutVisible);

  // Library (Steroids-only feature).
  s.libraryShortcut =
      clampEnum(doc["libraryShortcut"] | s.libraryShortcut, shortcutLocationCount, s.libraryShortcut);
  s.libraryShortcutOrder =
      clampEnum(doc["libraryShortcutOrder"] | s.libraryShortcutOrder, shortcutOrderCount, s.libraryShortcutOrder);
  s.libraryShortcutVisible =
      clampEnum(doc["libraryShortcutVisible"] | s.libraryShortcutVisible, 2, s.libraryShortcutVisible);
  s.libraryLayout = clampEnum(doc["libraryLayout"] | s.libraryLayout,
                              static_cast<uint8_t>(CrossPointSettings::LIBRARY_LAYOUT_COUNT - 1),
                              s.libraryLayout);
  s.libraryFilter = clampEnum(doc["libraryFilter"] | s.libraryFilter,
                              static_cast<uint8_t>(CrossPointSettings::LIBRARY_FILTER_COUNT - 1),
                              s.libraryFilter);
  s.librarySort = clampEnum(doc["librarySort"] | s.librarySort,
                            static_cast<uint8_t>(CrossPointSettings::LIBRARY_SORT_COUNT - 1),
                            s.librarySort);
  s.libraryViewMode = clampEnum(doc["libraryViewMode"] | s.libraryViewMode, 4, s.libraryViewMode);
  s.libraryUpdateMode =
      clampEnum(doc["libraryUpdateMode"] | s.libraryUpdateMode,
                static_cast<uint8_t>(CrossPointSettings::LIBRARY_UPDATE_MODE_COUNT - 1),
                s.libraryUpdateMode);
  s.libraryFolderCollections =
      clampEnum(doc["libraryFolderCollections"] | s.libraryFolderCollections, 2, s.libraryFolderCollections);
  s.libraryMetadataSeries =
      clampEnum(doc["libraryMetadataSeries"] | s.libraryMetadataSeries, 2, s.libraryMetadataSeries);
  s.librarySelectorIndex = doc["librarySelectorIndex"] | s.librarySelectorIndex;
  s.libraryCollectionIdx = doc["libraryCollectionIdx"] | s.libraryCollectionIdx;
  loadString(doc, "libraryCollectionName", s.libraryCollectionName, sizeof(s.libraryCollectionName));
  loadString(doc, "librarySearchText", s.librarySearchText, sizeof(s.librarySearchText));
  loadString(doc, "libraryRootDir", s.libraryRootDir, sizeof(s.libraryRootDir));
  s.libraryLastCleanupDay = doc["libraryLastCleanupDay"] | s.libraryLastCleanupDay;

  migrateLegacyStatsShortcut(s, doc, needsResave);
  normalizeShortcutOrderSettings(s);
  CrossPointSettings::validateFrontButtonMapping(s);

  // ScreenSaver
  copyString(s.screenSaverDirectory, sizeof(s.screenSaverDirectory),
             doc["screenSaverDirectory"] | static_cast<const char*>(s.screenSaverDirectory));
  s.screenSaverOrder =
      clampEnum(doc["screenSaverOrder"] | s.screenSaverOrder, CrossPointSettings::SCREENSAVER_ORDER_COUNT, s.screenSaverOrder);
  s.screenSaverInterval =
      clampEnum(doc["screenSaverInterval"] | s.screenSaverInterval, CrossPointSettings::SCREENSAVER_INTERVAL_COUNT, s.screenSaverInterval);
  s.screenSaverWakeButton = clampEnum(doc["screenSaverWakeButton"] | s.screenSaverWakeButton,
                                      CrossPointSettings::SCREENSAVER_WAKE_BUTTON_COUNT, s.screenSaverWakeButton);
  copyString(s.screenSaverText, sizeof(s.screenSaverText),
             doc["screenSaverText"] | static_cast<const char*>(s.screenSaverText));
  s.screenSaverFontSize =
      clampEnum(doc["screenSaverFontSize"] | s.screenSaverFontSize, CrossPointSettings::SCREENSAVER_FONT_SIZE_COUNT, s.screenSaverFontSize);
  s.screenSaverTextPosition = clampEnum(doc["screenSaverTextPosition"] | s.screenSaverTextPosition,
                                        CrossPointSettings::SCREENSAVER_TEXT_POSITION_COUNT, s.screenSaverTextPosition);
  s.screenSaverTextStyle = clampEnum(doc["screenSaverTextStyle"] | s.screenSaverTextStyle,
                                     CrossPointSettings::SCREENSAVER_TEXT_STYLE_COUNT, s.screenSaverTextStyle);
  s.screenSaverShowPanel = (doc["screenSaverShowPanel"] | s.screenSaverShowPanel) != 0 ? 1 : 0;
  s.screenSaverPanelColor = (doc["screenSaverPanelColor"] | s.screenSaverPanelColor) != 0 ? 1 : 0;
  s.screenSaverPanelOpacity =
      clampEnum(doc["screenSaverPanelOpacity"] | s.screenSaverPanelOpacity, 4, s.screenSaverPanelOpacity);
  s.screenSaverMinBattery =
      clampEnum(doc["screenSaverMinBattery"] | s.screenSaverMinBattery, 9, s.screenSaverMinBattery);
  s.screenSaverReplaceSleep = (doc["screenSaverReplaceSleep"] | s.screenSaverReplaceSleep) != 0 ? 1 : 0;
  copyString(s.screenSaverReaderDir, sizeof(s.screenSaverReaderDir),
             doc["screenSaverReaderDir"] | static_cast<const char*>(s.screenSaverReaderDir));
  s.screenSaverReaderOrder =
      clampEnum(doc["screenSaverReaderOrder"] | s.screenSaverReaderOrder, CrossPointSettings::SCREENSAVER_ORDER_COUNT, s.screenSaverReaderOrder);
  s.cycleScreensaverOnTap = (doc["cycleScreensaverOnTap"] | s.cycleScreensaverOnTap) != 0 ? 1 : 0;
  s.screenSaverShortcut = doc["screenSaverShortcut"] | s.screenSaverShortcut;
  s.screenSaverShortcutOrder = doc["screenSaverShortcutOrder"] | s.screenSaverShortcutOrder;
  s.screenSaverShortcutVisible = (doc["screenSaverShortcutVisible"] | s.screenSaverShortcutVisible) != 0 ? 1 : 0;
}

void saveSteroidsSettings(const CrossPointSettings& s, JsonDocument& doc) {
  // Shortcuts
  doc["appsHubShortcutOrder"] = s.appsHubShortcutOrder;
  doc["browseFilesShortcut"] = s.browseFilesShortcut;
  doc["browseFilesShortcutOrder"] = s.browseFilesShortcutOrder;
  doc["statsShortcut"] = s.statsShortcut;
  doc["statsShortcutOrder"] = s.statsShortcutOrder;
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
  doc["quickCardsShortcut"] = s.quickCardsShortcut;
  doc["quickCardsShortcutOrder"] = s.quickCardsShortcutOrder;
  doc["clippingsShortcut"] = s.clippingsShortcut;
  doc["clippingsShortcutOrder"] = s.clippingsShortcutOrder;
  doc["wikipediaShortcut"] = s.wikipediaShortcut;
  doc["wikipediaShortcutOrder"] = s.wikipediaShortcutOrder;
  doc["pluginsShortcut"] = s.pluginsShortcut;
  doc["pluginsShortcutOrder"] = s.pluginsShortcutOrder;

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
  doc["quickCardsShortcutVisible"] = s.quickCardsShortcutVisible;
  doc["clippingsShortcutVisible"] = s.clippingsShortcutVisible;
  doc["wikipediaShortcutVisible"] = s.wikipediaShortcutVisible;
  doc["pluginsShortcutVisible"] = s.pluginsShortcutVisible;

  // Library (Steroids-only feature).
  doc["libraryShortcut"] = s.libraryShortcut;
  doc["libraryShortcutOrder"] = s.libraryShortcutOrder;
  doc["libraryShortcutVisible"] = s.libraryShortcutVisible;
  doc["libraryLayout"] = s.libraryLayout;
  doc["libraryFilter"] = s.libraryFilter;
  doc["librarySort"] = s.librarySort;
  doc["libraryViewMode"] = s.libraryViewMode;
  doc["libraryUpdateMode"] = s.libraryUpdateMode;
  doc["libraryFolderCollections"] = s.libraryFolderCollections;
  doc["libraryMetadataSeries"] = s.libraryMetadataSeries;
  doc["librarySelectorIndex"] = s.librarySelectorIndex;
  doc["libraryCollectionIdx"] = s.libraryCollectionIdx;
  doc["libraryCollectionName"] = s.libraryCollectionName;
  doc["librarySearchText"] = s.librarySearchText;
  doc["libraryRootDir"] = s.libraryRootDir;
  doc["libraryLastCleanupDay"] = s.libraryLastCleanupDay;

  // ScreenSaver
  doc["screenSaverDirectory"] = s.screenSaverDirectory;
  doc["screenSaverOrder"] = s.screenSaverOrder;
  doc["screenSaverInterval"] = s.screenSaverInterval;
  doc["screenSaverWakeButton"] = s.screenSaverWakeButton;
  doc["screenSaverText"] = s.screenSaverText;
  doc["screenSaverFontSize"] = s.screenSaverFontSize;
  doc["screenSaverTextPosition"] = s.screenSaverTextPosition;
  doc["screenSaverTextStyle"] = s.screenSaverTextStyle;
  doc["screenSaverShowPanel"] = s.screenSaverShowPanel;
  doc["screenSaverPanelColor"] = s.screenSaverPanelColor;
  doc["screenSaverPanelOpacity"] = s.screenSaverPanelOpacity;
  doc["screenSaverMinBattery"] = s.screenSaverMinBattery;
  doc["screenSaverReplaceSleep"] = s.screenSaverReplaceSleep;
  doc["screenSaverReaderDir"] = s.screenSaverReaderDir;
  doc["screenSaverReaderOrder"] = s.screenSaverReaderOrder;
  doc["cycleScreensaverOnTap"] = s.cycleScreensaverOnTap;
  doc["screenSaverShortcut"] = s.screenSaverShortcut;
  doc["screenSaverShortcutOrder"] = s.screenSaverShortcutOrder;
  doc["screenSaverShortcutVisible"] = s.screenSaverShortcutVisible;
}

}  // namespace JsonSettingsIOSteroids
