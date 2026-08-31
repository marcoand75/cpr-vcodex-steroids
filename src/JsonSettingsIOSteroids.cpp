#include "JsonSettingsIOSteroids.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "SettingsList.h"
#include "util/CprVcodexLogs.h"
#include "util/ShortcutRegistry.h"
#include "activities/reader/ReaderUtils.h"

// Shared internal helpers (saveJsonDocumentToFile, migrateStoredUiTheme, etc.)
#include "JsonSettingsIOShared.inc"

using S = CrossPointSettings;

namespace {

void writeSteroidsSettingsDoc(JsonDocument& doc, const CrossPointSettings& s) {
  doc["formatVersion"] = 1;

  doc["cycleScreensaverOnTap"] = s.cycleScreensaverOnTap;

  doc["guideReadingEnabled"] = s.guideReadingEnabled;
  doc["dotsSpacing"] = s.dotsSpacing;
  doc["epubRenderMode"] = s.epubRenderMode;

  doc["frontLongPressBehavior"] = s.frontLongPressBehavior;
  doc["selectLongPress"] = s.selectLongPress;

  // New per-directional button behavior settings
  doc["longPressUpBehavior"] = s.longPressUpBehavior;
  doc["longPressDownBehavior"] = s.longPressDownBehavior;
  doc["frontLongPressLeftBehavior"] = s.frontLongPressLeftBehavior;
  doc["frontLongPressRightBehavior"] = s.frontLongPressRightBehavior;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["selectLongPressBehavior"] = s.selectLongPressBehavior;

  doc["uiTheme"] = s.uiTheme;
  doc["uiThemeSchemaVersion"] = UI_THEME_SCHEMA_VERSION;
  doc["fontFamily"] = s.fontFamily;
  doc["fontFamilySchemaVersion"] = FONT_FAMILY_SCHEMA_VERSION;
  if (s.sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = s.sdFontFamilyName;
  }
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["longPressChapterSkip"] = s.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP;
  doc["displayDay"] = s.displayDay;
  doc["clockFormat"] = s.clockFormat;

  doc["libraryLayout"] = s.libraryLayout;
  doc["libraryFilter"] = s.libraryFilter;
  {
    const std::string rootDir(s.libraryRootDir);
    if (!rootDir.empty() && rootDir != "/") {
      doc["libraryRootDir"] = rootDir;
    }
  }
  doc["libraryLastCleanupDay"] = s.libraryLastCleanupDay;
  doc["librarySort"] = s.librarySort;
  doc["libraryUpdateMode"] = s.libraryUpdateMode;
  {
    const std::string searchText(s.librarySearchText);
    if (!searchText.empty()) {
      doc["librarySearchText"] = searchText;
    }
  }

  {
    const std::string ssDir(s.screenSaverDirectory);
    if (!ssDir.empty()) {
      doc["screenSaverDirectory"] = ssDir;
    }
  }
  doc["screenSaverOrder"] = s.screenSaverOrder;
  doc["screenSaverInterval"] = s.screenSaverInterval;
  doc["screenSaverWakeButton"] = s.screenSaverWakeButton;
  {
    const std::string ssText(s.screenSaverText);
    if (!ssText.empty()) {
      doc["screenSaverText"] = ssText;
    }
  }
  doc["screenSaverFontSize"] = s.screenSaverFontSize;
  doc["screenSaverTextPosition"] = s.screenSaverTextPosition;
  doc["screenSaverTextStyle"] = s.screenSaverTextStyle;
  doc["screenSaverShowPanel"] = s.screenSaverShowPanel;
  doc["screenSaverPanelColor"] = s.screenSaverPanelColor;
  doc["screenSaverPanelOpacity"] = s.screenSaverPanelOpacity;
  doc["screenSaverMinBattery"] = s.screenSaverMinBattery;
  doc["screenSaverReplaceSleep"] = s.screenSaverReplaceSleep;
  {
    const std::string ssReaderDir(s.screenSaverReaderDir);
    if (!ssReaderDir.empty()) {
      doc["screenSaverReaderDir"] = ssReaderDir;
    }
  }
  doc["screenSaverReaderOrder"] = s.screenSaverReaderOrder;

  doc["imageDitheringEnabled"] = s.imageDitheringEnabled;
  doc["imageLutEnabled"] = s.imageLutEnabled;
  doc["imageDitheringAlgorithm"] = s.imageDitheringAlgorithm;
  doc["imageThresholdBlack"] = s.imageThresholdBlack;
  doc["imageThresholdDark"] = s.imageThresholdDark;
  doc["imageThresholdLight"] = s.imageThresholdLight;
  doc["imageGamma"] = s.imageGamma;

  doc["statusBarTimeLeft"] = s.statusBarTimeLeft;

  doc["libraryShortcut"] = s.libraryShortcut;
  doc["libraryShortcutOrder"] = s.libraryShortcutOrder;
  doc["libraryShortcutVisible"] = s.libraryShortcutVisible;
  doc["screenSaverShortcut"] = s.screenSaverShortcut;
  doc["screenSaverShortcutOrder"] = s.screenSaverShortcutOrder;
  doc["screenSaverShortcutVisible"] = s.screenSaverShortcutVisible;
  doc["clippingsShortcut"] = s.clippingsShortcut;
  doc["clippingsShortcutOrder"] = s.clippingsShortcutOrder;
  doc["clippingsShortcutVisible"] = s.clippingsShortcutVisible;
  doc["wikipediaShortcut"] = s.wikipediaShortcut;
  doc["wikipediaShortcutOrder"] = s.wikipediaShortcutOrder;
  doc["wikipediaShortcutVisible"] = s.wikipediaShortcutVisible;
   doc["quickCardsShortcut"] = s.quickCardsShortcut;
   doc["quickCardsShortcutOrder"] = s.quickCardsShortcutOrder;
   doc["quickCardsShortcutVisible"] = s.quickCardsShortcutVisible;
   doc["pluginsShortcut"] = s.pluginsShortcut;
   doc["pluginsShortcutOrder"] = s.pluginsShortcutOrder;
   doc["pluginsShortcutVisible"] = s.pluginsShortcutVisible;

doc["readerMenuVisibilityMask"] = s.readerMenuVisibilityMask;
    JsonArray readerMenuOrder = doc["readerMenuOrder"].to<JsonArray>();
    for (size_t i = 0; i < 19; i++) {
      readerMenuOrder.add(s.readerMenuOrderMask[i]);
    }
}

void readSteroidsSettingsDoc(const JsonDocument& doc, CrossPointSettings& s, bool* needsResave) {
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };
  auto loadToggle = [&](const char* key, uint8_t& field) {
    field = clamp(doc[key] | field, static_cast<uint8_t>(2), field);
  };
  auto loadEnum = [&](const char* key, uint8_t& field, const uint8_t count) {
    field = clamp(doc[key] | field, count, field);
  };
  auto loadString = [&](const char* key, char* dest, const size_t maxLen) {
    const std::string value = doc[key] | std::string(dest);
    strncpy(dest, value.c_str(), maxLen - 1);
    dest[maxLen - 1] = '\0';
  };
  using S = CrossPointSettings;

