# CPR-vCodex Steroids — Apps System (Shortcuts, Registration, Icons)

> **SCOPE:** Complete technical reference for app registration, icon mapping, and shortcut persistence.

---

## 1. App Registration Pipeline

```
ShortcutRegistry (ShortcutDefinition)
        │            +-- UIIcon enum value (BaseTheme.h)
        │            +-- location/order/visible ptr (CrossPointSettings.h)
        ▼
Home / ShortcutOrderActivity
        │
        ▼
Theme::drawIcon / renderer.drawIcon(...)
        │
        ▼
Theme iconForName(UIIcon icon, [size])  →  const uint8_t* bitmap  (or nullptr)
        │
        ▼
renderer.drawIcon(bitmap, x, y, w, h)
```

---

## 2. 5 Mandatory Pieces for New App Visibility

### 2.1 Sprite Bitmap (2 files)
```cpp
// 32×32 → components/icons/<name>icon.h, symbol <Name>Icon[]
// 24×24 → components/icons/<name>icon24.h, symbol <Name>24Icon[]
#pragma once
#include <cstdint>
// size: 32x32
static const uint8_t MyAppIcon[] = { ... };
```

### 2.2 `UIIcon` Enum Value (`src/components/themes/BaseTheme.h`)
```cpp
enum class UIIcon {
    // ...
    MyApp,    // Add here
};
```

### 2.3 `ShortcutDefinition` (`src/util/ShortcutRegistry.h`)
```cpp
enum class ShortcutId { ..., MyApp };

ShortcutDefinition{
    ShortcutId::MyApp,
    StrId::STR_MYAPP,
    StrId::STR_MYAPP_APP_DESC,
    UIIcon::MyApp,
    &CrossPointSettings::myAppShortcut,       // location
    &CrossPointSettings::myAppShortcutOrder,  // order
    &CrossPointSettings::myAppShortcutVisible // visible
}
```

### 2.4 Theme Mapping (EVERY theme with private `iconForName`)
```cpp
// LyraTheme.cpp (24 + 32 blocks)
case UIIcon::MyApp: return MyAppIcon;       // 32px
case UIIcon::MyApp: return MyApp24Icon;     // 24px

// LyraCarouselTheme.cpp (24 + 32)
// LyraMarcoand75Theme.cpp (32)
// LyraCustomTheme.cpp (if present)
```

### 2.5 JSON Persistence (`src/JsonSettingsIOSteroids.cpp` — NOT `JsonSettingsIO.cpp`!)
```cpp
// CrossPointSettings.h fields:
uint8_t myAppShortcut = SHORTCUT_APPS;      // location (0=Home, 1=Apps)
uint8_t myAppShortcutOrder = 22;            // order (default: last)
uint8_t myAppShortcutVisible = 1;           // visible

// Load (in ALL load functions):
s.myAppShortcut       = clamp(doc["myAppShortcut"]       | s.myAppShortcut,       shortcutLocationCount, s.myAppShortcut);
s.myAppShortcutOrder  = clamp(doc["myAppShortcutOrder"]  | s.myAppShortcutOrder,  shortcutOrderCount,    s.myAppShortcutOrder);
s.myAppShortcutVisible= clamp(doc["myAppShortcutVisible"]| s.myAppShortcutVisible, static_cast<uint8_t>(2), s.myAppShortcutVisible);

// Save:
doc["myAppShortcut"]        = s.myAppShortcut;
doc["myAppShortcutOrder"]   = s.myAppShortcutOrder;
doc["myAppShortcutVisible"] = s.myAppShortcutVisible;
```

---

## 3. Current App Catalog

