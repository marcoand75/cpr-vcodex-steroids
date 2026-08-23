# CPR-vCodex Steroids — Guide to Aligning with Upstream

This document details the exact workflow to merge a new upstream CPR-vCodex release
into CPR-vCodex Steroids while preserving all Steroids-specific features, branding,
and OTA configuration. It is based on the 1.3.0 → 1.4.5 merge performed on
2026-07-26.

---

## Quick Reference: Files You MUST NOT Overwrite

These files contain Steroids-only features. **Never `git checkout --theirs`**

| File | Steroids Feature |
|---|---|
| `src/activities/reader/BookmarkStore.h` | Layout-independent absolute word index bookmarks |
| `src/activities/reader/ClippingStore.h` | Text highlight/clipping data model |
| `src/activities/reader/ClippingsActivity.cpp/h` | In-reader clipping UI |
| `src/activities/apps/ClippingsAppActivity.cpp/h` | Clippings browser app |
| `src/activities/apps/BookmarksAppActivity.cpp/h` | Bookmarks browser app |
| `src/activities/apps/LibraryActivity.cpp/h` | Full e-book library browser |
| `src/activities/apps/ScreenSaverActivity.cpp/h` | Screensaver app |
| `src/activities/apps/ScreenSaverDirActivity.cpp/h` | Screensaver directory selector |
| `src/activities/apps/ScreenSaverPreviewActivity.cpp/h` | Screensaver preview |
| `src/components/LibraryCache.cpp/h` | Library thumbnail cache |
| `src/HiddenBooksStore.h` | Hidden books singleton (bookId + path, JSON persist) |
| `src/HiddenBooksStore.cpp` | Load/save/toggle/remove with atomic JSON write |
| `src/components/EpubParser.cpp/h` | EPUB metadata parser (used by library) |
| `src/components/themes/lyra/LyraMarcoand75Theme.cpp/h` | Custom Steroids theme |
| `src/components/LibraryPopupOverlay.h` | Library popup overlay |
| `src/components/PanelDrawHelper.h` | Panel drawing helper |
| `src/images/Logo-steroids*.png` | Steroids logo images |
| `src/network/html/LogoPng.generated.h` | Generated logo PNG for web server |
| `src/network/html/AppSettingsPage.html` | Browser stats/settings editor |
| `src/util/CoverRibbonBaker.cpp/h` | Cover ribbon baker |
| `src/util/BookStoreUtils.h` | Book store utilities |
| `lib/GfxRenderer/DitheringConfig.h` | Steroids-only shared grayscale config (dither method, gray levels, gamma, `gammaLUT`) |
| `lib/GfxRenderer/ImageRenderConfig.h` | Runtime image rendering parameters (globals + failover defaults) |
| `lib/GfxRenderer/ImageRenderConfig.cpp` | init + applySettings from CrossPointSettings |
| `lib/GfxRenderer/BitmapHelpers.h` | Ditherers reworked with `int16_t`, pre-clamp error, safe +2 buffer indexing |
| `lib/GfxRenderer/BitmapHelpers.cpp` | `quantizeSimple` (50/120/200), `unquantize`, `adjustPixel` gamma-LUT |
| `lib/GfxRenderer/Bitmap.cpp` | Uses shared `DitheringConfig.h` (removed local `USE_ATKINSON`) |
| `lib/JpegToBmpConverter/JpegToBmpConverter.cpp` | Uses shared `DitheringConfig.h` (removed local dither consts) |
| `lib/PngToBmpConverter/PngToBmpConverter.cpp` | Uses shared `DitheringConfig.h` (removed local dither consts) |
| `src/util/ListInputMapper.h` | List input mapper (may need upstream refactoring) |
| `src/util/ListLayout.h` | List layout calculator |
| `src/util/ListRenderHelper.h` | List render helper |
| `src/icons/*` (various .h files) | Steroids custom icons |
| `agent-docs/*` | Steroids documentation |
| `README.md` (sections marked "Steroids") | Steroids feature documentation |
| **`src/JsonSettingsIO.cpp`** | **ALL Steroids settings serialization (shortcuts, library, screensaver, clippings, longPress, etc.)** |
| **`src/JsonSettingsIO.h`** | **Steroids-specific function declarations** |
| **`src/network/CrossPointWebServer.cpp`** | **App Settings page route, logo endpoint, Steroids routes** |
| **`src/network/CrossPointWebServer.h`** | **Steroids-specific handler declarations** |
| **`src/network/html/AppSettingsPage.html`** | **Browser stats/settings editor (deleted by upstream!)** |
| **`src/SettingsList.cpp`** | **Steroids menu items (library, screensaver, frontLongPress, clippingsShortcut, etc.)** |
| **`src/activities/ActivityManager.cpp/h`** | **goToLibrary, goToScreensaver, goToClippings methods** |
| **`src/ReadingStatsStore.h`** | **Steroids pace-tracking fields (avgSecondsPerForwardPage, paceSampleCount) + Home summary.json fast path (SummaryJSON, getGlobalSummary/getBookProgressForHome/getBookHomeStats/getHomeBookStatsForRender/preloadHomeSummary)** |
| **`src/ReadingStatsStore.cpp`** | **Steroids pace-tracking implementation (recordForwardPageRead, mark-as-unread) + summary.json save/load + summary-aware getters (Home renders without the ~41 KB store at boot)** |
| **`src/ReadingStatsActivity.cpp/h`** | **selectedBookPath constructor param (pre-select book in stats)** |
| **`src/components/LibraryIndex.cpp`** | **Incremental scan vector pre-allocation, null-terminated ZIP reads** |
| **`src/activities/settings/StatusBarSettingsActivity.cpp`** | **Clock position, clock format, sync clock now in status bar menu** |
| **`src/util/TimeUtils.cpp`** | **applySystemClockFromRtc: no clockHasBeenSynced guard, DS3231 time used immediately** |

### Why ReadingStatsStore is critical

Upstream 1.5.0 removed `avgSecondsPerForwardPage`, `paceSampleCount`, and
`recordForwardPageRead()` from ReadingStatsStore. Steroids preserves these
for reading pace estimation and "time left" status bar. If upstream
ReadingStatsStore.h is taken, `JsonSettingsIO.cpp` fails to compile because
it references these fields in import/export functions.

**2026-08-04 full alignment audit:** Steroids ReadingStatsStore was compared
line-by-line against upstream 1.5.0.5. The following upstream features were
found missing in Steroids and have been restored:

- **`loadReadingStatsDocument` function:** upstream splits JSON parsing
  (`loadReadingStats`) from document validation/deserialization
  (`loadReadingStatsDocument`). Steroids had inlined the deserialization body
  into `loadReadingStats`, which worked but broke the friend-declaration
  pattern. Restored with the upstream split.
- **Aggregate reconciliation block:** upstream's `loadReadingStatsDocument`
  contains a `formatVersion >= 6` block that detects and reconciles
  mismatches between declared reading-day aggregates and rebuilt totals,
  forwarding surplus to `legacyReadingDays`. Steroids had removed this
  entirely. Restored for data integrity.
- **`std::stable_sort` of `sessionLog`:** upstream sorts session log entries
  by `dayOrdinal` after loading. Steroids had removed this. Restored.
- **`importFromFile` rollback path:** upstream creates a pre-import backup
  via `refreshInternalBackupFromMain()`, rejects empty imports, and rolls
  back on save failure via `reloadOriginalStats()`. Steroids had none of
  these, so a corrupted import could destroy live stats. Restored.
- **`loadFromFile` persistence suspension:** upstream calls
  `markLoadSkippedForRecovery()` on load failure to suspend saves and prevent
  overwriting the backup. Steroids had removed this call. Restored.
- **`normalizeBook` `clampPercent` calls:** upstream clamps
  `lastProgressPercent` and `chapterProgressPercent` in `normalizeBook`.
  Steroids had removed these. Restored.
- **`loadReadingStatsFromFile` uses `loadJsonDocumentFromFile`:** upstream
  loads directly into a `JsonDocument` instead of reading the file as a
  string and then parsing it (which doubled memory usage). Steroids was
  using `Storage.readFile()` + `loadReadingStats()`. Aligned to upstream.
- **`maybeCreateAutoBackup` pre-removal:** Steroids was removing an existing
  backup file before saving a new one, which could cause data loss if the
  new save failed. Aligned to upstream behavior (no pre-removal;
  `saveJsonDocumentToFile` handles its own temp-then-rename).

**Steroids-only features preserved in all changes:**
- `avgSecondsPerForwardPage` / `paceSampleCount` serialization (save + load)
- `recordForwardPageRead()` weighted average implementation
- `mergeBookInto()` pace-data merge
- `beginSession()` / `updateProgress()` mark-as-unread logic
- `.reserve()` fragmentation fixes with `HCR-FRAG` diagnostics
- `knownPaths.reserve()` in per-book loop
- `loadedBookCount` counter with periodic heap logging
- Meyers' Singleton pattern (inline `getInstance()`)
- `#include <ArduinoJson.h>` in `ReadingStatsStore.h` (upstream already had it)
- `SummaryJSON` + `/.crosspoint/summary.json` fast path — the Home renders the
  global panel and carousel badges from the small derived summary instead of
  loading the full store at boot. If an upstream merge rewrites
  `ReadingStatsStore.{h,cpp}`, re-apply: `SummaryJSON` struct, `summaryJson`
  cache + `summaryJsonValid_`, `saveSummaryJSON`/`loadSummaryJSON`/
  `getSummaryJSON`, the `loaded_` guards on all summary getters, `beginSession()`
  `ensureLoaded()` guard, and `preloadHomeSummary()` in `BootActivity`.