  loadToggle("cycleScreensaverOnTap", s.cycleScreensaverOnTap);

  loadToggle("guideReadingEnabled", s.guideReadingEnabled);
  loadEnum("dotsSpacing", s.dotsSpacing, S::DOTS_SPACING_COUNT);
  loadEnum("epubRenderMode", s.epubRenderMode, S::EPUB_RENDER_MODE_COUNT);

  loadEnum("frontLongPressBehavior", s.frontLongPressBehavior, S::FRONT_LONG_PRESS_BEHAVIOR_COUNT);
  loadEnum("selectLongPress", s.selectLongPress, S::SELECT_LONG_PRESS_COUNT);

  {
    const uint8_t rawUiTheme = doc["uiTheme"] | s.uiTheme;
    const uint8_t uiThemeSchemaVersion = doc["uiThemeSchemaVersion"] | static_cast<uint8_t>(0);
    s.uiTheme = migrateStoredUiTheme(rawUiTheme, uiThemeSchemaVersion, s.uiTheme, needsResave);
  }
  {
    const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
    if (rawFontFamily >= static_cast<uint8_t>(S::FONT_FAMILY_COUNT)) {
      s.fontFamily = S::BOOKERLY;
      if (needsResave) *needsResave = true;
    } else {
      s.fontFamily = rawFontFamily;
    }
  }
   loadString("sdFontFamilyName", s.sdFontFamilyName, sizeof(s.sdFontFamilyName));
   // Legacy long-press behavior (for backward compat)
   if (!doc["longPressButtonBehavior"].isNull()) {
     loadEnum("longPressButtonBehavior", s.longPressButtonBehavior, S::LONG_PRESS_BUTTON_BEHAVIOR_COUNT);
   } else {
     s.longPressButtonBehavior = (doc["longPressChapterSkip"] | true) ? S::LONG_PRESS_CHAPTER_SKIP : S::LONG_PRESS_OFF;
   }
   // New per-directional long-press settings — load if present
   loadEnum("longPressUpBehavior", s.longPressUpBehavior, S::BTN_ACTION_COUNT);
   loadEnum("longPressDownBehavior", s.longPressDownBehavior, S::BTN_ACTION_COUNT);
   loadEnum("frontLongPressLeftBehavior", s.frontLongPressLeftBehavior, S::BTN_ACTION_COUNT);
   loadEnum("frontLongPressRightBehavior", s.frontLongPressRightBehavior, S::BTN_ACTION_COUNT);
   // Migrate legacy longPressButtonBehavior to per-directional if new fields at default
   if (s.longPressUpBehavior == S::BTN_ACTION_OFF && s.longPressDownBehavior == S::BTN_ACTION_OFF &&
       s.longPressButtonBehavior != S::LONG_PRESS_OFF) {
      const auto legacyAct = ReaderUtils::legacyLongPressToButtonAction(s.longPressButtonBehavior);
     s.longPressUpBehavior = legacyAct;
     s.longPressDownBehavior = legacyAct;
     if (needsResave) *needsResave = true;
   }
   loadEnum("frontLongPressBehavior", s.frontLongPressBehavior, S::FRONT_LONG_PRESS_BEHAVIOR_COUNT);
   // Migrate legacy frontLongPressBehavior to per-directional if new fields at default
   if (s.frontLongPressLeftBehavior == S::BTN_ACTION_OFF && s.frontLongPressRightBehavior == S::BTN_ACTION_OFF &&
       s.frontLongPressBehavior != S::FRONT_LONG_PRESS_OFF) {
      const auto legacyAct = ReaderUtils::legacyFrontLongPressToButtonAction(s.frontLongPressBehavior);
     s.frontLongPressLeftBehavior = legacyAct;
     s.frontLongPressRightBehavior = legacyAct;
     if (needsResave) *needsResave = true;
   }
   loadEnum("selectLongPress", s.selectLongPress, S::SELECT_LONG_PRESS_COUNT);
   loadEnum("selectLongPressBehavior", s.selectLongPressBehavior, S::BTN_ACTION_COUNT);
   // Migrate legacy selectLongPress to selectLongPressBehavior if at default
   if (s.selectLongPressBehavior == S::BTN_ACTION_TOGGLE_BOOKMARK &&
       s.selectLongPress != S::SELECT_LONG_PRESS_BOOKMARK) {
     if (s.selectLongPress == S::SELECT_LONG_PRESS_READING_TIME) {
       s.selectLongPressBehavior = S::BTN_ACTION_READING_TIME;
     } else if (s.selectLongPress == S::SELECT_LONG_PRESS_OFF) {
       s.selectLongPressBehavior = S::BTN_ACTION_OFF;
     }
     if (needsResave) *needsResave = true;
   }
   loadEnum("shortPwrBtn", s.shortPwrBtn, S::SHORT_PWRBTN_COUNT);
   s.displayDay = clamp(doc["displayDay"] | s.displayDay, S::DISPLAY_HEADER_MODE_COUNT, s.displayDay);
   s.clockFormat = clamp(doc["clockFormat"] | s.clockFormat, static_cast<uint8_t>(2), s.clockFormat);