| App | Activity | ShortcutId | UIIcon | Settings Prefix |
|-----|----------|------------|--------|-----------------|
| Library | `LibraryActivity` | `Library` | `Library` | `libraryShortcut` |
| Wikipedia | `WikipediaActivity` | `Wikipedia` | `Wikipedia` | `wikipediaShortcut` |
| Quick Cards | `QuickCardsActivity` | `QuickCards` | `QuickCards` | `quickCardsShortcut` |
| Screensaver | `ScreenSaverActivity` | `ScreenSaver` | `ScreenSaver` | `screenSaverShortcut` |
| Sleep | `SleepAppActivity` | `Sleep` | `Sleep` | `sleepShortcut` |
| Screen Clean | `ScreenCleanActivity` | `ScreenClean` | `ScreenClean` | `screenCleanShortcut` |
| Reading Stats | `ReadingStatsActivity` | `ReadingStats` | `ReadingStats` | `readingStatsShortcut` |
| Reading Heatmap | `ReadingHeatmapActivity` | `ReadingHeatmap` | `ReadingHeatmap` | `readingHeatmapShortcut` |
| Reading Profile | `ReadingProfileActivity` | `ReadingProfile` | `ReadingProfile` | `readingProfileShortcut` |
| Achievements | `AchievementsActivity` | `Achievements` | `Achievements` | `achievementsShortcut` |
| Flashcards | `FlashcardsAppActivity` | `Flashcards` | `Flashcards` | `flashcardsShortcut` |
| Dictionary | `DictionaryActivity` | `Dictionary` | `Dictionary` | `dictionaryShortcut` |
| IfFound | `IfFoundActivity` | `IfFound` | `IfFound` | `ifFoundShortcut` |
| Bookmarks | `BookmarksAppActivity` | `Bookmarks` | `Bookmarks` | `bookmarksShortcut` |
| Clippings | `ClippingsAppActivity` | `Clippings` | `Clippings` | `clippingsShortcut` |
| Favorites | `FavoritesAppActivity` | `Favorites` | `Favorites` | `favoritesShortcut` |
| Sync Day | `SyncDayActivity` | `SyncDay` | `SyncDay` | `syncDayShortcut` |
| Reading Date Selection | `ReadingDateSelectionActivity` | `ReadingDate` | `ReadingDate` | `readingDateShortcut` |
| Apps Hub | `AppsActivity` | `Apps` | `Apps` | `appsShortcut` |
| Plugin Browser | `PluginBrowserActivity` | `Plugins` | `Plugins` | `pluginsShortcut` |

---

## 4. Theme Files

| File | Function | Sizes |
|------|----------|-------|
| `src/components/themes/lyra/LyraTheme.cpp` | `iconForName(UIIcon)`, 2 blocks | 24 + 32 |
| `src/components/themes/lyra/LyraCarouselTheme.cpp` | `iconForName(UIIcon, int size)` | 24 + 32 |
| `src/components/themes/lyra/LyraMarcoand75Theme.cpp` | `iconForName(UIIcon)` | 32 (maps all apps) |
| `src/components/themes/lyra/LyraCustomTheme.cpp` | (inherits) | — |

---

## 5. Verification Checklist

When adding an app or icon, check ALL:

- [ ] Sprite bitmap in `src/components/icons/`
- [ ] Value in `UIIcon` enum (`BaseTheme.h`)
- [ ] `ShortcutDefinition` in `ShortcutRegistry.h`
- [ ] 3 fields (`...Shortcut`, `...ShortcutOrder`, `...ShortcutVisible`) in `CrossPointSettings.h`
- [ ] `case` in `LyraTheme.cpp` (24 and 32 blocks)
- [ ] `case` in `LyraCarouselTheme.cpp` (24 and 32 blocks)
- [ ] `case` in `LyraMarcoand75Theme.cpp`
- [ ] `#include` of bitmap header in every modified theme
- [ ] 3 load lines in `JsonSettingsIOSteroids.cpp` (ALL load points)
- [ ] 3 save lines in `JsonSettingsIOSteroids.cpp`
- [ ] `python -X utf8 -m platformio run -e default -j 16` compiles
- [ ] Device test: open from Home and Apps in every theme, change order/visibility/location, reboot, verify persist

---

## 6. Key Files

| File | Role |
|------|------|
| `src/components/icons/*.h` | Icon bitmap data |
| `src/components/themes/BaseTheme.h` | `enum UIIcon` |
| `src/util/ShortcutRegistry.h` | `ShortcutDefinition` + helpers |
| `src/CrossPointSettings.h` | `...Shortcut*` fields |
| `src/JsonSettingsIOSteroids.cpp` | Steroids settings serialization |
| `src/JsonSettingsIO.cpp` | Upstream settings (byte-identical) |
| `src/components/themes/lyra/*.cpp` | Icon mapping per theme |

---

## 7. Related Documents

- **Library:** `STEROIDS-ADDICTIONS-LIBRARY.md`
- **Wikipedia:** `STEROIDS-ADDICTIONS-WIKIPEDIA.md`
- **Screensaver:** `STEROIDS-ADDICTIONS-SCREENSAVER.md`
- **Reading Stats:** `STEROIDS-ADDICTIONS-READING-STATS.md`
- **Dictionary:** `STEROIDS-ADDICTIONS-DICTIONARY.md`
- **Flashcards:** `STEROIDS-ADDICTIONS-FLASHCARDS.md`
- **Bookmarks/Clippings:** `STEROIDS-ADDICTIONS-BOOKMARKS-CLIPPINGS.md`
- **Quick Cards:** `STEROIDS-ADDICTIONS-QUICK-CARDS.md`
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` §3.5
- **Lua Plugins:** `STEROIDS-ADDICTIONS-LUA.md`

---

*Last updated: 2026-09-14*