**Files modified in this alignment:**
- `src/ReadingStatsStore.h`: added `#include <ArduinoJson.h>`, friend
  declaration for `loadReadingStatsDocument`
- `src/ReadingStatsStore.cpp`: restored `importFromFile` rollback, restored
  `markLoadSkippedForRecovery()` in `loadFromFile`, restored `clampPercent`
  in `normalizeBook`, aligned `maybeCreateAutoBackup`
- `src/JsonSettingsIO.h`: added `#include <ArduinoJson.h>`, added
  `loadReadingStatsDocument` declaration
- `src/JsonSettingsIO.cpp`: extracted `loadReadingStatsDocument` from
  `loadReadingStats`, added upstream reconciliation block, added
  `std::stable_sort`, aligned `loadReadingStatsFromFile` to use
  `loadJsonDocumentFromFile`

### 2026-08-04: Steroids Settings JSON Split (v2 — complete separation)

**Key architectural change:** Steroids settings are now stored in a separate JSON file
(`/.crosspoint/settings-steroids.json`) instead of being mixed into
`/.crosspoint/settings.json`. All steroids I/O code is also extracted to
`JsonSettingsIOSteroids.cpp` so that `JsonSettingsIO.cpp` stays **byte-identical
to upstream** — zero merge conflicts in the entire file.

**How it works:**
- `settings.json` (~122 fields): upstream-only, byte-identical to upstream
- `settings-steroids.json` (~37 fields): Steroids-only with `formatVersion: 1`
- `CrossPointSettings::saveToFile()` saves to BOTH files transparently
- `CrossPointSettings::loadFromFile()` loads upstream first, then Steroids
- One-shot migration: on first boot after upgrade, if `settings-steroids.json` doesn't
  exist, Steroids fields are extracted from the old unified `settings.json`, saved to
  the new file, and `settings.json` is re-saved without them.

**Fields moved from upstream to Steroids file (2026-08-04 v2):**
These fields exist in upstream but have been moved to `settings-steroids.json`
because Steroids uses different enum values, counts, or defaults:

| Field | Reason for move |
|---|---|
| `longPressButtonBehavior` | Steroids adds BOOKMARK=1, CLIPPING=2, FONTSIZE=5 (6 values vs upstream's 3) |
| `clockFormat` | Steroids inverts meaning: 0=24h/1=12h (upstream had 0=12h/1=24h → now aligned) |
| `fontFamily` | Steroids adds LEXEND=2 (upstream has 2 values, Steroids has 3) |
| `uiTheme` | Steroids adds LYRA_MARCOAND75=3 (upstream has 3 values, Steroids has 4) |
| `displayDay` | Steroids changes default from 1 (DATE_ONLY) to 2 (TIME_ONLY) |

**All Steroids-only fields in `settings-steroids.json`:**
- Display/Theme: `uiTheme`, `darkMode`, `antiGhostingExperimental`, `displayDay`, `clockFormat`
- Font/Rendering: `fontFamily`, `guideReadingEnabled`, `dotsSpacing`, `epubRenderMode`
- Controls: `longPressButtonBehavior`, `frontLongPressBehavior`, `cycleScreensaverOnTap`
- Status bar: `statusBarTimeLeft`
- Library: `libraryLayout`, `libraryFilter`, `librarySort`, `librarySearchText`,
  `libraryRootDir`, `libraryUpdateMode`, `libraryLastCleanupDay`
- Screensaver: `screenSaverDirectory`, `screenSaverOrder`, `screenSaverInterval`,
  `screenSaverWakeButton`, `screenSaverReaderDir`, `screenSaverReaderOrder`,
  `screenSaverText`, `screenSaverFontSize`, `screenSaverTextPosition`,
  `screenSaverTextStyle`, `screenSaverShowPanel`, `screenSaverPanelColor`,
  `screenSaverPanelOpacity`, `screenSaverMinBattery`, `screenSaverReplaceSleep`
- Shortcuts: `libraryShortcut*`, `screenSaverShortcut*`, `clippingsShortcut*`,
  `wikipediaShortcut*`

**Web interface:**
A new `/steroids-settings` page is available in the web UI, accessible from the
nav bar on all pages. It uses dedicated `/api/steroids-settings` GET/POST endpoints
that read/write `settings-steroids.json` directly.

**File architecture:**
- `src/JsonSettingsIO.h` / `.cpp`: upstream-only, byte-identical to upstream
- `src/JsonSettingsIOSteroids.h` / `.cpp`: steroids-only serialization
- `src/JsonSettingsIOShared.inc`: shared internal helpers (saveJsonDocumentToFile, etc.)
- `src/CrossPointSettings.cpp`: unified facade (saveToFile/loadFromFile)
- `src/network/CrossPointWebServer.cpp`: steroids settings API endpoints
- `src/network/html/SteroidsSettingsPage.html`: steroids settings web page
- `src/network/html/SteroidsSettingsPageHtml.generated.h`: gzipped HTML header

**Upstream merge workflow (updated):**
1. `JsonSettingsIO.cpp`: **zero conflicts** — byte-identical to upstream
2. `CrossPointSettings.h`: only needs manual merge of the POD struct (add new fields)
3. `JsonSettingsIOSteroids.cpp`: never touched by upstream — no conflicts
4. All other files: same as before

**Migration safety:** If the Steroids file save fails during migration, the old
`settings.json` (still containing Steroids fields) is preserved and migration
retries on next boot. If `settings-steroids.json` is corrupted or deleted,
Steroids settings revert to struct defaults — upstream settings are unaffected.

### Why JsonSettingsIO.cpp is NO LONGER critical (v2 split)

As of the 2026-08-04 settings split, `JsonSettingsIO.cpp` is **byte-identical to
upstream**. All 37 Steroids-only fields are now serialized in
`src/JsonSettingsIOSteroids.cpp` which upstream never touches. This means:

- **Zero merge conflicts** in `JsonSettingsIO.cpp` on any future upstream release
- The only conflict zone is `CrossPointSettings.h` (the POD struct — ~20 lines)
- `JsonSettingsIOShared.inc` contains shared internal helpers used by both files

If you accidentally take upstream's `JsonSettingsIO.cpp`, **no data is lost**
because steroids fields are in their own file. The result would be a build
failure from the missing steroids file includes, which is immediately obvious.

### Why CrossPointWebServer.cpp is important (but simpler to merge)

The local Steroids `CrossPointWebServer.cpp` adds:
- `/steroids-settings` route and handler (16 lines registration + ~400 lines handlers)
- `/api/steroids-settings` GET/POST endpoints
- `SteroidsSettingsPageHtml.generated.h` include
- Some steroids-only settings removed from `WEB_SETTINGS[]` (clean separation)

The actual merge is simpler now because steroids fields are no longer in
`WEB_SETTINGS[]`. The upstream `WEB_SETTINGS[]` contains only upstream fields.
If upstream adds new WEB_SETTINGS entries, they merge cleanly.

### HTML nav links: Steroids must be added back

Upstream does NOT have a "Steroids" nav link. After any merge where you take
upstream HTML, you MUST add `<a href="/steroids-settings">Steroids</a>`
to all 6 HTML pages in the nav-links section (Home, Files, Settings, App
Settings, Fonts, IfFound).

### Pre-migration backup location

The one-shot settings migration creates a backup of the original unified
`settings.json` at:
```
/.crosspoint/settings-steroids.json.bak
```
This file is created **once** during the first boot after upgrade. It is
never overwritten. Keep it on the SD card for manual rollback if the
migration had issues. It is safe to delete after confirming everything works.

### Silent restart mechanism

When returning to Home from Library or Wikipedia, the system uses
`silentRestartToHome()` — a `ESP.restart()` variant that skips the
"Loading..." popup and the panel white flash. The boot sequence skips
KOReader, Flashcard, OPDS loads and ReadingStats backup (~1088ms saved).
This provides a clean heap on return to Home with `maxAlloc` rising from
~70 KB to ~105 KB.

The `SilentRestart.h` API:
```cpp
void silentRestart();          // Home, with "Loading..." popup (WiFi exit)
void silentRestartToReader();  // Currently-open EPUB (WiFi exit)
void silentRestartToHome();    // Home, seamless — no popup (Library/Wikipedia exit)
```

---

## Files to Cherry-Pick (NEW upstream files only)

These files don't exist in the local Steroids codebase. Cherry-pick them directly:

```powershell
git checkout upstream/master -- lib/hal/HalClock.cpp lib/hal/HalClock.h
git checkout upstream/master -- src/activities/settings/ClockSyncActivity.cpp src/activities/settings/ClockSyncActivity.h
git checkout upstream/master -- src/activities/settings/KOReaderProfileEditActivity.cpp src/activities/settings/KOReaderProfileEditActivity.h
git checkout upstream/master -- src/activities/settings/KOReaderProfileListActivity.cpp src/activities/settings/KOReaderProfileListActivity.h
git checkout upstream/master -- lib/Utf8/Utf8ComposeTable.h
git checkout upstream/master -- test/CMakeLists.txt test/epubs/test_br_section_break.epub test/utf8_compose/
git checkout upstream/master -- src/util/HeaderDateUtils.cpp src/util/HeaderDateUtils.h
git checkout upstream/master -- src/activities/settings/TimeZoneSelectActivity.cpp src/activities/settings/TimeZoneSelectActivity.h
```

Check what other NEW files upstream added with:
```powershell
git diff --name-only HEAD upstream/master --diff-filter=A
```

---

## Files Requiring Strategic Merge (keep local, add upstream API)

These are files that upstream modified AND have local Steroids changes.
The **correct approach** is to keep the LOCAL version, then **manually add**
only the specific upstream features (not the whole file).

### 1. `src/CrossPointSettings.h` — Add clock/display enums AND long-press enums

The upstream adds `STATUS_BAR_CLOCK` and `DISPLAY_HEADER` enums, plus member
fields. Keep the entire local file, and manually add:

```cpp
// After STATUS_BAR_TIME_LEFT enum (~line 70):
enum STATUS_BAR_CLOCK {
  STATUS_BAR_CLOCK_HIDE = 0,
  STATUS_BAR_CLOCK_RIGHT = 1,
  STATUS_BAR_CLOCK_LEFT = 2,
  STATUS_BAR_CLOCK_COUNT
};

// After DATE_FORMAT enum (~line 213):
enum DISPLAY_HEADER {
  DISPLAY_HEADER_OFF = 0,
  DISPLAY_HEADER_DATE_ONLY = 1,
  DISPLAY_HEADER_TIME_ONLY = 2,
  DISPLAY_HEADER_BOTH = 3,
  DISPLAY_HEADER_MODE_COUNT = 4,
};

// After statusBarTimeLeft member (~line 343):
uint8_t statusBarClock = STATUS_BAR_CLOCK_HIDE;
uint8_t clockFormat = 0;   // 0=12h, 1=24h
uint8_t clockHasBeenSynced = 0;

// After displayDay member (~line 433):
uint8_t displayDay = DISPLAY_HEADER_TIME_ONLY;
uint8_t clockSyncSkipNext = 0;

// At the bottom of the struct, add:
void normalizeDisplayDay() {
  if (displayDay >= DISPLAY_HEADER_MODE_COUNT) {
    displayDay = DISPLAY_HEADER_TIME_ONLY;
  }
}
bool isHardwareRtcAutoDayClockActive() const { return true; }
```

**Steroids also expands the long-press behavior enums beyond upstream:**

- `LONG_PRESS_BUTTON_BEHAVIOR` (side buttons) adds `LONG_PRESS_BOOKMARK = 3`, `LONG_PRESS_CLIPPING = 4`, `LONG_PRESS_FONTSIZE = 5` — upstream only has OFF, CHAPTER_SKIP, ORIENTATION_CHANGE (0-2).
- `FRONT_LONG_PRESS_BEHAVIOR` (front buttons) is entirely Steroids-specific. Upstream has NO separate front button long-press setting. The values are: `FRONT_LONG_PRESS_OFF = 0`, `FRONT_LONG_PRESS_BOOKMARK = 1`, `FRONT_LONG_PRESS_CLIPPING = 2`, `FRONT_LONG_PRESS_CHAPTER_SKIP = 3`, `FRONT_LONG_PRESS_ORIENTATION = 4`, `FRONT_LONG_PRESS_FONTSIZE = 5`.
- Both enums use the **same option order**: OFF, BOOKMARK, CLIPPING, CHAPTER_SKIP, ORIENTATION, FONTSIZE.
- If upstream modifies these enums, NEVER take their version — always keep the local expanded enums.

### 2. `src/main.cpp` — Add HalClock init

```cpp
// Add include:
#include <HalClock.h>

// In setup(), after powerManager.begin():
halClock.begin();

// Update log message:
LOG_DBG("MAIN", "Starting CPR-vCodex Steroids version %s", CROSSPOINT_VERSION);
```

### 3. `src/JsonSettingsIO.cpp` — Add clock serialization

Add these lines to the `loadSettingsDirect` function (near the `statusBarTimeLeft` line):
```cpp
loadEnum("statusBarClock", s.statusBarClock, CrossPointSettings::STATUS_BAR_CLOCK_COUNT);
loadEnum("clockFormat", s.clockFormat, static_cast<uint8_t>(2));
loadToggle("clockHasBeenSynced", s.clockHasBeenSynced);
// After displayDay:
s.displayDay = clamp(doc["displayDay"] | s.displayDay, S::DISPLAY_HEADER_MODE_COUNT, s.displayDay);
```

And to the save function:
```cpp
doc["statusBarClock"] = s.statusBarClock;
doc["clockFormat"] = s.clockFormat;
doc["clockHasBeenSynced"] = s.clockHasBeenSynced;
```

### 4. Upstream files to take AS-IS (they add APIs needed by new features)

These files should be taken from upstream because the new features (ClockSync, KOReader profiles) depend on their updated APIs. However, verify after taking them that Steroids-specific serialization still works:

**After taking TimeUtils.cpp from upstream, you MUST re-apply the Steroids patch:**
Remove the `!SETTINGS.clockHasBeenSynced` guard from `applySystemClockFromRtc()`.
On X3 with a DS3231 RTC, this guard prevents the RTC time from being copied to
the ESP32 system clock on first access if NTP was never synced, making the top
header date invisible. The DS3231 time is independently validated by isClockValid()
(epoch >= 2024-01-01), so stale RTC values are still safely rejected.

Search for this pattern in the upstream file and remove the `!SETTINGS.clockHasBeenSynced` part:
```
  if (!halClock.isAvailable() || !SETTINGS.clockHasBeenSynced) {
```
Change to:
```
  if (!halClock.isAvailable()) {
```

```powershell
git checkout upstream/master -- src/util/TimeUtils.cpp src/util/TimeUtils.h
git checkout upstream/master -- lib/KOReaderSync/KOReaderCredentialStore.cpp lib/KOReaderSync/KOReaderCredentialStore.h
git checkout upstream/master -- src/activities/network/WifiSelectionActivity.cpp src/activities/network/WifiSelectionActivity.h
git checkout upstream/master -- src/network/CrossPointWebServer.cpp src/network/CrossPointWebServer.h
git checkout upstream/master -- src/network/html/HomePage.html src/network/html/SettingsPage.html
git checkout upstream/master -- src/network/html/FilesPage.html src/network/html/FontsPage.html src/network/html/IfFoundPage.html
```

**After taking these**, verify and re-add Steroids-specific serialization to `JsonSettingsIO.cpp`:
- libraryLayout, libraryFilter, librarySort, libraryRootDir, libraryLastCleanupDay, librarySearchText
- screenSaver* fields (16 fields)
- clippingsShortcut*, libraryShortcut*, screenSaverShortcut* (9 fields)
- cycleScreensaverOnTap, guideReadingEnabled, dotsSpacing, epubRenderMode
- frontLongPressBehavior, uiTheme

---

## Files to NEVER Merge as Upstream

### New Additions (2026-08-10)

These files were added in the 2026-08-10 Steroids development round and must never be overwritten:

| File | Steroids Feature |
|---|---|
| `src/activities/apps/QuickCardsActivity.cpp/h` | Quick Cards image/QR/barcode viewer app |
| `src/util/QrCardParser.h` | Structured QR field extraction (10 formats) |
| `src/components/icons/quickcards.h` / `quickcards24.h` | Quick Cards app icon bitmaps |
| `lib/hal/HalSpiBus.h/cpp` | SPI recursive mutex |
| `lib/hal/XteinkDetectExt.h/cpp` | UC8279/UC8179 panel controller detection |
| `freeink-sdk/` | Replaces `open-x4-sdk` — multi-device SDK |
| `platformio.ini` | Added `BoardConfig`, `XteinkDetect`, `-DFREEINK_DEVICE_X4=1 -DFREEINK_DEVICE_X3=1` |

### New Additions (2026-08-09 → 2026-08-18, base `07126f2b` → HEAD)

Added/restructured between `07126f2b` and `4eaf2371`. Some of these are brand-new
upstream files (never overwrite with an old upstream copy); others replace an upstream
implementation Steroids no longer ships.

| File | Steroids state / note |
|---|---|
| `lib/Epub/epub/*` (Section, Page, ParsedText, blocks, parsers, css) | **CrossInk EPUB engine**; massive divergence from upstream. Merge manually only. |
| `lib/MiniBidi/*` | Bidi engine pulled in by CrossInk; not in upstream master. |
| `lib/miniz/` + `third_party/miniz.c` | New inflater used by CrossInk. |
| `lib/Memory/Arena.h` / `ArenaVector.h` | Arena allocator used **inside** the CrossInk engine, **not** on the epub render hot path. |
| `src/activities/apps/WikipediaActivity.cpp/h` | Per-article folder cache (`wiki_<hash>`), crash fixes, i18n delete key, `listFiles(includeDirectories)`. |
| `src/activities/reader/WikiTxtReaderActivity.cpp/h` | Dedicated wiki reader (progress.bin, screenshot info, frame reserve). |
| `src/util/ScreenshotInfo.h` | New reader-metadata hook (used by wiki reader). |
| `src/ReadingStats/` | **Intentionally empty** — CrossInk binary stats removed; vCodex JSON is the only store. |
| `lib/EpdFont/SdCardFont.cpp` | **Chunked 4 KiB bitmap storage** (1.5.0.20 port: `miniBitmapChunks[24]`, TextGetter, `miniGlyphBitmap`, `onCoverageQuery`). Advance table still grows in place via `realloc`. |
| `lib/EpdFont/builtinFonts/all.h` (font set) | UI font is **Ubuntu**, not Inter (Inter default was reverted). |
| `lib/GfxRenderer/ImageRenderConfig.h/cpp` | Grayscale/gamma/dither shared config (see §8A / Grayscale section). |
| `src/util/PngSleepRenderer.*`, `src/util/SleepScreenCache.*` | Steroids PNG sleep renderer — **never merge upstream `patch_pngdec.py`**. |
| `enums/hyph-*` | Hyphenation dictionaries: non-core ones opt-in via `CPR_ENABLE_*_HYPHENATION`.

### Existing Protected Files — DO NOT overwrite

These files contain Steroids-only features. **Never `git checkout --theirs`**
Guide Dots, EPUB render modes). **Always keep local:**