  loadEnum("libraryLayout", s.libraryLayout, S::LIBRARY_LAYOUT_COUNT);
  loadEnum("libraryFilter", s.libraryFilter, S::LIBRARY_FILTER_COUNT);
  loadString("libraryRootDir", s.libraryRootDir, sizeof(s.libraryRootDir));
  s.libraryLastCleanupDay = doc["libraryLastCleanupDay"] | static_cast<uint8_t>(0);
  loadEnum("librarySort", s.librarySort, S::LIBRARY_SORT_COUNT);
  loadEnum("libraryUpdateMode", s.libraryUpdateMode, S::LIBRARY_UPDATE_MODE_COUNT);
  {
    const std::string searchText = doc["librarySearchText"] | std::string("");
    strncpy(s.librarySearchText, searchText.c_str(), sizeof(s.librarySearchText) - 1);
    s.librarySearchText[sizeof(s.librarySearchText) - 1] = '\0';
  }

  loadString("screenSaverDirectory", s.screenSaverDirectory, sizeof(s.screenSaverDirectory));
  loadEnum("screenSaverOrder", s.screenSaverOrder, S::SCREENSAVER_ORDER_COUNT);
  loadEnum("screenSaverInterval", s.screenSaverInterval, S::SCREENSAVER_INTERVAL_COUNT);
  loadEnum("screenSaverWakeButton", s.screenSaverWakeButton, S::SCREENSAVER_WAKE_BUTTON_COUNT);
  loadString("screenSaverReaderDir", s.screenSaverReaderDir, sizeof(s.screenSaverReaderDir));
  loadEnum("screenSaverReaderOrder", s.screenSaverReaderOrder, S::SCREENSAVER_ORDER_COUNT);
  loadString("screenSaverText", s.screenSaverText, sizeof(s.screenSaverText));
  loadEnum("screenSaverFontSize", s.screenSaverFontSize, S::SCREENSAVER_FONT_SIZE_COUNT);
  loadEnum("screenSaverTextPosition", s.screenSaverTextPosition, S::SCREENSAVER_TEXT_POSITION_COUNT);
  loadEnum("screenSaverTextStyle", s.screenSaverTextStyle, S::SCREENSAVER_TEXT_STYLE_COUNT);
  loadToggle("screenSaverShowPanel", s.screenSaverShowPanel);
  loadEnum("screenSaverPanelColor", s.screenSaverPanelColor, static_cast<uint8_t>(2));
  loadEnum("screenSaverPanelOpacity", s.screenSaverPanelOpacity, static_cast<uint8_t>(4));
  loadEnum("screenSaverMinBattery", s.screenSaverMinBattery, static_cast<uint8_t>(9));
  loadToggle("screenSaverReplaceSleep", s.screenSaverReplaceSleep);

