# CPR-vCodex Steroids — Settings Extensions

> **SCOPE:** Additional settings categories, web API optimization, steroids-specific settings page. Low merge risk.

---

## 1. User-Facing Functionality

| Category | Settings |
|----------|----------|
| **Library** | Layout, filter, sort, view mode, update mode, root directory |
| **Sleep Screen** | Mode, cover mode, cover filter, clean refresh |
| **Power Button** | Short press action, long press action |
| **Screensaver** | Enabled, timeout, type, custom path |
| **Reading Stats** | Daily goal, import/export, manual correction |
| **Steroids Web UI** | `/settings` page with all above |

---

## 2. Technical Architecture

### 2.1 Settings Fields (`CrossPointSettings.h`)
```cpp
// Library
uint8_t libraryLayout;           // 0=flat, 1=collections, 2=mixed
uint8_t libraryFilter;           // ALL, FAVORITES, UNREAD, COMPLETED, HIDDEN
uint8_t librarySort;             // TITLE_ASC, TITLE_DESC, AUTHOR_ASC, AUTHOR_DESC, RECENT, PROGRESS
uint8_t libraryViewMode;         // 0=flat, 1=mixed, 2=collections
uint8_t libraryUpdateMode;       // 0=manual, 1=auto, 2=background
std::string libraryRootDir;      // Custom root directory

// Sleep Screen
uint8_t sleepScreen;             // SleepScreenMode enum
uint8_t sleepScreenCoverMode;    // CoverMode enum
uint8_t sleepScreenCoverFilter;  // CoverFilter enum
bool cleanSleepRefresh;

// Power Button
uint8_t powerButtonShortPress;   // PowerButtonShortPress enum
uint8_t powerButtonLongPress;    // PowerButtonLongPress enum

// Screensaver
bool screensaverEnabled;
uint32_t screensaverTimeout;
uint8_t screensaverType;
std::string screensaverCustomPath;

// Steroids Branding
bool steroidsBrandingEnabled;    // Always true in this fork
```

### 2.2 Web API Optimization (`CrossPointWebServer.cpp`)

#### Before: 50s Response
```cpp
// Individual I18N resolution per string
JsonDocument doc;
for (const auto& s : allSettings) {
    doc[s.key] = tr(s.i18nKey);  // 1000+ tr() calls
}
serializeJson(doc, response);
```

#### After: <1s Response
```cpp
// Batched JSON + pre-resolved I18N
void handleSteroidsSettings() {
    JsonDocument doc;
    // Single pass: resolve all I18N strings
    preResolveI18nStrings(i18nCache);
    // Build JSON with cached strings
    buildSettingsJson(doc, i18nCache);
    serializeJson(doc, response);  // One write
}
```

### 2.3 Settings Persistence
```cpp
// Saved on SettingsActivity::onExit()
SETTINGS.saveToFile();  // Writes settings-steroids.json

// Format:
{
    "formatVersion": 3,
    "language": "IT",           // ISO code (not language.bin)
    "libraryLayout": 2,
    "libraryFilter": 0,
    "librarySort": 1,
    "sleepScreen": 3,
    "powerButtonShortPress": 1,
    "screensaverEnabled": true,
    ...
}
```

### 2.4 Language Persistence
```cpp
// i18n.language.persistence decision:
// - Language saved as ISO code: "language": "IT"
// - Old language.bin on SD ignored at boot
// - Default English if not configured
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/CrossPointSettings.h/cpp` | Settings struct, persistence, defaults |
| `src/activities/settings/SettingsActivity.cpp` | Settings UI, steroids sections |
| `src/network/CrossPointWebServer.cpp` | Web API endpoints, batched JSON |
| `docs/SteroidsSettingsPage.html` | Web settings UI |
| `src/main.cpp` | Language persistence logic |

---

## 4. Upstream Merge Notes

### LOW RISK
- `CrossPointSettings` — New fields (backward compatible)
- `SettingsActivity` — New sections (self-contained)
- Web API batching pattern (performance, no behavior change)

### Conflicts Unlikely
- Settings struct additions (default values handle missing)
- Web API is steroids-specific endpoint

### Safe Cherry-Picks
- Settings API batching (<1s vs 50s)
- Language ISO code persistence
- Library root directory setting

---

## 5. Validation Checklist

- [ ] Library settings: layout/filter/sort/view/update/root all work
- [ ] Sleep screen settings: mode/cover/filter/refresh all work
- [ ] Power button settings: short/long press actions work
- [ ] Screensaver settings: enabled/timeout/type/path work
- [ ] Reading stats settings: goal/import/export/correction work
- [ ] Web `/settings` loads <1s
- [ ] Web `/settings` POST saves correctly
- [ ] Language persists as ISO code, survives reboot
- [ ] Settings survive OTA update

---

## 6. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-BRANDING.md` — Web UI pages
- `STEROIDS-ADDICTIONS-SLEEP.md` — Sleep screen settings
- `STEROIDS-ADDICTIONS-SCREENSAVER.md` — Screensaver settings
- `STEROIDS-ADDICTIONS-LIBRARY.md` — Library settings

---

*Last updated: 2026-09-28 | Commit: 25349a04 (steroids settings page)*