- `lib/Epub/Epub.cpp/h`
- `lib/Epub/Epub/blocks/TextBlock.cpp/h`
- `lib/Epub/Epub/blocks/BlockStyle.h`
- `lib/Epub/Epub/ParsedText.cpp/h`
- `lib/Epub/Epub/Section.cpp/h`
- `lib/Epub/Epub/BookMetadataCache.cpp/h`
- `lib/Epub/Epub/EpubRenderMode.h`
- `lib/Epub/Epub/css/CssParser.cpp/h`
- `lib/Epub/Epub/htmlEntities.cpp`
- `lib/Epub/Epub/parsers/*`
- `lib/EpdFont/FontDecompressor.cpp/h`
- `lib/EpdFont/builtinFonts/all.h`
- `lib/GfxRenderer/GfxRenderer.cpp/h`
- `lib/EpdFont/SdCardFont.h/cpp` — chunked bitmap storage (1.5.0.20 port)
- `lib/EpdFont/SdCardFontManager.h/cpp` — standard-size detection + loadFamilyExtraSize
- `lib/EpdFont/EpdFont.h/cpp` — added `hasCodepoint()` (preserves SMALL_CAPS=64)
- `lib/EpdFont/EpdFontFamily.h/cpp` — added `hasCodepoint(Style)` (preserves enum)
- `lib/EpdFont/EpdFontData.h` — added `coverageHandler` field
- `lib/GfxRenderer/FontCacheManager.h/cpp` — `scanFontIdSet_` flag
- `src/ReadingStatsStore.h/cpp` — pace tracking + summary.json fast path
- `src/JsonSettingsIO.cpp/h` — settings JSON split (byte-identical to upstream)
- `src/JsonSettingsIOSteroids.cpp/h` — Steroids-only settings serialization
- `src/JsonSettingsIOShared.inc` — shared internal serialization helpers

---

## I18N (i18n) Workflow

1. **Always keep local `english.yaml` and `italian.yaml`** — these contain all 147 Steroids-specific strings.

2. **Add new upstream strings** that the new features need. Use a Python script to find and add missing keys:

```python
import re

def get_keys(path):
    keys = {}
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            m = re.match(r'(STR_\w+):\s*\"(.*)\"', line)
            if m:
                keys[m.group(1)] = line.rstrip('\n')
    return keys

def insert_missing(local_path, upstream_path):
    local = get_keys(local_path)
    upstream = get_keys(upstream_path)
    missing = {k: upstream[k] for k in sorted(upstream) if k not in local}
    
    if not missing:
        return
    
    lines = []
    with open(local_path, 'r', encoding='utf-8') as f:
        lines = [l.rstrip('\n') for l in f]
    
    result = []
    ki = 0
    to_add = sorted(missing.items())
    for line in lines:
        m = re.match(r'(STR_\w+):', line)
        if m and ki < len(to_add):
            while ki < len(to_add) and to_add[ki][0] < m.group(1):
                result.append(to_add[ki][1])
                ki += 1
        result.append(line)
    while ki < len(to_add):
        result.append(to_add[ki][1])
        ki += 1
    
    with open(local_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(result) + '\n')
    print(f'Added {len(to_add)} strings to {local_path}')

insert_missing('lib/I18n/translations/english.yaml', '/path/to/upstream_english.yaml')
insert_missing('lib/I18n/translations/italian.yaml', '/path/to/upstream_italian.yaml')
```

---

## Branding & OTA Checklist (After Every Merge)

After merging upstream, verify and fix these files:

| File | Check | Action |
|---|---|---|
| `src/network/CrossPointWebServer.cpp` | Logo endpoint | Ensure `#include "html/LogoPng.generated.h"` and `server->on("/logo.png", ...)` exist |
| `src/network/CrossPointWebServer.h` | Logo handler declaration | Ensure `void handleLogo() const;` exists |
| `src/network/html/HomePage.html` | Steroids branding | Title, H1, footer: "CPR-vCodex Steroids"; logo `<img>`; About card with Author (Marco Andreacchio) + GitHub link |
| `src/network/html/SettingsPage.html` | Steroids branding | Title, H1, footer: "CPR-vCodex Steroids"; logo `<img>` |
| `src/network/html/FilesPage.html` | Steroids branding | Title, H1, footer, JS strings: "CPR-vCodex Steroids"; logo `<img>` |
| `src/network/html/FontsPage.html` | Steroids branding | Title, H1, footer: "CPR-vCodex Steroids" + 📚; logo `<img>` |
| `src/network/html/IfFoundPage.html` | Steroids branding | Title, H1, footer: "CPR-vCodex Steroids" + 📚; logo `<img>` |
| `scripts/package_vcodex_bin.py` | Artifact naming | `cpr-vcodex-steroids.bin`; README regex matches "CPR-vCodex Steroids"; repo URL: `marcoand75/cpr-vcodex-steroids` |
| `.github/workflows/release.yml` | Tag pattern | `*-cpr-vcodex-steroids` |
| `.github/workflows/release.yml` | Build name | "Build CPR-vCodex Steroids release" |
| `.github/workflows/release.yml` | Release body | "CPR-vCodex Steroids firmware release" |
| `.github/workflows/sync_autoflash_firmware.yml` | Repo | `marcoand75/cpr-vcodex-steroids` |
| `docs/firmware/manifest.json` | Version/URLs | `marcoand75/cpr-vcodex-steroids`; `-cpr-vcodex-steroids` suffixes |
| `src/network/OtaUpdater.cpp` | OTA URL | Already points to `marcoand75/cpr-vcodex-steroids` — verify |
| `platformio.ini` | Version | Update to match upstream base version (e.g., 1.4.5) |
| `README.md` | Version refs | Update upstream base, firmware line, release table, artifact format |

---

## Build & Verification

After all changes:

```powershell
# Main build
python -X utf8 -m platformio run -e default -j 16

# Release build (if needed)
python -X utf8 -m platformio run -e gh_release -j 16
```

If the build fails:
1. Check for missing i18n strings — add them with the Python script above
2. Check for API mismatches between `.cpp` and `.h` files — if a `.cpp` was accidentally taken from upstream but its `.h` is local, restore the `.cpp` to local
3. Check for missing member fields in `CrossPointSettings.h` — add them manually

---

## Summary: Merge Strategy in One Command Sequence

```powershell
# 1. Backup
git tag backup-before-merge-$(Get-Date -Format yyyyMMdd-HHmmss)

# 2. Fetch upstream
git fetch upstream

# 3. Cherry-pick NEW files only (nothing that exists locally)
git checkout upstream/master -- lib/hal/HalClock.cpp lib/hal/HalClock.h
git checkout upstream/master -- src/activities/settings/ClockSyncActivity.cpp src/activities/settings/ClockSyncActivity.h
# ... (see full list above)

# 4. Take upstream for files that need new APIs
git checkout upstream/master -- src/util/TimeUtils.cpp src/util/TimeUtils.h
git checkout upstream/master -- lib/KOReaderSync/KOReaderCredentialStore.cpp lib/KOReaderSync/KOReaderCredentialStore.h
git checkout upstream/master -- src/activities/network/WifiSelectionActivity.cpp src/activities/network/WifiSelectionActivity.h

# 5. Take upstream for web server + HTML (will need branding fixes after)
git checkout upstream/master -- src/network/CrossPointWebServer.cpp src/network/CrossPointWebServer.h
git checkout upstream/master -- src/network/html/HomePage.html src/network/html/SettingsPage.html src/network/html/FilesPage.html src/network/html/FontsPage.html src/network/html/IfFoundPage.html

# 6. Manually add clock/display enums + fields to src/CrossPointSettings.h
# 7. Manually add HalClock init to src/main.cpp
# 8. Manually add clock serialization to src/JsonSettingsIO.cpp
# 9. Add missing i18n strings (Python script)
# 10. Apply branding checklist above
# 11. Build and verify
# 12. Commit
```

---

## Version Counter Management

The build system uses counter files in `artifacts/` (gitignored).
After a version bump, reset manually:

```powershell
# Set release counter to 0 for the new version line (dev builds start at .0)
echo 0 > artifacts/.release-counter-X-Y-Z.txt

# Reset dev counter
echo 0 > artifacts/.dev-counter-X-Y-Z-r0.txt
```

### Version naming: dev builds start at .0

By default, the `scripts/git_branch.py` `get_dev_release_number()` function
forces dev builds to start at `.1` when the release counter is `0`.
This has been patched in Steroids so dev builds use `.0` to match upstream
tag format (e.g., `1.5.0.0-cpr-vcodex-steroids`).

If you update `scripts/git_branch.py` from upstream, re-apply this patch:
```python
# In get_dev_release_number(~line 175):
if release_number == 0:
    return 0, f"{source} (new base line starting at .0)"
```

Without this patch, dev builds produce `1.5.0.1.dev1-...` instead of `1.5.0.0.dev1-...`.

---

## OTA Compatibility Check

Steroids OTA URLs are hardcoded in `src/network/OtaUpdater.cpp` pointing to
`marcoand75/cpr-vcodex-steroids`. After every merge verify:

```powershell
Select-String -Path src/network/OtaUpdater.cpp -Pattern "marcoand75"
# Must return: https://github.com/marcoand75/cpr-vcodex-steroids/releases/...
```

If it returns `franssjz/cpr-vcodex`, restore the local Steroids version.

---

## KOReaderCredentialStore API Migration

Upstream 1.4.5 changed `KOReaderCredentialStore` from single-profile (direct
member access: `store.username`, `store.password`) to multi-profile (getter/setter
API + profiles vector). When you take upstream's `KOReaderCredentialStore.h/cpp`,
you must also update `JsonSettingsIO.cpp` to use the new API:

1. `saveKOReader` → save as `profiles[]` array (multi-profile format)
2. `loadKOReader` → load `profiles[]` array, migrate legacy single-profile on first load
3. Add `saveKOReaderLegacyMirror` → saves single-profile for backwards compat
4. Add `loadKOReaderLegacyProfile` → loads old single-profile JSON format

All four functions are provided above in the `JsonSettingsIO.cpp` section.
The upstream `JsonSettingsIO.cpp` contains the reference implementation.

---

## Post-Merge Verification Checklist

After completing a merge, verify these items ON DEVICE (not just build):

| # | Check | Expected result |
|---|---|---|
| 1 | Open Settings → Controls → Front Long Press | Should show OFF/Bookmark/Clipping options |
| 2 | Open Settings → Apps → Clippings Shortcut | Should show location picker |
| 3 | Open Settings → Apps → Library Shortcut | Should show location picker |
| 4 | Open Settings → Apps → Screensaver Shortcut | Should show location picker |
| 5 | Open Apps Hub → Icons (LyraMarcoand75 theme) | All app icons visible with correct order |
| 6 | Open Web Browser → Settings | Device settings visible |
| 7 | Open Web Browser → App Settings | App settings visible with Steroids sections |
| 8 | Open Web Browser → Home | Logo.png visible, About card with Author + GitHub link |
| 9 | Long press left/right side buttons in reader | Should trigger configured action (chapter skip, bookmark, clipping, orientation, font size) |
| 10 | Long press front buttons in reader | Should trigger configured action (bookmark, clipping, chapter skip, orientation, font size) |
| 10b | Long press UP/DOWN side button (font size mode) | Font size increases on DOWN, decreases on UP |
| 10c | Open Settings > Controls > Long-press side buttons | All 6 options present: OFF, Bookmarks, Clippings, Chapter skip, Orientation change, Font size |
| 10d | Open Settings > Controls > Long-press front buttons | Same 6 options as side buttons |
| 10e | Open Settings > Customize Status Bar | 11 items including Clock position, Clock format, Sync clock now |
| 10f | Open Home screen (top header) on X3 with DS3231 | Date/time visible even without prior NTP sync |
| 11 | Open Reading Stats | Should show pace info and book stats |
| 12 | Library cover generation | Should not crash on corrupt EPUBs |

If any of these fail, the merge has overwritten Steroids-specific code.
Refer to the "Files to NEVER Overwrite" section and restore the local version.

---

---

## X3 DS3231 RTC: applySystemClockFromRtc without clockHasBeenSynced

On X3 devices with the DS3231 hardware RTC (`halClock.isAvailable() == true`),
the function `TimeUtils::applySystemClockFromRtc()` copies the DS3231 time to the
ESP32 system clock via `settimeofday()`. Upstream gates this on both
`halClock.isAvailable()` AND `SETTINGS.clockHasBeenSynced`.

**Steroids removes the `clockHasBeenSynced` requirement.** Without this change, an
X3 user who has never connected to WiFi for NTP sync gets no system clock time,
so `getAuthoritativeTimestamp()` returns 0, and the top header date (`drawTopLine` in
`HeaderDateUtils`) is empty. The status bar clock still works because it reads directly
from the DS3231 via `halClock.readUtcEpoch()`, bypassing the system clock.

The `isClockValid(epoch >= 2024-01-01)` check after reading the DS3231 provides
sufficient protection against factory-default or corrupted RTC values.

**If upstream modifies `TimeUtils.cpp`, re-apply this fix** — remove the
`!SETTINGS.clockHasBeenSynced` condition in `applySystemClockFromRtc()`.

## Long-press Button Behavior: Expanded Options

Upstream provides only 3 options for side-button long-press (OFF, Chapter Skip,
Orientation Change) and has NO separate front-button long-press setting.

The Steroids `CrossPointSettings.h` expands both enums to 6 options in the same order:

| Value | Side (`LONG_PRESS_BUTTON_BEHAVIOR`) | Front (`FRONT_LONG_PRESS_BEHAVIOR`) |
|-------|-------------------------------------|------------------------------------|
| 0 | `LONG_PRESS_OFF` | `FRONT_LONG_PRESS_OFF` |
| 1 | `LONG_PRESS_BOOKMARK` | `FRONT_LONG_PRESS_BOOKMARK` |
| 2 | `LONG_PRESS_CLIPPING` | `FRONT_LONG_PRESS_CLIPPING` |
| 3 | `LONG_PRESS_CHAPTER_SKIP` | `FRONT_LONG_PRESS_CHAPTER_SKIP` |
| 4 | `LONG_PRESS_ORIENTATION_CHANGE` | `FRONT_LONG_PRESS_ORIENTATION` |
| 5 | `LONG_PRESS_FONTSIZE` | `FRONT_LONG_PRESS_FONTSIZE` |

### Search patterns (find upstream diffs that touch these enums):
```powershell
Select-String -Path src/CrossPointSettings.h -Pattern "LONG_PRESS_BUTTON_BEHAVIOR"
Select-String -Path src/CrossPointSettings.h -Pattern "FRONT_LONG_PRESS_BEHAVIOR"
```

### Implementation files (never take upstream from these):
| File | What changed |
|------|-------------|
| `src/CrossPointSettings.h` | Expanded enums, `frontLongPressBehavior` member field |
| `src/activities/reader/EpubReaderActivity.cpp` | `!fromFrontButton` guard on side-button handlers, front-button handler block, font-size lambda calling `ensureSdFontLoaded()` |
| `src/activities/reader/XtcReaderActivity.cpp` | `fromFrontButton` guard on skip-pages |
| `src/activities/reader/TxtReaderActivity.cpp` | `fromFrontButton` guard, front-button orientation handling |
| `src/activities/settings/SettingsActivity.cpp` | Option lists with 6 entries |
| `src/SettingsList.cpp` | Option lists with 6 entries, clock display settings |
| `src/network/CrossPointWebServer.cpp` | `OPT_LONG_PRESS_BEHAVIOR` and `OPT_FRONT_LONG_PRESS_BEHAVIOR` arrays |
| `lib/I18n/translations/english.yaml` | `STR_LONG_PRESS_BEHAVIOR_FONTSIZE` key |
| `lib/I18n/translations/italian.yaml` | `STR_LONG_PRESS_BEHAVIOR_FONTSIZE` key |

## Clocks, Timers, and X3/X4 Differences

### Status bar clock (bottom of reading screen)

| Aspect | Upstream | Steroids |
|--------|----------|----------|
| Available devices | DS3231 RTC only (probed on X3) | Same — `halClock.isAvailable()` gates on DS3231 presence on I2C bus |
| Clock position | Same enum (`STATUS_BAR_CLOCK` with HIDE/RIGHT/LEFT) | Same |
| Clock format | Same (`clockFormat`: 0=12h, 1=24h) | Same |
| Clock settings location | On-device: `Customize Status Bar` menu (8 items) + Web Settings | On-device: `Customize Status Bar` menu (11 items — added Clock position, Clock format, Sync clock now) |
| Sync clock now action | `ClockSyncActivity` launched from Web Settings only | Also accessible from on-device `Customize Status Bar` menu |

### Top header date (Home, Library, Settings screens)

| Aspect | Upstream | Steroids |
|--------|----------|----------|
| `displayDay` setting | Same enum (OFF, DATE_ONLY, TIME_ONLY, BOTH) | Same |
| `applySystemClockFromRtc()` guard | `!halClock.isAvailable() \|\| !SETTINGS.clockHasBeenSynced` | `!halClock.isAvailable()` only (clockHasBeenSynced removed) |
| X3 with DS3231, no NTP sync | Date header empty (system clock never set from RTC) | Date header shows current date (RTC applied on first access) |
| X4 without DS3231 | Date depends on NTP sync via WiFi connection | Same (X4 behavior unchanged) |

### `displayDay` default value

Both upstream and Steroids default to `DISPLAY_HEADER_TIME_ONLY = 2`.

The `getDisplayDateText()` function returns empty only when `displayDay == 0` (OFF).
For any non-zero value, the formatted date string is drawn in the top header via
`drawHeaderTopLine()`.

### Search patterns for clock merge conflicts:
```powershell
# Check if upstream changed applySystemClockFromRtc
Select-String -Path src/util/TimeUtils.cpp -Pattern "clockHasBeenSynced"
# Expected Steroids: only non-applied boot conditional (WifiSelectionActivity)
# The applySystemClockFromRtc guard must NOT have clockHasBeenSynced

# Check if upstream changed the StatusBarSettings menu item count
Select-String -Path src/activities/settings/StatusBarSettingsActivity.cpp -Pattern "MENU_ITEMS"
# Expected Steroids: MENU_ITEMS = 11 (upstream has 8)
```

---

## Incremental Merge Workflow (from an already-merged base)

When Steroids **already has the previous release merged** (e.g., 1.5.0.2 is merged
and only 1.5.0.3 needs to be applied), follow this lightweight workflow instead of
the full merge procedure above.