  loadToggle("imageDitheringEnabled", s.imageDitheringEnabled);
  loadToggle("imageLutEnabled", s.imageLutEnabled);
  // imageDitheringAlgorithm: 0=Atkinson, 1=Floyd-Steinberg
  loadEnum("imageDitheringAlgorithm", s.imageDitheringAlgorithm, static_cast<uint8_t>(2));
  s.imageThresholdBlack  = clamp(doc["imageThresholdBlack"]  | s.imageThresholdBlack,  static_cast<uint8_t>(255), s.imageThresholdBlack);
  s.imageThresholdDark   = clamp(doc["imageThresholdDark"]   | s.imageThresholdDark,   static_cast<uint8_t>(255), s.imageThresholdDark);
  s.imageThresholdLight  = clamp(doc["imageThresholdLight"]  | s.imageThresholdLight,  static_cast<uint8_t>(255), s.imageThresholdLight);
  s.imageGamma           = clamp(doc["imageGamma"]           | s.imageGamma,           static_cast<uint8_t>(30),  s.imageGamma);

  loadEnum("statusBarTimeLeft", s.statusBarTimeLeft, S::STATUS_BAR_TIME_LEFT_COUNT);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.libraryShortcut = clamp(doc["libraryShortcut"] | s.libraryShortcut, shortcutLocationCount, s.libraryShortcut);
  s.libraryShortcutOrder = clamp(doc["libraryShortcutOrder"] | s.libraryShortcutOrder, shortcutOrderCount, s.libraryShortcutOrder);
  s.libraryShortcutVisible = clamp(doc["libraryShortcutVisible"] | s.libraryShortcutVisible, static_cast<uint8_t>(2), s.libraryShortcutVisible);
  s.screenSaverShortcut = clamp(doc["screenSaverShortcut"] | s.screenSaverShortcut, shortcutLocationCount, s.screenSaverShortcut);
  s.screenSaverShortcutOrder = clamp(doc["screenSaverShortcutOrder"] | s.screenSaverShortcutOrder, shortcutOrderCount, s.screenSaverShortcutOrder);
  s.screenSaverShortcutVisible = clamp(doc["screenSaverShortcutVisible"] | s.screenSaverShortcutVisible, static_cast<uint8_t>(2), s.screenSaverShortcutVisible);
  s.clippingsShortcut = clamp(doc["clippingsShortcut"] | s.clippingsShortcut, shortcutLocationCount, s.clippingsShortcut);
  s.clippingsShortcutOrder = clamp(doc["clippingsShortcutOrder"] | s.clippingsShortcutOrder, shortcutOrderCount, s.clippingsShortcutOrder);
  s.clippingsShortcutVisible = clamp(doc["clippingsShortcutVisible"] | s.clippingsShortcutVisible, static_cast<uint8_t>(2), s.clippingsShortcutVisible);
  s.wikipediaShortcut = clamp(doc["wikipediaShortcut"] | s.wikipediaShortcut, shortcutLocationCount, s.wikipediaShortcut);
  s.wikipediaShortcutOrder = clamp(doc["wikipediaShortcutOrder"] | s.wikipediaShortcutOrder, shortcutOrderCount, s.wikipediaShortcutOrder);
  s.wikipediaShortcutVisible = clamp(doc["wikipediaShortcutVisible"] | s.wikipediaShortcutVisible, static_cast<uint8_t>(2), s.wikipediaShortcutVisible);
  s.quickCardsShortcut = clamp(doc["quickCardsShortcut"] | s.quickCardsShortcut, shortcutLocationCount, s.quickCardsShortcut);
  s.quickCardsShortcutOrder = clamp(doc["quickCardsShortcutOrder"] | s.quickCardsShortcutOrder, shortcutOrderCount, s.quickCardsShortcutOrder);
   s.quickCardsShortcutVisible = clamp(doc["quickCardsShortcutVisible"] | s.quickCardsShortcutVisible, static_cast<uint8_t>(2), s.quickCardsShortcutVisible);
   s.pluginsShortcut = clamp(doc["pluginsShortcut"] | s.pluginsShortcut, shortcutLocationCount, s.pluginsShortcut);
   s.pluginsShortcutOrder = clamp(doc["pluginsShortcutOrder"] | s.pluginsShortcutOrder, shortcutOrderCount, s.pluginsShortcutOrder);
   s.pluginsShortcutVisible = clamp(doc["pluginsShortcutVisible"] | s.pluginsShortcutVisible, static_cast<uint8_t>(2), s.pluginsShortcutVisible);

s.readerMenuVisibilityMask = doc["readerMenuVisibilityMask"] | s.readerMenuVisibilityMask;
    {
      JsonArrayConst arr = doc["readerMenuOrder"];
      if (!arr.isNull()) {
        const size_t count = std::min<size_t>(arr.size(), 19);
        uint8_t defaults[19];
        for (size_t i = 0; i < 19; i++) {
          defaults[i] = s.readerMenuOrderMask[i];
        }
for (size_t i = 0; i < count; i++) {
            s.readerMenuOrderMask[i] = arr[i] | defaults[i];
        }
        if (needsResave) *needsResave = true;
    }
}
}
}  // namespace

namespace JsonSettingsIO {
bool saveSettingsSteroids(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;
  writeSteroidsSettingsDoc(doc, s);
  return saveJsonDocumentToFile("STZ", path, doc);
}

bool loadSettingsSteroids(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("STZ", "Steroids settings JSON parse error: %s", error.c_str());
    return false;
  }
  readSteroidsSettingsDoc(doc, s, needsResave);
  LOG_DBG("STZ", "Steroids settings loaded from file");
  return true;
}
}  // namespace JsonSettingsIO