### Step 1: Identify the exact delta

```powershell
git fetch upstream --tags
git log --oneline <PREVIOUS_TAG>..<CURRENT_TAG>
```

For example, to see the delta between 1.5.0.2 and 1.5.0.3:
```powershell
git log --oneline 1.5.0.2-cpr-vcodex..1.5.0.3-cpr-vcodex
```

This gives you 2–5 commits at most (the release-specific changes, not the entire
upstream history).

### Step 2: Inspect only the code-relevant commits

Ignore `docs(release): sync auto-flash firmware X.Y.Z.W` commits — those are
firmware binaries and docs only. Focus on actual code commits like
`fix(stats): restore valid JSON imports`.

```powershell
# Show all files touched by relevant commits
git diff <PREVIOUS_TAG>..<CURRENT_TAG> --stat
```

### Step 3: Extract the exact code changes

Use `git diff` between the two tags, **only for files that exist locally**:

```powershell
git diff <PREVIOUS_TAG>..<CURRENT_TAG> -- src/File.cpp src/OtherFile.cpp
```

View the full diff of only the actual code files (skip docs, scripts, generated files):
```powershell
git diff <PREVIOUS_TAG>..<CURRENT_TAG> -- src/JsonSettingsIO.cpp src/ReadingStatsStore.cpp
```

### Step 4: Apply changes manually to local files

Use the **edit** tool to surgically apply only the specific changes from the delta,
preserving all Steroids-specific code around them. Never `git checkout` the
upstream version of a file that Steroids has modified.

### Step 5: Check None of the "Protected Files" are touched

Run the 1.5.0.3 delta diff against the protected files list above. If any
protected files appear in `git diff <PREVIOUS_TAG>..<CURRENT_TAG> --stat`,
manually verify the delta does not remove Steroids features.

### Step 6: Build and verify

```powershell
python -X utf8 -m platformio run -e default -j 16
```

If the build fails, the delta introduced an API dependency that Steroids doesn't
have yet — check for:
- New enum values in `CrossPointSettings.h` that need to be added
- New i18n strings that need translation keys
- New function declarations in headers exposed by the delta

### Step 7: Verify `platformio.ini` Steroids-only flags are preserved

After a merge or rebase, `platformio.ini` may lose Steroids-specific build flags
that are vital for runtime stability. Check that these lines are still present
in the `[base] build_flags` section:

```powershell
Select-String -Path platformio.ini -Pattern "ARDUINO_LOOP_STACK_SIZE|PNG_MAX_BUFFERED_PIXELS"
# Expected output:
#   -DARDUINO_LOOP_STACK_SIZE=9216
#   -DPNG_MAX_BUFFERED_PIXELS=8192
```

**Why each flag is needed:**

| Flag | Value | Effect |
|---|---|---|
| `-DARDUINO_LOOP_STACK_SIZE` | **9216** | Prevents stack protection fault when entering Settings. The `getDeviceOnlyAppSettings()` static initializer constructs 53 `SettingInfo` entries with inline `std::vector<StrId>` temporaries, which uses ~9 KB of stack. The Arduino default of 8192 overflows by 1 word. |
| `-DPNG_MAX_BUFFERED_PIXELS` | **8192** | Supports screensaver/sleep-screen PNG images up to 1023 px wide. The library default (2562) only supports 320 px. X4 screensaver images are 800 px in landscape. Formula: `(maxPixels * 4 + 1) * 2`. |

### Example: 1.5.0.2 → 1.5.0.3 merge

The 1.5.0.3 delta was just 2 commits (1 docs-only, 1 code):

```
920cffd1 docs(release): sync auto-flash firmware 1.5.0.2  ← docs only, skip
096bc73b fix(stats): restore valid JSON imports            ← actual code change
```

The code change touched `src/JsonSettingsIO.cpp` and `src/ReadingStatsStore.cpp`.
Changes applied:
1. **`saveJsonDocumentToFile`** — added `doc.overflowed()` check, `measureJson()` for
   expected size, incomplete-write detection, and `copyVerifiedJsonTempToTarget()`
   rename fallback when `Storage.rename()` fails on FAT filesystems.
2. **`loadJsonDocumentFromFile`** — added `doc.overflowed()` check to parse errors.
3. **`loadReadingStats` → extracted `loadReadingStatsDocument`** — improved JSON
   import validation with `JsonObjectConst`/`JsonArrayConst` type checks,
   formatVersion range validation, array key presence checks, and per-entry type
   validation. Steroids-specific fields (`avgSecondsPerForwardPage`,
   `paceSampleCount`) preserved.
4. **`ReadingStatsStore::importFromFile`** — split empty-path check, added
   `CPR_VCODEX_LOG_EVENT` logging for empty/missing/rejected paths.

### Example: 1.5.0.3 → 1.5.0.5 merge

The 1.5.0.5 delta was 3 code commits (plus docs/version-bump only):

```
47f10008 docs(release): sync auto-flash firmware 1.5.0.3     ← docs only, skip
81a288b4 fix(storage): stamp SD files with RTC or last Sync Day time   ← 1.5.0.4
2c02674b fix(reader): preserve exact EPUB reopen position     ← 1.5.0.5
```

**Applied to Steroids:**

1. **SD FAT timestamps (1.5.0.4)** — clean, non-conflicting storage improvement:
   - Added new `src/util/SdFatDateTime.cpp` with `TimeUtils::registerSdFatDateTimeCallback()`.
   - Added `TimeUtils::getBestEffortFileTimestamp()` to `src/util/TimeUtils.cpp`.
   - Added both declarations to `src/util/TimeUtils.h`.
   - Wired `#include "util/TimeUtils.h"` + `TimeUtils::registerSdFatDateTimeCallback()` in `src/main.cpp`
     after the SD-card init block.
   - `ScreenshotUtil.cpp` gained a comment-only note.
   - The Steroids `applySystemClockFromRtc()` fix (no `clockHasBeenSynced` guard) was **preserved** —
     only `getBestEffortFileTimestamp()` was added, never the upstream file wholesale.

2. **EPUB reopen position (1.5.0.5)** — **NOT ported into the Steroids reader.**
   The fix targets upstream's progressive/async cache reader (`applyDeferredReposition()`,
   `buildTickHeapGate()`, `estimatedTotalPages()`, `pendingPaginationReposition`). The Steroids
   `EpubReaderActivity.cpp` is a synchronous section-build architecture (`createSectionFile`,
   `section->pageCount`) that already preserves the exact saved page unless the page count
   actually changes (the percentage remap only fires on `pageCount != cachedChapterTotalPageCount`).
   So the bug upstream fixed (page jumps from converging progressive estimates) cannot occur in
   Steroids. Porting the flag machinery would add risk to a heavily Steroids-modified file with no
   behavioral gain. Instead:
   - Added the new `src/activities/reader/ReaderPosition.h` helper (self-contained, unused).
   - Added `test/reader_position/` unit tests + `add_subdirectory(reader_position)` in
     `test/CMakeLists.txt` for future parity.
   - Did **not** touch the repositioning logic in `EpubReaderActivity.cpp`.

**Protected/unchanged in this delta:** `src/activities/reader/*`, `src/components/Library*`,
`src/JsonSettingsIO.*`, `src/network/CrossPointWebServer.*`, web HTML, i18n yaml, settings lists,
`platformio.ini` Steroids flags (`ARDUINO_LOOP_STACK_SIZE=9216`, `PNG_MAX_BUFFERED_PIXELS=8192`).

**Verification:** `python -X utf8 -m platformio run -e default -j 16` → SUCCESS;
dev artifact `1.5.0.5.dev1-...-cpr-vcodex-steroids.bin`.

### Power button handling (main.cpp loop)

**Divergence from upstream:** upstream unconditionally calls `enterDeepSleep()` on every
power button press edge (`gpio.wasPressed(BTN_POWER)`), regardless of the `shortPwrBtn`
setting (`IGNORE`, `SLEEP`, `PAGE_TURN`, `FORCE_REFRESH`, `TOGGLE_STATUS_BAR`). This means
that only `SLEEP` mode works; all other values are unreachable.

**Fix applied in Steroids:** the power button is now treated as a short/long-press event:

- **Short press** (< `getPowerButtonDuration()`, 400 ms for non-SLEEP modes): the release
  edge triggers the configured `shortPwrBtn` action — `FORCE_REFRESH` refreshes the screen,
  `TOGGLE_STATUS_BAR` toggles the status bar, `PAGE_TURN` turns the page, `IGNORE` does nothing.
- **Long press** (held ≥ `getPowerButtonDuration()`): always deep sleeps, or starts the
  replacement screen saver if one is configured for the current reader activity and battery
  condition (`canStartReplacementScreenSaver()`).
- **Active screen saver**: the long-press check is skipped so the screen saver can process
  the wake button without interference. The release edge is also suppressed so that a brief
  press used to dismiss the screen saver does not accidentally fire the configured `shortPwrBtn`
  action.

**Files changed:** `src/main.cpp` — restructured the power button event loop from an
unconditional `enterDeepSleep()` on press edge to state machine tracking `powerBtnDownMs`
and `powerBtnInScreensaver` flags.

**Verification:** `python -X utf8 -m platformio run -e default -j 16` → SUCCESS.

---

## Grayscale Image Pipeline (2-bit, 4-level) divergence

**Divergence from upstream:** upstream's BMP/cover/screensaver grayscale pipeline
is flat and pale on X4/X3 e-ink because its image-adjustment path is disabled
(`USE_BRIGHTNESS = false`) and each converter carries its own local constants.
Steroids reworks this into a shared, contrast-correct, overflow-safe pipeline.

### What changed (Steroids-only)
- **New `lib/GfxRenderer/DitheringConfig.h`** — single shared config: dither method
  (`USE_ATKINSON=true`, `USE_FLOYD_STEINBERG=false`), the 4 gray levels
  (0/85/170/255), `GAMMA_VALUE=1.5`, and the shared `gammaLUT[256]` +
  `initGammaLUT()`.
- **`Bitmap.cpp`, `JpegToBmpConverter.cpp`, `PngToBmpConverter.cpp`** — use the
  shared config (their local `USE_ATKINSON`/`USE_FLOYD_STEINBERG`/dither consts
  removed) and apply `adjustPixel()` (gamma LUT) before any error diffusion.
- **`BitmapHelpers.cpp`** — `adjustPixel()` maps input luminance through the
  gamma `gammaLUT`; `quantizeSimple()` uses empiric thresholds **50/120/200**
  (upstream 43/128/213); adds `unquantize()` (0/85/170/255).
- **`BitmapHelpers.h`** (ditherers) — `AtkinsonDitherer`, `Atkinson1BitDitherer`,
  `FloydSteinbergDitherer`: per-pixel math in `int16_t` (`accumulated`, `gray`,
  `reconstructed`, `error`); **error computed on the pre-clamp value**; pure-integer
  diffusion (`error >> 3`, and `(error*7)>>4 / (error*3)>>4 / (error*5)>>4 / (error)>>4`);
  horizontal error dropped at the last pixel of a row; buffers `int16_t[width+pad]`
  allocated once with a **+2 base offset** (no negative index, safe at `x = 0`).
- **`src/main.cpp`** — calls `initGammaLUT()` at boot; `adjustPixel()` also has a
  lazy-init guard so an uninitialized LUT can never produce an all-black image.

### On merge
If upstream touches any of these files (`lib/GfxRenderer/Bitmap*.cpp/h`,
`lib/JpegToBmpConverter/*`, `lib/PngToBmpConverter/*`), **keep the local Steroids
version** and manually re-add only genuinely new upstream logic. See the detailed
section in `STEROIDS-ADDICTIONS.md` §8A for the full design rationale.

---

## Upstream Sync Status

### Current upstream base
Steroids HEAD (`d3e21a61`) is synced through upstream **1.5.0.22** (`upstream/master` at
`1c060a24`). The following upstream releases have been incorporated:

| Release | Upstream tag | Status in Steroids | Notes |
|---|---|---|---|
| 1.5.0.20 | `3e46941c` | **Fully Ported** (`72515f4f` + `d3e21a61`) | SdCardFont fragmentation-resistant storage, HAL crash detection |
| 1.5.0.21 | `723a1a4d` | **Not yet ported** | EPUB image + low-memory handling |
| 1.5.0.22 | `1c060a24` | **Not yet ported** | docs only (auto-flash firmware sync) |

Steroids HEAD (`a605404d`) also includes the carousel recents panel fix:
the cyber panel (showing book count + filter) is now only visible in
`LYRA_MARCOAND75` theme, and `homeRecentBooksCount` increased from 3 to 20
in `LyraCarouselTheme` (clamped to `HOME_MAX_BOOKS=10`).

The next upstream pull target is **1.5.0.21** (`723a1a4d`), which contains EPUB image
hardening and low-memory fixes that need to be re-applied on top of the Steroids
CrossInk engine (see §"What changed since" below for divergence details).

### Completed 1.5.0.20 SdCardFont port

The SdCardFont fragmentation-resistant bitmap storage from upstream 1.5.0.20
has been fully ported (`72515f4f`):

- **`lib/EpdFont/SdCardFont.h` / `SdCardFont.cpp`** — replaced single-buffer
  `miniBitmap` with 24 × 4 KiB `miniBitmapChunks[24]` chunked storage, eliminating
  large contiguous allocation needs on the 380 KB RAM ESP32-C3. Added `TextGetter`
  typedef + `prewarm` overload, `loadKernLig` param, `miniGlyphBitmap()` overflow
  fallback, `onCoverageQuery()` static callback for `coverageHandler` dispatch.
- **`lib/EpdFont/EpdFont.h` / `EpdFont.cpp`** — added `hasCodepoint(uint32_t cp)`
  method with coverage handler support (binary search intervals + coverageHandler fallback).
- **`lib/EpdFont/EpdFontData.h`** — added `bool (*coverageHandler)(void* ctx, uint32_t codepoint)` field.
- **`lib/EpdFont/EpdFontFamily.h` / `cpp`** — added `hasCodepoint(uint32_t cp, Style style)`.
  **Steroids enum preserved:** `SMALL_CAPS=64` and `RUBY_CONTINUE=128` kept (upstream removed SMALL_CAPS and renumbered RUBY_CONTINUE to 64).
- **`lib/EpdFont/FontDecompressor.h` / `cpp`** — replaced `std::vector<uint8_t>` with
  raw `uint8_t*` + capacity for `hotGroup`/`hotGlyphBuf`; removed `isInitialized()`/`_initialized`.
- **`lib/GfxRenderer/GfxRenderer.h` / `cpp`** — added `FrameBufferLoan` class (via
  `BuildScratch.h`), `resolveTextFontId()` for CJK fallback font resolution,
  `ensureSdGlyphsResident()`, `setFallbackFont()`/`clearFallbackFonts()`,
  `fallbackFontMap_` member, `TextGetter` typedef + `prewarmFallbackText()`,
  vector-based `invertMonocheBitmap`, `miniGlyphBitmap` fallback in `getGlyphBitmap`.
  `ensureSdCardFontReady` changed from `std::vector` to `std::deque` with inline
  backward-compat wrapper.
- **`lib/EpdFont/SdCardFontManager.h` / `cpp`** — refactored `loadFamily` with
  standard-size detection (prefers 12/14/16/18), `loadFile` helper,
  `loadFamilyExtraSize` for non-standard sizes.
- **`lib/GfxRenderer/FontCacheManager.h` / `cpp`** — added `scanFontIdSet_` flag
  to fix negative SD font ID handling.
- **`src/main.cpp`** — removed `isInitialized()` guards (lazy init removed upstream).

**Build:** SUCCESS — RAM 16.0% (52276/327680), Flash 98.5% (6455495/6553600), 0 warnings.

### Completed 1.5.0.20–22 alignment (commit `669ccb1a` + `f467593a`)

The following upstream 1.5.0.20–22 changes were ported in commit `669ccb1a`
(2026-08-22) and `f467593a` (2026-08-22):

**Phase 1 — WifiCredentialStore security hardening:**
- `lib/Serialization/CredentialIntegrity.h` — new CRC-32 constexpr utility
- `lib/Serialization/ObfuscationUtils.h/cpp` — bounded `deobfuscateFromBase64`
  overload, thread-safe `std::array` key init
- `src/WifiCredentialStore.h/cpp` — mutable mutexes, `MAX_PASSWORD_LENGTH=64`,
  `saveToFileUnlocked`, `hasCredentials()`, `findCredential` returns `std::optional`,
  `getLastConnectedSsid` returns `std::string`, `getCredentials` returns vector copy
- `src/JsonSettingsIO.cpp` — `saveWifi`/`loadWifi` with CRC-32 + password length,
  legacy plaintext migration, mutex locking

**Phase 1c — WifiSelectionActivity:** `autoConnectOnly` constructor param,
  `sdFontSystem.releaseForNetwork` + MemoryBudget instrumentation, early-complete
  paths, `findCredential` updated to `std::optional`.

**Phase 1d/1e — hideFileExtension + EXTRA_WIDE:**
  `EXTRA_WIDE=3` in `LINE_COMPRESSION` enum, `hideFileExtension` member,
  `STR_HIDE_FILE_EXTENSION`/`STR_EXTRA_WIDE`/`STR_FIRMWARE_WRONG_DEVICE` i18n,
  FileBrowser extension column hidden when set.

**Phase 2 — OTA wrong-device rejection:**
  `WRONG_DEVICE_ERROR` in `OtaUpdaterError`, `BAD_CHIP → WRONG_DEVICE_ERROR` mapping,
  `STR_FIRMWARE_WRONG_DEVICE` in `OtaUpdateActivity`, `BAD_CHIP` in
  `FirmwareFlasher::Result`.

**Phase 3 — KOReaderSyncActivity:** `isAutomaticSyncIntent()` helper,
  auto-sync uses `autoConnectOnly=true`, `silentRestartToHome` replaced with
  `activityManager.goHome()` (Steroids preserves `fromReaderExit=true` for
  `ReadingStatsDetailActivity` heap defragmentation).

**Phase 4 — Write-only KOReader password (web):** `CrossPointWebServer.cpp`
  returns empty password with `configured:true/false` boolean.

**Phase 5 — FileBrowser hideFileExtension:** extension getter returns empty
  string when `SETTINGS.hideFileExtension`.

**Phase 6 — KOReaderSyncClient HTTP robustness:** accepts all 2xx, treats 204
  as `NOT_FOUND` in `getProgress`.

**Phase 7 — CJK codepoint detection:** `lib/Utf8/Utf8.h` —
  `utf8IsCjkCodepoint()` for UI font fallback selection.

**Phase 8 — ConfirmationActivity:** `MAX_BODY_LINES=6` (was 8),
  `safeHeading` (single-line truncated), `bodyLines` (newline-paragraph word-wrap).

**Phase 3 remaining (`f467593a`):** Web Server serial number (reads
  `ESP_EFUSE_USER_DATA`), `FirmwareFlasher::runningPartitionChipId()` (cached
  chip_id validation in `validateImageFile()`).

**Build:** SUCCESS — RAM 15.9%, Flash 98.5%, 0 new warnings.

### HAL crash detection (PANIC_CAPTURE_MAGIC) — PORTED
The `PANIC_CAPTURE_MAGIC` watchdog crash detection from upstream 1.5.0.20 has
been ported (`d3e21a61`):

- **`lib/hal/HalSystem.cpp`** — added `PANIC_CAPTURE_MAGIC 0x50414E49u` +
  `RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker`. Set in
  `__wrap_panic_abort` and `__wrap_panic_print_backtrace`. `isRebootFromPanic()`
  now treats watchdog resets (`ESP_RST_INT_WDT`/`ESP_RST_TASK_WDT`/`ESP_RST_WDT`)
  as panic reboots ONLY when the magic marker is set — normal deep-sleep wake
  cycles (which use watchdog) won't false-positive. `checkPanic()` verifies
  write completeness and clears the marker on success. `clearPanic()` clears
  the marker. Updated `begin()` comment.
- **`src/activities/home/CrashActivity.cpp`** — removed explicit
  `HalSystem::clearPanic()` call; `checkPanic()` already clears the watchdog
  marker after successful SD dump.

**Build:** SUCCESS — RAM 16.0% (52292/327680), Flash 98.5% (6455663/6553600), 0 warnings.

### Remaining deferred: SdCardFontRegistry case-insensitive dirs
Not needed — Steroids uses a different font directory management approach.

---

## Deferred Items Summary

| Item | Risk | Status |
|---|---|---|
| SdCardFontRegistry case-insensitive dirs | LOW | NOT NEEDED — Steroids uses different font management |
| Web Server serial number | — | COMPLETED (commit `f467593a`) |
| FirmwareFlasher chip validation | — | COMPLETED (commit `f467593a`) |
| HAL crash detection (`PANIC_CAPTURE_MAGIC`) | MEDIUM | COMPLETED (commit `d3e21a61`) |
| Carousel recents panel (Lyra Carousel) | LOW | COMPLETED (commit `a605404d` + `b2601c90`) — removed cyber panel from LYRA_CAROUSEL, kept only in LYRA_MARCOAND75; increased `homeRecentBooksCount` from 3 to 20; matched `kVisibleMenuSlots` to 7 (same as Marcoand75) |

## What changed since `07126f2b` — align-upstream notes

Base `07126f2b` (2026-08-09) → HEAD `d3e21a61` (2026-08-23). Key merge-sensitive
deltas to watch in the next upstream pull. Details by feature in
`STEROIDS-ADDICTIONS.md` §21.

### Divergence that will conflict on upstream merge
- **CrossInk EPUB engine** (`lib/Epub/epub/*`, `MiniBidi`, `lib/miniz`, `lib/Memory/Arena.*`)
  replaced the EPUB stack. Upstream EPUB changes must be re-applied on top of the
  CrossInk files — do **not** take upstream `lib/Epub/*` wholesale. The render
  signature is now `foregroundBlack` (bionic-mode param removed): keep the
  `c2a65b20` fix (foreground black when bionic OFF). **Steroids EPUB section cache is
  at `SECTION_FILE_VERSION=60`** (ahead of upstream v47); do **not** port the version
  bump. The `BookmarkStore` is at Steroids v4 (`absoluteWordStart`) vs upstream v5
  (`visibleTextOffset`); do **not** port.
- **Reading stats:** upstream re-adding `src/ReadingStats/` binaries will conflict with
  the decision to keep vCodex JSON only. Reject binary reintroduction.
- **PNG/screensaver:** never import upstream `patch_pngdec.py` (incompatible with
  `PngSleepRenderer`). `ScreenSaverActivity`/`SleepActivity`/`PngSleepRenderer` are
  protected Steroids files.
- **ZipFile EOCD scan stays at 1 KB.** If upstream grows the EOCD scan (> 1 KB) or
  streams it, do not take it (caused a ~131 KB malloc failure / infinite loop here) —
  keep the 1 KB scan.
- **UI font is Ubuntu** (upstream may default to Inter). Keep local `lib/EpdFont`
  font set and hyphenation opt-in flags (`CPR_ENABLE_*_HYPHENATION`).
- **Boot memory:** `src/main.cpp` and the lazy-load of `*Store`s diverge (eager in
  upstream). `src/main.cpp` is a protected file (power-hold sequence, silent restart,
  boot instrumentation, initGammaLUT, power-button state machine).
- **Home reading-stats summary.json fast path:** Steroids reads a small derived
  `/.crosspoint/summary.json` on the Home screen and keeps the full
  `reading_stats.json` store (~41 KB) out of RAM at boot; upstream loads the store
  eagerly. This lives entirely in the protected `ReadingStatsStore.{h,cpp}` +
  `BootActivity.cpp` + the Lyra themes. `summary.json` is a **derived** artifact —
  never back it up or import it; it is regenerated on the next stats save if missing.
  See `STEROIDS-ADDICTIONS.md` §22.
- **Library cover cache** uses FNV-1a 64 (aligned with `Epub`) instead of upstream
  `std::hash`; a hash mismatch is what used to make cover thumbs and book-cache
  deletion miss.
- **Wikipedia** is a full Steroids app (`WikipediaActivity`/`WikiTxtReaderActivity`)
  plus cache plumbing (`HalStorage::listFiles(includeDirectories=false)`),
  `title.txt`, per-article `wiki_<hash>` folders. English/Italian yaml carry the
  Steroids string keys (keep local; see I18N workflow).
- **Settings JSON split** (2026-08-04): `JsonSettingsIO.cpp` is byte-identical to
  upstream; all 37 Steroids-only fields live in `JsonSettingsIOSteroids.cpp`.
  `CrossPointSettings.h` is the only conflict zone (~20 lines of POD struct changes).
  See §3 of this guide for the merge procedure.
- **SdCardFont storage** is now at the Steroids-cherry-picked 1.5.0.20 version.
  Upstream may change the `TextGetter` callback signature or `coverageHandler`
  registration. If upstream changes `lib/EpdFont/SdCardFont.*` or
  `lib/GfxRenderer/GfxRenderer.*`, keep local and manually re-apply.
- **WifiCredentialStore security** (1.5.0.20–22 port): `CredentialIntegrity.h`,
  `ObfuscationUtils.h/cpp` bounded overload, thread-safe `WifiCredentialStore` with
  `MAX_PASSWORD_LENGTH=64`, CRC-32 password validation, `JsonSettingsIO.cpp`
  `saveWifi`/`loadWifi` updates. `JsonSettingsIO.cpp` was already byte-identical to
  upstream (settings split v2), so these changes applied cleanly.
- **KOReaderSyncActivity** (`silentRestartToHome` → `goHome`): Steroids preserves its
  own `silentRestartToHome()` for Home returns (heap defragmentation); the upstream
  change is noted but Steroids overrides the restart call. The
  `fromReaderExit=true` flag for `ReadingStatsDetailActivity` is Steroids-specific.

### Protected-file additions since `07126f2b`
See the "New Additions (2026-08-09 → 2026-08-18)" table above. In particular the
`lib/Epub/epub/*`, `lib/MiniBidi/*`, `lib/miniz`, `lib/Memory/Arena.*`, and the
per-article Wikipedia cache files must never be replaced by upstream copies.

New since 1.5.0.20 port:
- `lib/Serialization/CredentialIntegrity.h` — new file (CRC-32 constexpr)
- `lib/Memory/BuildScratch.h` / `BuildScratch.cpp` — already present (FrameBufferLoan
  support for SdCardFont port)
- `src/util/SdFatDateTime.cpp` (from 1.5.0.4) — already ported

### Suggested incremental-merge focus for this delta
Priority order if cherry-picking the next upstream release (1.5.0.21) into this base:
1. **EPUB engine fixes** (1.5.0.21) — re-apply to CrossInk files, verifying `foregroundBlack`.
   The section cache version (v60) and BookmarkStore (v4) divergences are protected.
2. Any updated `.yaml`/i18n keys **except** english/italian (keep local).
3. Settings bugfixes excluding `SettingsActivity.cpp` (label) and `settingsCount`
   logic (already fixed locally).
4. Re-check `freeink-sdk` submodule pin (SDCardManager `listFiles` signature).
5. If 1.5.0.22 has non-docs changes, review for SdCardFont/EPUB engine touch points.

### Merge command sequence for 1.5.0.21
```powershell
# 1. Backup
git tag backup-before-1.5.0.21-$(Get-Date -Format yyyyMMdd-HHmmss)

# 2. Fetch upstream tags
git fetch upstream --tags

# 3. Show the 1.5.0.21 delta
git diff 1.5.0.20-cpr-vcodex..1.5.0.21-cpr-vcodex --stat

# 4. For each changed file that exists locally and is NOT in the protected list,
#    apply only the specific code changes manually (edit tool), never git checkout.

# 5. EPUB engine changes (if any) — re-apply to CrossInk files

# 6. Build and verify
python -X utf8 -m platformio run -e default -j 16
```

---

*Last updated: 2026-08-23 — updated upstream sync status (1.5.0.20 fully ported incl. HAL crash detection), added completed alignment phases summary, SdCardFont port details, HAL crash detection port details, carousel recents panel fix (a605404d), WifiCredentialStore security notes, settings JSON split reference, EPUB/MarkdownStore divergence, and new HAL/FontManager protected files.*
