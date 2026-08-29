# CPR-vCodex Steroids — App Definitions & Enhancements

> **SCOPE OF THIS FILE**
>
> `STEROIDS-ADDICTIONS.md` is the **single source of truth for all Steroids apps and
> enhancements** relative to upstream (`franssjz/cpr-vcodex` → fork base
> `marcoand75/cpr-vcodex-steroids`). It describes every **app**, every **screensaver /
> sleep / deep-sleep** behavior, and every **feature** Steroids adds.
>
> The other Steroids definition file is `STEROIDS-ALIGN-TO-UPSTREAM.md`, which only
> contains the **workflow** to merge a new upstream release into Steroids while
> preserving everything listed here.
>
> **Two Steroids definition files:**
> | File | Purpose |
> |---|---|
> | `STEROIDS-ADDICTIONS.md` | All Steroids apps, screensavers, sleep/screens, and enhancements (this file) |
> | `STEROIDS-ALIGN-TO-UPSTREAM.md` | Instructions for merging upstream while keeping Steroids features |

This document consolidates the former `STEROIDS-CLIPPINGS-BOOKMARKS.md`,
`STEROIDS-LIBRARY.md`, and `STEROIDS-APP-ICON-THEME.md`, and adds the full app
catalog (including the **Wikipedia** app and the **deep-sleep / sleep screen**
handling). It is the **only** Steroids "definitions" file for apps and
enhancements; see §10 for the upstream-merge counterpart.

---

## 1. Overview

CPR-vCodex Steroids is a fork of CPR-vCodex for the **Xteink X4** e-reader
(tested also on X3 with DS3231 RTC). Design goals: stable reading first, then
careful improvements — a full e-book library, cyber-style carousel panels,
dual-mode e-ink screensaver, contextual book menus, reading pace tracking,
bookmarks & clippings, reading statistics, flashcards, dictionary, and a set of
quality-of-life additions. Built for the ESP32-C3 (~380 KB usable RAM, no PSRAM).

Bread and butter rule: **never sacrifice reading stability for feature size**;
new features must respect RAM/heap/e-ink-refresh budgets.

---

## 2. App Catalog

All Steroids on-device apps, with their purpose and key source files. Each lives
in `src/activities/apps/` unless noted. Icons and ordering are registered in
`src/util/ShortcutRegistry.h` and must be mapped in **every theme** (see the
icon/theme checklist at the end of this file).

| App | Activity | Purpose |
|---|---|---|
| **Library** | `LibraryActivity` | Grid-based e-book library (EPUB/XTC/TXT/MD), sort/filter/search, cover generation, collections/series. Full technical detail in [§6](#6-library-module-detail). |
| **Wikipedia** | `WikipediaActivity` + `WikiTxtReaderActivity` | Download/read Wikipedia articles. Search, summary preview (in-app), full-article reading via a dedicated reader. Each downloaded/converted article is cached in a per-article folder (`wiki_<hash>` with `article.md` + `title.txt` + `index.bin` + `progress.bin`) for offline reopen. Detail in [§5](#5-wikipedia-app). |
| **Quick Cards** | `QuickCardsActivity` | Image, QR code, and barcode viewer. Browses `/cards/` on SD. Renders BMP/JPEG/PNG images with auto-scaling, QR codes with structured field parsing (Wi‑Fi, vCard, MeCard, geo, email, phone, SMS, OTP, calendar event, URL), and Code‑128 barcodes. Cyberpunk panel file list. Fullscreen mode. Detail in [§19](#19-quick-cards-app). |
| **Screensaver** | `ScreenSaverActivity`, `ScreenSaverDirActivity`, `ScreenSaverPreviewActivity` | Dual-mode e-ink screensaver (general + in-book), PNG compositing, folder picker. Detail in [§4](#4-screensaver--sleep--deepsleep). |
| **Sleep** | `SleepAppActivity`, `SleepPreviewActivity` | Sleep screen, image rotation on short power press, deep-sleep handling. Detail in [§4](#4-screensaver--sleep--deepsleep). |
| **Screen Clean** | `ScreenCleanActivity` | Anti-burn-in screen clean helper. |
| **Reading Stats** | `ReadingStatsActivity`, `ReadingStatsDetailActivity`, `ReadingStatsExtendedActivity` | Reading statistics, per-book detail, extended stats. |
| **Reading Heatmap** | `ReadingHeatmapActivity` | Calendar heatmap of reading time. |
| **Reading Profile** | `ReadingProfileActivity` | Reading profile / pace settings. |
| **Achievements** | `AchievementsActivity` | Achievements browser. |
| **Flashcards** | `FlashcardsAppActivity`, `FlashcardBrowserActivity`, `FlashcardReviewActivity`, `FlashcardDeckStatsActivity`, `FlashcardSessionSummaryActivity`, `FlashcardSettingsActivity`, `FlashcardStatsActivity`, `FlashcardRecentsActivity` | Spaced-repetition flashcards with decks and stats. |
| **Dictionary** | `DictionaryActivity` | Dictionary lookup. |
| **Bookmarks** | `BookmarksAppActivity` | Cross-book bookmark browser. Detail in [§7](#7-bookmarks--clippings-detail). |
| **Clippings** | `ClippingsAppActivity` | Cross-book highlight/clipping browser. Detail in [§7](#7-bookmarks--clippings-detail). |
| **Favorites** | `FavoritesAppActivity`, `FavoritesBrowserActivity`, `FavoritesOrderActivity` | Favorite books list, ordering. |
| **IfFound** | `IfFoundActivity` | Send to / find device. |
| **Sync Day** | `SyncDayActivity`, `ManualDateActivity` | Daily goal sync / manual reading date. |
| **Reading date selection** | `ReadingDateSelectionActivity`, `ReadingDayDetailActivity`, `BookReadingAdjustmentActivity`, `BookStatsActionsActivity` | Manual reading-time corrections and per-day detail. |
| **Apps hub** | `AppsActivity` | Grid of all installed apps. |
| **Lua Plugin Browser** | `PluginBrowserActivity` | Scans `/custom/*.lua` on the SD card, parses `-- NAME:`, `-- DESC:`, `-- ICON:`, `-- RESTART:` headers, and lists available plugins. When the user confirms a plugin, it either triggers a silent restart (default, `-- RESTART: yes`) or launches in-process with no reboot (`-- RESTART: no`, pushed onto the activity stack so the browser is restored on exit). |
| **Lua Plugins** | `LuaPluginActivity` | Sandboxed Lua 5.4.7 interpreter running a single `.lua` script from `/custom/`. A 64 KB heap cap is enforced by a custom allocator; an instruction-count hook aborts runaway loops (100 000 limit per callback). Standard Lua libs (base, string, table, utf8, debug) plus a custom `lcd.*`, `fs.*`, `input.*`, `sys.*`, `plugin_str.*` API surface are registered. `init()` runs at launch; `onKey()` is dispatched every 10 ms loop frame; Back always exits. Errors are logged to serial with a line-numbered stack traceback; `sys.log()`/`plugin.log()` output is tagged `[PLUGIN:<name>]`. On exit, the VM is shut down and either the device silently restarts back to the Plugin Browser/Apps/Home, or (for `-- RESTART: no`) pops back in-process without any reboot. Full reference in `[STEROIDS-LUA.md](STEROIDS-LUA.md)`. |

---

## 3. App Registration, Icons & Persistence (Full Guide)

When an app appears in Home or the Apps grid, the data flows through:

```
ShortcutRegistry (ShortcutDefinition)
        |            +-- UIIcon enum value (BaseTheme.h)
        |            +-- location/order/visible ptr (CrossPointSettings.h)
        v
Home / ShortcutOrderActivity
        |
        v
Theme::drawIcon / renderer.drawIcon(...)
        |
        v
Theme iconForName(UIIcon icon, [size])  ->  const uint8_t* bitmap  (or nullptr)
        |
        v
renderer.drawIcon(bitmap, x, y, w, h)
```

So making an app visible requires **exactly 5 pieces**:

1. **Sprite bitmap** header in `src/components/icons/*.h`
   (32px `<Name>Icon[]`, and 24px `<Name>24Icon[]` when a theme has a 24px block).
2. A value in the **`UIIcon` enum** (`src/components/themes/BaseTheme.h`).
3. A **`ShortcutDefinition`** row in
   `src/util/ShortcutRegistry.h::getShortcutDefinitions()`.
4. A **`case UIIcon::<App>`** in `iconForName()` of **every** active theme.
5. **JSON persistence** of the app's 3 settings in `src/JsonSettingsIO.cpp`
   (load + save), otherwise order/visibility reset at every boot.

### 3.1 Adding a new icon (sprite bitmap)

1. Convert the PNG into a 1-bpp bitmap array. Two sizes are conventional:
   - **32×32** → `components/icons/<name>icon.h`, symbol `<Name>Icon[]`
   - **24×24** → `components/icons/<name>icon24.h`, symbol `<Name>24Icon[]`
   - Existing example: `wikipediaicon.h` (`WikipediaIcon`, 32) and
     `wikipediaicon24.h` (`Wikipedia24Icon`, 24).

   ```cpp
   #pragma once
   #include <cstdint>
   // size: 32x32
   static const uint8_t WikipediaIcon[] = { ... };
   ```

> Note: app icons in Home / grid are typically 32px. Older themes (Classic,
> `TextInd`/`ScreenSaver`/`Pageview`) may use 24px; check the destination theme.

### 3.2 Registering the app in ShortcutRegistry

All registration lives in **`src/util/ShortcutRegistry.h`**:

```cpp
enum class ShortcutId { ..., Wikipedia, };  // add to the enum if new

ShortcutDefinition{ShortcutId::Wikipedia, StrId::STR_WIKIPEDIA, StrId::STR_WIKIPEDIA_APP_DESC,
                   UIIcon::Wikipedia,
                   &CrossPointSettings::wikipediaShortcut,        // location
                   &CrossPointSettings::wikipediaShortcutOrder,   // order
                   &CrossPointSettings::wikipediaShortcutVisible} // visible
```

The 3 pointers must reference `CrossPointSettings` fields (see §3.4). The array
count is automatic (`std::array`); `.size() + 1` is the order ceiling. Keep
`ShortcutId` and the entry count coherent.

### 3.3 Mapping the icon in EVERY theme

The most common cause of an invisible icon is a missing `case` in a theme's
`switch` on `UIIcon` (default returns `nullptr`). Every theme with its own
`iconForName()` must include the case:

| File | Function | Sizes |
|------|----------|-------|
| `src/components/themes/lyra/LyraTheme.cpp` | `iconForName(UIIcon)`, 2 blocks (24 and 32) | 24 + 32 |
| `src/components/themes/lyra/LyraCarouselTheme.cpp` | `iconForName(UIIcon, int size)` | 24 + 32 |
| `src/components/themes/lyra/LyraMarcoand75Theme.cpp` | `iconForName(UIIcon)` | 32 (maps all apps) |
| `src/components/themes/lyra/LyraCustomTheme.cpp` | (inherits) | — |

Add, per private `iconForName` block, plus the matching `#include`:
```cpp
case UIIcon::Wikipedia: return WikipediaIcon;    // 32px block
// and, for a 24px block:
case UIIcon::Wikipedia: return Wikipedia24Icon;  // 24px block
```

> Historical reference: commits `47a18ae…` and `51862b2…` fixed many invisible
> icons (25+ missing in Carousel theme; `UIIcon::File -> ClipIcon32` missing in
> the 32px block). The same pattern fixed Wikipedia in `LyraMarcoand75Theme`.

### 3.4 Persistence of order / visibility / location

For order, visibility and location (Home vs Apps) to survive reboots, the app's
3 settings must be serialized in `src/JsonSettingsIO.cpp`. Three fields in
`CrossPointSettings.h`:
```cpp
uint8_t wikipediaShortcut = SHORTCUT_APPS;  // location
uint8_t wikipediaShortcutOrder = 22;        // order (default: last)
uint8_t wikipediaShortcutVisible = 1;       // visible
```

And 3 lines in **both** load points and the save point of `JsonSettingsIO.cpp`:

Load (repeat in **every** load function):
```cpp
s.wikipediaShortcut       = clamp(doc["wikipediaShortcut"]       | s.wikipediaShortcut,       shortcutLocationCount, s.wikipediaShortcut);
s.wikipediaShortcutOrder  = clamp(doc["wikipediaShortcutOrder"]  | s.wikipediaShortcutOrder,  shortcutOrderCount,    s.wikipediaShortcutOrder);
s.wikipediaShortcutVisible= clamp(doc["wikipediaShortcutVisible"]| s.wikipediaShortcutVisible, static_cast<uint8_t>(2), s.wikipediaShortcutVisible);
```
Save:
```cpp
doc["wikipediaShortcut"]        = s.wikipediaShortcut;
doc["wikipediaShortcutOrder"]   = s.wikipediaShortcutOrder;
doc["wikipediaShortcutVisible"] = s.wikipediaShortcutVisible;
```

Clamp bounds:
- `shortcutLocationCount = CrossPointSettings::SHORTCUT_LOCATION_COUNT` (0=Home, 1=Apps).
- `shortcutOrderCount = getShortcutDefinitions().size() + 1` (22 with 21 defs).

> A default order equal to `shortcutOrderCount` (e.g. 22) is the **highest** → the
> app always sorts last until the user reorders it. Give a lower value to start in
> the middle.

### 3.5 Verification checklist

When adding an app or icon, check ALL of the following:

- [ ] Sprite bitmap present in `src/components/icons/`.
- [ ] Value added to the `UIIcon` enum in `BaseTheme.h` (if a new icon).
- [ ] `ShortcutDefinition` present in `ShortcutRegistry.h::getShortcutDefinitions()`.
- [ ] 3 fields (`...Shortcut`, `...ShortcutOrder`, `...ShortcutVisible`) in `CrossPointSettings.h`.
- [ ] `case` in `LyraTheme.cpp` (24 and 32 blocks).
- [ ] `case` in `LyraCarouselTheme.cpp` (24 and 32 blocks).
- [ ] `case` in `LyraMarcoand75Theme.cpp` (and any other theme with a private `iconForName`).
- [ ] `#include` of the bitmap header in every modified theme.
- [ ] 3 load lines in `JsonSettingsIO.cpp` (in ALL load points).
- [ ] 3 save lines in `JsonSettingsIO.cpp`.
- [ ] `python -X utf8 -m platformio run -e default -j 16` compiles.
- [ ] Device test: open the app from Home and from the Apps grid in every theme,
      change order/visibility/location, reboot, and verify they persist.

### 3.6 Involved files

| File | Role |
|------|------|
| `src/components/icons/*.h` | Icon bitmap data |
| `src/components/themes/BaseTheme.h` | `enum UIIcon` |
| `src/util/ShortcutRegistry.h` | `ShortcutDefinition` + order/visibility helpers |
| `src/CrossPointSettings.h` | `...Shortcut`, `...ShortcutOrder`, `...ShortcutVisible` fields |
| `src/JsonSettingsIO.cpp` | JSON serialization (load + save) |
| `src/components/themes/lyra/LyraTheme.cpp` | Icon mapping (24 + 32) |
| `src/components/themes/lyra/LyraCarouselTheme.cpp` | Icon mapping (24 + 32) |
| `src/components/themes/lyra/LyraMarcoand75Theme.cpp` | Icon mapping (32, apps) |
| `src/components/themes/lyra/LyraCustomTheme.cpp` | Icon mapping (if present) |

---

## 4. Screensaver, Sleep & Deep-Sleep

A comprehensive **dual-mode** e-ink screensaver system built on transparent PNG
compositing, with separate configuration for general use and in-book reading.

### 4.1 General screensaver (`Settings > Screensaver`)
- **Folder selector** with preview (`ScreenSaverDirActivity` + `ScreenSaverPreviewActivity`)
  — pick a photo folder from a browsable list instead of typing a path.
- **Sequential / shuffle** order.
- **Automatic sleep bypass** — screensaver keeps the display refreshed without
  triggering full sleep cycles.
- **Battery-protection deep sleep** — after a configurable timeout.
- **Wake-on-any-button** or a **single custom button**.
- **Sleep screen rotation** — a short power-button press rotates the displayed
  sleep image without a full wake.
- 4-gray-level BMP cycling respects refresh settings.

### 4.2 Reader screensaver (in-book)
- **Separate reader screensaver** folder + order (`Settings > Screensaver (reading)`),
  used only when triggered from inside a book.
- **Launch from reader menu** — during reading press Select → "Screensaver"; any
  button exits back to the exact page.
- **Replace sleep with screensaver** — when enabled, a **long-press of the power
  button while reading launches the screensaver instead of deep sleep**; the reader
  activity stays on the activity stack for instant return.
- Battery-minimum checks respected: below the threshold, normal deep sleep is used.
- Outside reading, the power button behaves as normal sleep.

### 4.4 Battery Safety Under High-Power Draw (#59)

Under WiFi-heavy activities (File Transfer, OTA, web server), the ESP32 draws
significantly more current than during normal reading. A battery voltage that
reads fine at idle can sag below the brown-out threshold mid-operation, causing
an abrupt power-off with the e-ink display frozen on the last frame.

**Added safety check in `main.cpp` `loop()`:** Every 5 seconds (when no auto-sleep
is pending), the loop checks `powerManager.getBatteryPercentage()`. If the reading
is below 5% AND the device is not already entering deep sleep, it:

1. Renders a "Battery Empty / Please charge" screen (centered text) and flushes
   it to the e-ink display with a half-refresh.
2. Holds the screen for 2 seconds so the user sees it.
3. Calls `enterDeepSleep()` to gracefully shut down before power is lost.

This prevents the frozen-display scenario where the device dies mid-WiFi-transfer
without rendering a final screen.

**Files:** `src/main.cpp` (battery check + shutdown screen),
`src/activities/ActivityManager.h/.cpp` (new `isWifiActivity()` method),
`src/activities/Activity.h` (new `isWifiActivity()` virtual),
`src/activities/network/CrossPointWebServerActivity.h` (override `isWifiActivity()`),
`lib/I18n/I18nKeys.h`, `lib/I18n/translations/english.yaml`,
`lib/I18n/translations/italian.yaml`.

---

### 4.3 Deep-sleep / power button handling (main.cpp)
Steroids **differs from upstream** in how the power button is processed in
`src/main.cpp`:

- **Upstream**: unconditionally calls `enterDeepSleep()` on every power-button
  press edge, regardless of the `shortPwrBtn` setting — so only `SLEEP` mode ever
  works and all other values (`IGNORE`, `PAGE_TURN`, `FORCE_REFRESH`,
  `TOGGLE_STATUS_BAR`) are unreachable.
- **Steroids**: the power button is a short/long-press state machine
  (`powerBtnDownMs`, `powerBtnInScreensaver`):
  - **Short press** (< `getPowerButtonDuration()`, 400 ms for non-SLEEP modes):
    triggers the configured `shortPwrBtn` action.
  - **Long press** (≥ threshold): always deep-sleeps, **or starts the replacement
    screensaver** when one is configured for the current reader activity and the
    battery condition passes (`canStartReplacementScreenSaver()`).
  - **Active screensaver**: the long-press check is skipped so the screensaver can
    process the wake button without interference; the release edge is suppressed so
    a brief press used to dismiss the screensaver does not accidentally fire the
    configured `shortPwrBtn` action.

**Files:** `src/main.cpp`, `src/activities/apps/ScreenSaverActivity.cpp`,
`PngSleepRenderer`, `ReaderUtils::canStartReplacementScreenSaver()`.

### 4.4 Transparent PNG compositing (unified)
- **Snapshot-based background** — before any sleep/screensaver image is drawn, the
  current framebuffer is saved to SD (`/.crosspoint/screensaver-caller.tmp` for
  screensaver, `/.crosspoint/last_reader_page.bin` for sleep).
- On every image change the snapshot is restored first, so transparent areas always
  show the original calling content.
- **File-based framebuffer cache** (replaces the in-memory 48 KB vector) removes
  persistent heap pressure.
- **Heap-friendly PNG decoder** — SD font caches cleared before decoding, maximizing
  contiguous free space for the ~44 KB decoder. `PngSleepRenderer::releaseDecoder()`
  returns the heap on exit.

### 4.5 Exit & transitions
- No forced full refresh on screensaver exit — the underlying activity re-renders
  with its natural refresh mode.
- Immediate render notification on exit to avoid blank gaps.

### 4.6 Screensaver reading-stats fix
- Screensaver time is **no longer counted toward reading statistics** — the reading
  session timer is correctly reset every time the screensaver is dismissed.

---

## 5. Wikipedia App

Downloads and reads Wikipedia articles on-device. Search, cache, summary preview,
and full-article reading are decoupled:

- **`WikipediaActivity`** — search (opensearch), search history, cached-article list,
  summary preview (`renderArticle()`, plain text, in-app), download + wikitext→markdown
  conversion, and launching the reader.
- **`WikiTxtReaderActivity`** — dedicated reader for the cached per-article folders.

> **Cache layout (current): per-article folder.** Each downloaded/converted article is
> stored in `/.crosspoint/wikipedia-cache/wiki_<FNVhash>/` and contains:
> `article.md` (the markdown body), `title.txt` (the real display title, so the
> `CACHED_PAGES` list can show and reopen by title even though the folder name is an
> opaque hash), `index.bin` (page-index cache), `progress.bin` (last read page).
> The old flat format (`.wiki` files) is a legacy fallback; legacy files are removed
> when a newer per-folder copy is cached.

### 5.1 Download flow (`fetchFullArticle`)
1. Calls `https://it.wikipedia.org/w/api.php?action=parse&page=<TITLE>&prop=wikitext&format=json`
   (wikitext JSON, not the mobile-html REST API).
2. Streams the JSON response to `wiki_<hash>/raw.json` on SD (no full-RAM copy).
   **HTTP/TLS lifetime matters:** `HTTPClient` + the `NetworkClientSecure` (TLS) are
   held in a dedicated scope that destroys them **in the right order (http first, then
   client)** **before** conversion/restore; calling `httpClient.reset()` manually was a
   **double-free** (`HTTPClient` keeps an internal ref to the TLS client) → abort at
   `multi_heap_free`.
3. **`WikitextToMarkdown`** (`src/util/WikitextToMarkdown.{h,cpp}`) streams the JSON,
   scans the `"wikitext"` → `"*"` field, decodes JSON escapes on the fly, and writes
   `wiki_<hash>/article.md` as **markdown**.
   - wikitext `'''...'''`/`''...''` → markdown `**...**`/`*...*`; `==H==` → `# H`;
     lists `*`/`#`; links `[[X|Y]]` → display text; `{{templates}}`, `<ref>`, HTML
     comments and multi-line infobox templates stripped.
   - `HtmlToTxt` is **kept but unused** in the Wikipedia flow.
4. Writes `wiki_<hash>/title.txt` with the real title, then `loadCachedPages()`.
5. **No reading-stats reload here.** After this heavy streaming the heap is low and
   fragmented (`~46 KB free / ~20 KB maxAlloc`): reloading all book stats caused
   `abort()` (illegal instruction). `restoreAfterNetwork(..., reloadReadingStats=false)`
   skips it because `WikiTxtReaderActivity` does not use reading stats; they stay
   persisted on SD and load lazily via `ensureLoaded()` when actually needed.

### 5.2 Reading flow
- **Summary preview** (`/api/rest_v1/page/summary/...`) uses the in-app Wikipedia
  rendering (`renderArticle()`).
- **Confirm on summary** opens the full article **directly from cache when available**
  (no network), else falls back to `fetchFullArticle()`.
- **Cached article reopen** (`State::CACHED_PAGES`) opens by the **stored title** via
  `loadCachedArticle()` → `openArticleForReading()` (no re-connect). Large articles are
  read straight from `article.md` on SD (`g_articleFilePath`); small ones into RAM.
- **Long-press on a cached page** asks for confirmation (`ConfirmationActivity`,
  `STR_DELETE_CACHED_PAGE`) and then deletes the whole `wiki_<hash>` folder with
  `Storage.removeDir()`.
- **Home cleanup:** when the reader closes, `searchInput`/`currentQuery`/`errorMessage`
  are cleared so the "Search Wikipedia" button returns to its hint (previously it kept
  showing the last-read title).

### 5.3 WikiTxtReaderActivity (dedicated reader)
Uses the **same reading/rendering system as `TxtReaderActivity`** but without the
book-reader side effects:
- **Markdown span parsing**: `**bold**`, `*italic*`, `#` headings, `-`/ordered lists,
  `>` blockquotes.
- **Page index** built in RAM (`buildPageIndex`, with `pageOffsets.reserve()` to curb
  heap fragmentation) + **per-article `index.bin`** cache with settings-validation
  (font / margin / lines / viewport).
- **Chunked file reading** (`loadPageAtOffset`) with span-aware wrapping and SD-font
  priming per chunk.
- **Two-pass prewarm rendering** (`renderPage`) + status bar with progress
  (`Pag. N/M`).
- **Reading progress persisted** in `wiki_<hash>/progress.bin` (page + byte offset) and
  restored on `onEnter`; saves on each render and on exit.
- **`getScreenshotInfo()` / `isReaderActivity()`** so screenshots and reader-related
  framework hooks work for the wiki reader too.
- `article.md` content is always treated as markdown.
- **No** reading stats, achievements, recent books, completed-book mover, or
  orientation handling (RTL is supported at line level via `BidiUtils`).

**Files:** `src/activities/apps/WikipediaActivity.{cpp,h}`,
`src/activities/reader/WikiTxtReaderActivity.{cpp,h}`, `src/util/WikitextToMarkdown.{cpp,h}`,
`src/util/MarkdownReader.{cpp,h}`.
**Plumbing:** `lib/hal/HalStorage.{cpp,h}` + `freeink-sdk`
`SDCardManager::listFiles()` gained an `includeDirectories` option (default `false`)
so the cache can enumerate `wiki_*` folders; existing callers unchanged.

---

## 6. Library Module (Detail)

The complete library subsystem (rewritten from scratch July 2026): on-device book
collection, grid browsing with sort/filter/search, cover generation, and
collections/series navigation.

### 6.1 Storage layout
```
/.crosspoint/LIBRARY/
├── library.dat        (256 B/record × N books)
├── scan_state.dat     (16 B/record × N books)
├── idx_title.bin      (28 B/record, sorted by title)
├── idx_author.bin     (28 B/record, sorted by author)
├── idx_collections.bin (88 B/record, unique collections)
├── series.dat         (88 B/record, per-book series metadata)
└── tmp/chunk_*.tmp    (temporary merge-sort chunks, deleted after build)
```

### 6.2 Key principle: NO full dataset in RAM
None of these files is ever loaded entirely into memory. The library operates with a
fixed-RAM page cache (`16 BookRef` ≈ ~4 KB) populated on-demand via indexed queries.
Scales to 10,000+ books with flat RAM.

### 6.3 Pipeline
- **Scan** (`LibraryIndex::scan()`) — streaming callback DFS, no path vector in RAM;
  incremental with `std::lower_bound` binary search, skips the counting pass when no
  progress bar, `buildIndices()` only when `added>0 || removed>0`.
- **Index build** (`buildIndices()`) — external k-way merge-sort; descending by walking
  the same file backwards.
- **Collections index** (`buildCollectionsIndex()`) — sorts `series.dat` by collection +
  series index, builds `idx_collections.bin` compact directory.
- **Page query** (`queryPage()`) — indexed O(log N) walk, or O(N) full-text search for
  RECENT/PROGRESS/filters.

### 6.4 UI
- Grid layouts 2×2, 3×3, 4×4 (default), controlled by `SETTINGS.libraryLayout`.
- Partial render optimization — in-page navigation only redraws selection borders,
  sub-second on e-ink.
- Cover generation on-demand per page; every success triggers a render (progressive).
- Collections browse (series) from Calibre / EPUB3 metadata.
- Persistent filter/sort: `libraryFilter`, `librarySort`, `librarySearchText`, root dir.
- Settings → Rebuild Library calls `LibraryIndex::invalidate()` (deletes all library files).

### 6.5 RAM budget ~11 KB total
| Component | RAM |
|---|---|
| `pageCache_[16]` | ~4 KB |
| Sort chunk buffer | ~4 KB |
| Stack locals | ~2 KB |
| I/O buffers | ~1 KB |

**Files:** `src/components/LibraryIndex.{cpp,h}`, `src/components/LibraryCache.{cpp,h}`,
`src/components/EpubParser.{cpp,h}`, `src/activities/apps/LibraryActivity.{cpp,h}`,
`src/CrossPointSettings.h`.

---

## 7. Bookmarks & Clippings (Detail)

`STEROIDS-CLIPPINGS-BOOKMARKS.md` content. Both subsystems use **absolute word indices**
for layout-independent positioning — the same word stays highlighted at the same
screen position regardless of font size, margins, or orientation.

### 7.1 Storage
| Component | File Format | Location |
|---|---|---|
| Bookmarks | Binary (v4) | `/.crosspoint/bookdata/{bookId}/bookmarks.bin` |
| Clippings | Binary (v2) | `/.crosspoint/clippings/epub_{hash}.bin` |

Stable book identity hashing via `BookIdentity` (same book → same path regardless of
SD path changes).

### 7.2 Key structures
- **Bookmark** (32+ bytes): `spineIndex` (u16), `pageNumber` (u16, legacy), `snippet`
  (~80 chars, v3 compat), `absoluteWordStart` (u32, v4).
- **Clipping** (~560 bytes): `spineIndex`, `startPage/endPage`, `startWordIndex`,
  `endWordIndex`, `wordCount`, `absoluteWordStart` (u32, v2), `timestamp`,
  `chapterTitle` (char[48]), `selectedText` (≤ ~512 bytes).

### 7.3 Layout-independent positioning
`Section::buildCumulativeWordCounts()` computes `cumulativeWordCounts[page]` = total
words from chapter start to the beginning of `page`. Bookmarks store
`absoluteWordStart = cumulative[page]`. On layout change, the render function finds
`pageStart + wordIndex == absoluteWordStart` to re-place the highlight — no data lost.

### 7.4 Apps
- **BookmarksAppActivity** — cross-book bookmark browser (loads recent books via
  `RecentBooksStore`, bookmark per book from `bookdata/{bookId}/`, jumps to exact word).
- **ClippingsAppActivity** — cross-book clippings browser (`/.crosspoint/clippings/`),
  delete individual or all, i18n `STR_CLIPPINGS`, `STR_NO_CLIPPINGS`,
  `STR_DELETE_ALL_CLIPPINGS`.

### 7.5 Reader integration
- In-reader selection UI: cursor word **inverted** (black bg / white text);
  selected words light-gray with readable text; anti-aliasing compatible.
- Front/side long-press configured via `frontLongPressBehavior` /
  `longPressButtonBehavior` (bookmark / clipping / chapter skip / orientation / font size).

### 7.6 Performance
- `buildCumulativeWordCounts()` loads each page to count words; ~2–5 s on first open
  of a long section. Cumulative array ≈ 2 bytes/page in RAM.

**Files:** `src/activities/reader/BookmarkStore.h`, `ClippingStore.h`,
`EpubReaderActivity.cpp`, `src/activities/apps/BookmarksAppActivity.{cpp,h}`,
`ClippingsAppActivity.{cpp,h}`, `src/activities/reader/ClippingsActivity.{cpp,h}`,
`lib/Epub/Epub/Section.cpp`, `src/util/BookIdentity.{cpp,h}`.

---

## 8. Other Enhancements & Refactoring

- **Status bar time-left display** — now 5 modes (Hide / Chapter / Book / Session Duration / Today Total). Session Duration shows accumulated time since the current reading session began (resets on book open/close); Today Total shows the total reading time for the current day (includes the current session via the existing `getTodayReadingMs()` summary fast path). The label "Time Left" (EN) / "Tempo rimanente" (IT) was renamed to "Display Time" (EN) / "Tempo visualizzato" (IT) since it now also shows elapsed time.
- **Reading statistics & heatmap** (`ReadingStatsActivity`, `ReadingHeatmapActivity`)
  — pace fields preserved across upstream drops (`avgSecondsPerForwardPage`,
  `paceSampleCount`, `recordForwardPageRead`, mark-as-unread).
- **Flashcards** — spaced-repetition decks, review sessions, per-deck stats,
  recents, settings.
- **Dictionary** — on-device dictionary lookup.
- **Custom app icons** — hand-crafted monochrome icons (Library, Sleep, Screen Clean,
  Reading Heatmap) visible in **all** themes.
- **Boot & sleep logo** — custom "Steroids" 350×96 boot/sleep logo; `scripts/convert_logo.py`.
- **STRING-type setting support** in `SettingsActivity` (previously web-only);
  enables e.g. "Library root directory" on-device.
- **List-activity helpers** — shared `ListInputMapper`, `ListLayout`, `ListRenderHelper`
  (≈1,350 lines de-duplicated).
- **Book-Store deduplication** — `BookStoreUtils.h` shared by Favorites/Recent.
- **Performance & memory** — inventory caching, system-dir exclusion, zero-size
  thumbnail cleanup, font-decompressor lazy init (saves ~48 KB), `freeUnusedRenderMemory()`,
  library background memory release, lower cover-gen heap guards, full CPU during cover gen.
- **Home reading-stats summary fast path** — a small derived `/.crosspoint/summary.json`
  (global panel values + per-book home badges) lets the Home render the Lyra/Marcoand75
  dashboard, carousel progress badges, read ribbon and long-press menu **without loading
  the ~41 KB full `reading_stats.json` store into RAM at boot**. Written on every
  `markDirty()`/`saveToFile()`, read lazily at boot (`preloadHomeSummary`), with an
  upgrade path that generates it once from the existing store. Detail in [§22](#22-home-reading-stats-summary-json-fast-path).
- **OPDS download cancellation** — the OPDS book browser now supports cancelling an
  in-progress download by pressing the Back button. A "Press Back to cancel download"
  hint is shown on the progress screen. On cancellation the network stream is aborted,
  the partial file is deleted, and the user returns cleanly to the catalog browser
  without freezing the UI. Input is polled during the download progress callback
  (matching the FontDownloadActivity cancellation pattern). Issue [#91](https://github.com/marcoand75/cpr-vcodex-steroids/issues/91).
- **Power button / deep-sleep state machine** (see §4.3).

- **Select Long Press configuration** (`CrossPointSettings::selectLongPressBehavior`) — expanded from 3 to 14 `BUTTON_ACTION` options. Default is Toggle Bookmark; also supports Add/View Clippings, Lookup Word, Dictionary, Chapter Skip, Orientation, Font Size, Dark Mode, Full Refresh, Quick Settings, Reading Timer (pause/resume tracking), and Off. A `|| PAUSED` status bar indicator appears when reading timer is paused. TXT/XTC readers restrict to `READING_TIME` and `OFF` only.
- **Status bar clock hidden on X4** (device without DS3231 RTC) — Status Bar
  customization menu filters out Clock/Clock Format/Sync Clock Now on X4 hardware.
- **Settings dividers** — thin horizontal separator lines group related settings within
  each tab (Display, Reader, Controls, System, Apps), matching the pattern used by the
  Apps tab.
- **Clear reading cache confirmation fix** — multi-line wrapped text using
  `GfxRenderer::wrappedText()` instead of fixed single-line layout, preventing text
  overflow past screen margins.
- **Multi-device (X3/X4) support** — CrossInk v1.5.0-rc-3 screen/model integration:
  - `freeink-sdk` replacing `open-x4-sdk` with `BoardConfig`, UC8279 panel detection,
    SPI mutex (`HalSpiBus`), touch/gesture stubs, X3/X4 runtime device selection.
  - Build flags `-DFREEINK_DEVICE_X4=1 -DFREEINK_DEVICE_X3=1` on all environments.
- **QR field parser** (`QrCardParser.h`) — structured extraction for 10 formats:
  Wi‑Fi (SSID/Password/Security), vCard/MeCard (name/phone/email/URL/address),
  Geo (lat/lon), Email (mailto), Phone (tel), SMS (smsto), OTP (otpauth://),
  Calendar (BEGIN:VEVENT), URL (http/https). Sanitized to printable ASCII for e-ink fonts.
- **SdCardFont fragmentation-resistant storage** — ported from upstream 1.5.0.20:
  4 KiB chunked bitmap storage (`miniBitmapChunks[24]`), TextGetter prewarm callback,
  CJK fallback font resolution via `hasCodepoint()` + `coverageHandler`, FrameBufferLoan
   for section builds, FontDecompressor raw-buffer refactor. Built: RAM 16.0%,
   Flash 98.5%, 0 warnings. See §23.
- **Lua Plugin System** — a sandboxed Lua 5.4.7 plugin runtime for user-authored
  apps. Plugins are `.lua` files placed in `/custom/` on the SD card; the
  `PluginBrowserActivity` scans and lists them by parsing `-- NAME:`, `-- DESC:`,
  `-- ICON:` header comments. A `LuaPluginActivity` loads each script into a
  64 KB-capped Lua VM with a custom `heap_caps_realloc` allocator, a 100 000-instruction
  per-callback safety hook, and a custom API surface (`lcd.*`, `fs.*`,
  `input.*`, `sys.*`, `plugin_str.*`). Standard Lua libraries (base, string, table,
  utf8, debug) are available. `init()` is called once at launch; `onKey()` is
  dispatched every loop frame (~10 ms) for continuous updates (games, animations).
  File I/O is sandboxed to `/custom/<plugin_name>_data/` with path-traversal
  protection. On exit, the VM is shut down and a silent restart routes back to
  the Plugin Browser (or Apps/Home, depending on the caller). Plugin source
  files must be ≤ 40 KB; scripts exceeding the VM's 64 KB allocation cap will
  error out. See `STEROIDS-LUA.md` for the complete API reference.

## 8A. Grayscale Image Rendering (BMP, covers, screensaver/sleep)

Steroids-specific rework of the image → 2-bit (4-level grayscale) pipeline. This
is **different from upstream** and must be preserved on any merge (see
`STEROIDS-ALIGN-TO-UPSTREAM.md`). Target hardware: **Xteink X4 / X3 e-ink**,
ESP32-C3, monochrome panels with a 2-bit (4-gray-level) drive.

### Shared configuration — `lib/GfxRenderer/DitheringConfig.h` (new)
Single source of truth for every grayscale path, replacing per-file local
constants:
- `USE_ATKINSON = true`, `USE_FLOYD_STEINBERG = false` (dither method).
- `GRAY_LEVEL_0..3 = 0 / 85 / 170 / 255` (the 4 reconstructed panel levels).
- `GAMMA_VALUE = 1.5f` (input-luminance gamma before dithering).
- `extern uint8_t gammaLUT[256]` + `initGammaLUT()` (8-bit → gamma-corrected).

Consumers now all share it: `Bitmap.cpp` (BMP reader), `JpegToBmpConverter.cpp`
(JPEG covers), `PngToBmpConverter.cpp` (PNG/cover), and via `adjustPixel` the
**screensaver/sleep BMP rendering**.

### Pipeline (per pixel), in the ditherers — `lib/GfxRenderer/BitmapHelpers.{h,cpp}`
1. **Gamma LUT** applied to the original input luminance before any error is
   added (`adjustPixel(gray)` → `gammaLUT[gray]`), correcting midtones on e-ink
   where the panel paints light tones too bright.
2. `int16_t accumulated = gammaGray + errorBuffer[offset]` — pre-clamp value.
3. **Clamp** to `[0,255]` only for quantization / reconstructable panel values.
4. `uint8_t q_idx = quantizeSimple(gray)` with **empiric thresholds 50 / 120 / 200**
   (vs upstream 43/128/213) tuned for the X4 panel.
5. `uint8_t reconstructed = unquantize(q_idx)` → `0/85/170/255`.
6. `int16_t error = accumulated - reconstructed` — computed on the **pre-clamp**
   value so clamped overflow is not silently lost.
7. **Diffuse error using pure integers** (no float/double in the hot loop):
   - Atkinson: `error >> 3` (÷8) to 6 neighbors.
   - Floyd-Steinberg (serpentine): `(error*7)>>4`, `(error*3)>>4`, `(error*5)>>4`,
     `(error)>>4` (7/16, 3/16, 5/16, 1/16).
8. **Horizontal error dropped at the last pixel of a row** (and right/R+1 in the
   serpentine left-neighbor direction guarded by `x > 0`) to avoid streak
   artifacts on the panel.

### Overflow protection & buffer safety
- All per-pixel math is `int16_t` (`accumulated`, `gray`, `reconstructed`,
  `error`, `diffused`) — far within the -32768..32767 range for X4-sized rows.
- Error buffers are **`int16_t *` allocated once** in each ditherer constructor:
  - `AtkinsonDitherer` / `Atkinson1BitDitherer`: `new int16_t[width + 4]()` × 3 rows.
  - `FloydSteinbergDitherer`: `new int16_t[width + 2]()` × 2 rows.
  (≈ `(width + pad) × 2 bytes`; for an 800 px screen ≈ 1.6 KB, reused across rows —
  never re-allocated per row.)
- **No negative index access**: buffers are read at `+2`/`+1` and written at
  `+1..+4` (base-offset convention). The "bottom-left" neighbor is `errorRow1[x+1]`,
  never `x-1`; when `x = 0` the index is `1`, never negative. All writes stay
  within `[width + 4]`. Audited: no `x-1`/`x-2` indexing in any of the three
  ditherers.
- No global ditherer object: each is `new`-allocated **once per image decode**
  (stored as an object member) and freed with it — no per-row allocation.

### Boot init
`main.cpp` calls `initGammaLUT()` once after `randomSeed(esp_random())`, building
the 256-entry LUT before any decode. `adjustPixel()` also hosts a **lazy-init
guard** so a forgotten boot call can never yield an all-black image.

### Visual outcome
Higher perceived contrast and correct midtones on BMP-based screensaver/sleep
images and covers: dark tones stay dark, midtones separate clearly into the
4 available panel levels, and highlights are not washed out — versus the flat,
pale result from the upstream disabled/no-adjustment pipeline.

---

## 8.5. Expanded Long-Press Button Actions (Superseded by §8.5.2)

> **Note:** Section 8.5 described an intermediate expansion that added 4 new
> assignable actions to the legacy `LONG_PRESS_BUTTON_BEHAVIOR` and
> `FRONT_LONG_PRESS_BEHAVIOR` enums (Dictionary, Dark Mode, Full Refresh, Quick
> Settings), expanding them from 6 to 10 values. This approach has been
> **superseded** by the §8.5.2 per-directional `BUTTON_ACTION` enum (14 values)
> architecture. The legacy enums and their 10-value expansion are now kept only
> for backward compatibility and migration; all new code uses `BUTTON_ACTION`
> with the per-directional fields described in §8.5.2.

The intermediate expansion added 4 new assignable actions to both side-button
and front-button long-press settings, expanding the legacy enum from 6 to 10
options each:

| New Value | Label (EN) | Label (IT) | Effect |
|---|---|---|---|
| `LONG_PRESS_DICTIONARY` / `FRONT_LONG_PRESS_DICTIONARY` | Dictionary | Dizionario | Left button → "Look up word" (word selection); Right button → open Dictionary app |
| `LONG_PRESS_DARK_MODE` / `FRONT_LONG_PRESS_DARK_MODE` | Toggle Dark Mode | Modo scuro | Toggles `SETTINGS.darkMode` and saves + refreshes |
| `LONG_PRESS_FULL_REFRESH` / `FRONT_LONG_PRESS_FULL_REFRESH` | Force Full Refresh | Refresh completo | `requestCurrentPageFullRefresh()` — full e-ink refresh |
| `LONG_PRESS_READER_SETTINGS` / `FRONT_LONG_PRESS_READER_SETTINGS` | Quick Settings | Impostazioni rapide | Opens the reader quick-settings overlay |

**Backward compatibility:** existing saved settings values (0–5) are unchanged.
Devices with old settings files retain their current mappings; new options only
appear when the user cycles through or sets a new value.

**Power button isolation:** these changes ONLY touch `longPressButtonBehavior` and
`frontLongPressBehavior` — the power button's `shortPwrBtn` state machine in
`main.cpp` (SLEEP / IGNORE / PAGE_TURN / FORCE_REFRESH / TOGGLE_STATUS_BAR) and
the screensaver/replacement-screensaver logic (`powerBtnDownMs`,
`powerBtnInScreensaver`, `canStartReplacementScreenSaver`) are completely untouched.

**Directional semantics for DICTIONARY action:** Left (prevTriggered) = "Look up
word" (launches word-selection overlay on the current page); Right (nextTriggered)
= open the full Dictionary app directly. This mirrors the pattern used by the
existing Bookmark/Clippings actions.

### 8.5.2. Per-Directional Long-Press Configuration

Replaces the single `longPressButtonBehavior` (side buttons) /
`frontLongPressBehavior` (front buttons) with independently configurable
per-button actions:

- **Long-press Up** — side button Up (default: Chapter Skip)
- **Long-press Down** — side button Down (default: Chapter Skip)
- **Long-press Left** — front button Left (default: Off)
- **Long-press Right** — front button Right (default: Off)

Each button now supports the full `BUTTON_ACTION` enum (14 actions):

| Action | Label (EN) | Label (IT) | Reader support |
|---|---|---|---|
| `BTN_ACTION_OFF` | Off | Off | All |
| `BTN_ACTION_ADD_CLIPPING` | Add Clipping | Aggiungi clipping | EPUB only |
| `BTN_ACTION_VIEW_CLIPPINGS` | Clipping Store | Store clipping | EPUB only |
| `BTN_ACTION_TOGGLE_BOOKMARK` | Toggle Bookmark | Attiva/disattiva segnalibro | EPUB only |
| `BTN_ACTION_VIEW_BOOKMARKS` | Bookmark Store | Store segnalibri | EPUB only |
| `BTN_ACTION_LOOKUP_WORD` | Lookup Word | Cerca parola | EPUB only |
| `BTN_ACTION_DICTIONARY` | Dictionary | Dizionario | EPUB only |
| `BTN_ACTION_CHAPTER_SKIP` | Chapter Skip | Salta capitolo | All |
| `BTN_ACTION_ORIENTATION` | Orientation | Orientamento | All |
| `BTN_ACTION_FONTSIZE` | Font Size | Dimensione font | EPUB/TXT |
| `BTN_ACTION_DARK_MODE` | Toggle Dark Mode | Modo scuro | All |
| `BTN_ACTION_FULL_REFRESH` | Force Full Refresh | Refresh completo | All |
| `BTN_ACTION_READER_SETTINGS` | Quick Settings | Impostazioni rapide | All |
| `BTN_ACTION_READING_TIME` | Reading Timer | Timer lettura | All |

**Short power button** is also expanded to 16 options (the original 5 plus all
`BUTTON_ACTION` values except `READING_TIME`), enabling actions like adding a
clipping or toggling a bookmark directly from the power button during reading.

**Select long-press** is expanded from 3 to 14 options, using the same
`BUTTON_ACTION` enum. TXT/XTC readers restrict this to `READING_TIME` and
`OFF` only (no bookmark/clipping/dictionary support).

**Backward compatibility:** legacy `longPressButtonBehavior` and
`frontLongPressBehavior` fields are migrated to per-directional settings on
first load. If a per-directional setting is `OFF` and the legacy field is
non-default, the legacy value is used as fallback for both Up+Down (side) or
Left+Right (front) buttons. The `selectLongPress` legacy enum is similarly
migrated to `selectLongPressBehavior`.

---

## 9.

## 9. Build & Merge Reference

- Build: `python -X utf8 -m platformio run -e default -j 16` (release: `-e gh_release`).
- Icon/theme checklist: see §3 of this file (self-contained — no separate file needed).
- **Merging upstream:** see `STEROIDS-ALIGN-TO-UPSTREAM.md`. When that file says
  "keep local", it means **never overwrite** the Steroids files listed there —
  including `src/JsonSettingsIO.cpp`, `src/CrossPointSettings.h`,
  `src/ReadingStatsStore.cpp`, the EPUB parser/renderer files, web server + HTML,
  i18n yaml, the LyraMarcoand75 theme, and the screensaver/sleep `main.cpp` logic.

### 10. Relationship Between the Two Steroids Definition Files

There are **exactly two** Steroids definition files. Keep it that way — do not
reintroduce standalone `STEROIDS-LIBRARY.md` or `STEROIDS-APP-ICON-THEME.md`:

| File | Role |
|------|------|
| **`STEROIDS-ADDICTIONS.md`** | All Steroids apps, screensaver/sleep/deep-sleep handling, and every enhancement (this file: app catalog §2, icon/theme guide §3, Wikipedia §5, library §6, bookmarks & clippings §7). |
| **`STEROIDS-ALIGN-TO-UPSTREAM.md`** | Instructions for merging a new upstream release into Steroids while preserving everything in this file. |

## 12. Steroids Settings Storage

Since 2026-08-04, Steroids-only settings are stored in a separate JSON file
(`/.crosspoint/settings-steroids.json`) rather than being mixed into the
upstream `/.crosspoint/settings.json`. This isolates the 43 Steroids-specific
fields from the ~107 upstream fields, making `JsonSettingsIO.cpp` save/load
functions byte-identical to upstream — zero merge conflicts on future upstream
releases.

The separation includes a **dedicated C++ file** (`src/JsonSettingsIOSteroids.cpp`)
and a **shared internal helpers include** (`src/JsonSettingsIOShared.inc`) so the
upstream `JsonSettingsIO.cpp` contains only CrossPoint upstream code. Internal
dead code (~230 lines of unreachable generic settings loader) was removed.

### File layout

| File | Contents |
|------|----------|
| `/.crosspoint/settings.json` | ~107 upstream CrossPoint settings (byte-identical to upstream) |
| `/.crosspoint/settings-steroids.json` | 43 Steroids-only settings with `formatVersion: 1` |
| `/.crosspoint/settings-steroids.json.bak` | **Pre-migration backup** of the original unified `settings.json`, created once during migration. Keep for manual rollback if needed. |

### Steroids-only fields (43 fields, 5 diverged from upstream)

Fields with diverging enum values/counts/defaults from upstream:
| Field | Divergence |
|---|---|
| `uiTheme` | Steroids adds `LYRA_MARCOAND75=3` |
| `fontFamily` | Steroids adds `LEXEND=2` |
| `longPressButtonBehavior` | Steroids adds `BOOKMARK=1`, `CLIPPING=2`, `FONTSIZE=5` |
| `clockFormat` | Aligned to upstream convention: `0=24h, 1=12h` |
| `displayDay` | Default changed from `1` (DATE_ONLY) to `2` (TIME_ONLY) |

All other fields are unique to Steroids (not present in upstream at all):
| Category | Fields |
|---|---|
| **Display/Theme** | `darkMode`, `antiGhostingExperimental`, `displayDay`, `clockFormat` |
| **Font/Rendering** | `fontFamily`, `guideReadingEnabled`, `dotsSpacing`, `epubRenderMode` |
| **Controls** | `longPressButtonBehavior` (legacy), `frontLongPressBehavior` (legacy), `longPressUpBehavior`, `longPressDownBehavior`, `frontLongPressLeftBehavior`, `frontLongPressRightBehavior`, `shortPwrBtn`, `selectLongPressBehavior`, `selectLongPress` (legacy), `cycleScreensaverOnTap` |
| **Status bar** | `statusBarTimeLeft` |
| **Library** | `libraryLayout`, `libraryFilter`, `librarySort`, `librarySearchText`, `libraryRootDir`, `libraryUpdateMode`, `libraryLastCleanupDay` |
| **Screensaver** | `screenSaverDirectory`, `screenSaverOrder`, `screenSaverInterval`, `screenSaverWakeButton`, `screenSaverReaderDir`, `screenSaverReaderOrder`, `screenSaverText`, `screenSaverFontSize`, `screenSaverTextPosition`, `screenSaverTextStyle`, `screenSaverShowPanel`, `screenSaverPanelColor`, `screenSaverPanelOpacity`, `screenSaverMinBattery`, `screenSaverReplaceSleep` |
| **Shortcuts** | `libraryShortcut*`, `screenSaverShortcut*`, `clippingsShortcut*`, `wikipediaShortcut*`, `quickCardsShortcut*` |

### Migration

On first boot after the upgrade, if `settings-steroids.json` doesn't exist,
`CrossPointSettings::loadFromFile()` extracts Steroids fields from the old
unified `settings.json`, saves them to the new file, creates a **pre-migration
backup** at `/.crosspoint/settings-steroids.json.bak`, and re-saves
`settings.json` without Steroids fields. If the migration fails at any point,
the old file is preserved and migration retries on next boot.

### Code architecture

- `src/JsonSettingsIO.h` / `.cpp`: upstream-only, byte-identical to upstream
- `src/JsonSettingsIOSteroids.h` / `.cpp`: Steroids-only serialization
- `src/JsonSettingsIOShared.inc`: shared internal helpers (`saveJsonDocumentToFile`, `migrateStoredUiTheme`, etc.)
- `src/CrossPointSettings.cpp`: unified facade (`saveToFile` saves both, `loadFromFile` loads both with migration)

### Web interface

Three web pages with clean separation:
- `/settings` — upstream-only device settings (~50 fields from WEB_SETTINGS)
- `/app-settings` — upstream app settings (SyncDay, Reading Stats, Achievements, Flashcards, Shortcuts, KOReader, Status Bar)
- `/steroids-settings` — all 43 Steroids fields via dedicated `/api/steroids-settings` endpoints

### Enum Selector UI

For Steroids enum settings that have more than two options (such as `longPressUpBehavior`, `shortPwrBtn`, etc.), a reusable popup selector (`EnumSelectorActivity`) is used instead of cycling through options. This provides a more efficient way to select enum values, especially for those with many options.

### Rollback safety

If `settings-steroids.json` is corrupted or deleted, all Steroids fields revert
to their struct-initializer defaults. Upstream settings in `settings.json` are
completely unaffected. The pre-migration backup at `/.crosspoint/settings-steroids.json.bak`
provides a manual recovery path to the original unified settings file.

### Shortcut order persistence

Shortcut order changes made in Home (Lyra MarcoAnd75 / Lyra Carousel) are
immediately persisted via `SETTINGS.saveToFile()`. Normalization occurs after
**both** JSON files are loaded, ensuring the 4 Steroids shortcuts (Library,
Screensaver, Clippings, Wikipedia) don't perturb the upstream shortcut ordering.

---

## 13. Silent Restart (Heap Reclamation)

When returning to Home from memory-intensive activities, a **silent restart**
(`ESP.restart()` via RTC_NOINIT magic) gives the system a clean heap slate.
The reboot is visually seamless: no "Loading..." popup, no white flash
(`display.begin(true)` skips the panel reset), and the Boot activity is skipped.

**Activities that trigger silent restart on Back-to-Home:**
- `LibraryActivity` — library cache, thumbnail parser, book index vectors
- `WikipediaActivity` — WiFi socket buffers, TLS session, HTTP response chunks
- Context-aware routing: when Library or Wikipedia (or other apps) are launched
  from the **Apps hub**, Back returns to Apps instead of Home; otherwise Back
  returns to Home. The `launchFromApps` flag on each activity controls this, and
  `silentRestartToApps()` / `silentRestartToHome()` route accordingly.

**Silent reboot boot optimizations (saving ~1088ms):**
- KOReader credential profiles skipped (unchanged mid-session)
- ReadingStats auto-backup skipped (already current)
- OPDS server list skipped (unchanged mid-session)
- Flashcard decks skipped (unchanged mid-session)

**Heap improvement:** `maxAlloc` rises from ~70 KB (cold boot with heavy activity)
to ~105 KB after silent restart, providing significantly more headroom for rendering
and UI operations. This directly reduces OOM risks in the status bar time-left
estimate, page rendering, and web server heap pressure.

---

## 14. Library Shelf Enhancements

### 14.1 Hide/Unhide Books

Books can be hidden from the Library shelf without deleting them from the SD card.

**Storage**: `/.crosspoint/hidden_books.json` — JSON array of `{bookId, path}` entries,
managed by `HiddenBooksStore` (singleton, same pattern as `FavoritesStore`). Atomic writes
via `serializeJson(doc, HalFile)`, atomic reads via byte-by-byte `file.read()`.

**Context menu** (long-press on book): "Hide from Shelf" / "Show on Shelf" (toggle label
based on current hidden state).

**Filter integration**:
- Hidden books are **excluded from all standard views**: All, Favourites, Latest Read,
  Unread, Completed. Exclusion happens in `LibraryIndex::matchesFilter()` before the
  per-filter switch.
- New **"Hidden" filter** in the filter popup shows **only** hidden books (for management).
- `LibraryIndex::totalMatching()` fast path for `FilterMode::ALL` now also checks
  `HIDDEN_BOOKS.isHidden()` (previously skipped filtering entirely).
- `BookRef` gained an `isHidden` flag set by `recordToBookRef()`.
- `CrossPointSettings` gained `LIBRARY_FILTER_HIDDEN = 5`.

**Grid refresh after hide/unhide**: `totalBooks_` and `totalPages_` are recalculated via
`LibraryIndex::totalMatching()`, the page cache is refreshed, and the selector resets
to the first book on the current page. Book/page counts in the header update immediately.

### 14.2 Permanent Book Deletion

**Context menu** → "Delete Book File" → confirmation dialog.

**Dynamic confirmation message**: changes based on `Settings → Library Update Mode`:
- **AUTO**: standard warning message.
- **Manual**: adds a reminder to run a manual library scan afterward.
  `STR_DELETE_BOOK_FILE_CONFIRM` / `STR_DELETE_BOOK_FILE_CONFIRM_MANUAL` i18n keys.

**What is deleted**:
- The book file from the SD card
- Per-book cache directory (`/.crosspoint/epub_<hash>` or `/.crosspoint/xtc_<hash>`)
- Cover thumbnail (matching `coverWidth_ × coverHeight_`)

**What is preserved**: reading stats, bookmarks, clippings — never touched.

**Post-deletion cleanup**:
- Removes book from Hidden books, Favorites, and Recent books lists
- **AUTO mode**: sets `forceScanOnNextOpen_ = true`, calls `scanSd()` to rebuild the
  library index immediately. Grid updates with correct counts.
- **Manual mode**: does not re-scan. The grid is not rebuilt. The user must run
  "Update" from the library menu.

### 14.3 Cover Generation Progress

During cover thumbnail generation (after clearing cache or on missing covers):
- **Per-tile progress bar**: white bar drawn on black placeholder tiles, proportional
  to `coverGenDone_ / coverGenTotal_`
- **Global counter**: "X/Y Loading…" displayed below the selected book author
- **Selector feedback**: selection frame moves to the book being generated, title/author
  update in real-time to show which book is being processed
- **Deferred start**: the grid is rendered before generation begins (`coverGenPending_` flag)
- **Input lock**: `coverGenLock_` blocks all input during single-cover generation to prevent
  state corruption when the user presses keys while the selector is temporarily moved

### 14.4 Side Button Page Navigation

| Button | Short Press | Long Press (≥800ms) |
|--------|-------------|---------------------|
| Up | Row up | Sort popup |
| Down | Row down | Filter popup |
| Left | Previous book | **Previous page** (P−) |
| Right | Next book | **Next page** (P+) |

Button labels updated: `Left/P−` / `Right/P+` (EN), `Sx/P−` / `Dx/P+` (IT).

### 14.5 Library Header Text Truncation

Book title, author, and info line use `renderer.truncatedText()` (Unicode ellipsis `…`,
UTF‑8 safe codepoint traversal, O(log n) binary search) with 8px minimum margins on both
sides. Font style used for truncation matches the draw style (BOLD for titles, REGULAR
for body) to prevent overflow past the margin.

### 14.6 Re-generate Cover Labels

| Action | EN | IT |
|--------|-----|------|
| Single | Re-generate this cover | Rigenera questa copertina |
| Page | Re-generate page covers | Rigenera copertine pagina |
| All | Re-generate all book covers | Rigenera tutte le copertine |

Page and all-cover deletions show a `ConfirmationActivity` dialog. After confirmation or
cancellation, `refreshPageCache()` triggers automatic regeneration of missing covers.
`ConfirmationActivity` body text uses `wrappedText` (word-wrap, max 8 lines) for long
messages.

### 14.7 New Files

| Path | Purpose |
|------|---------|
| `src/HiddenBooksStore.h` | Singleton for hidden books (bookId + normalized path) |
| `src/HiddenBooksStore.cpp` | Persistence, toggle, add, remove, deduplication |

### 14.8 Modified Files

| Path | Change |
|------|--------|
| `src/activities/home/BookContextMenuActivity.h/.cpp` | `isHidden` parameter, `HIDE_BOOK`, `DELETE_BOOK_FILE` menu actions, toggle label |
| `src/activities/apps/LibraryActivity.h/.cpp` | `coverGenLock_`, `coverGenPending_`, `deleteBookFile()`, hide/delete/filter handlers, progress bar rendering |
| `src/components/LibraryIndex.h` | `FilterMode::HIDDEN = 5`, `BookRef::isHidden` |
| `src/components/LibraryIndex.cpp` | `matchesFilter()` hidden exclusion, `totalMatching()` fast-path fix, `recordToBookRef()` isHidden |
| `src/CrossPointSettings.h` | `LIBRARY_FILTER_HIDDEN = 5` |
| `src/main.cpp` | `HIDDEN_BOOKS.loadFromFile()` at boot |

---

## 15. Dashboard (Lyra MarcoAnd75) Restructure

### 15.1 Layout

```
┌──────────────────────────────────────────┐
│  Panel 1: Title + Author                 │
├────────────────────┬─────────────────────┤
│  BOOK STATS        │  GLOBAL STATS       │
│  Read: 8h48m       │  Today: 1h8m        │
│  Sessions: 23      │  Goal: 30m  ✓       │
│  Days: 12          │  Curr. Streak: 9d   │
│  Left: ~2h5m       │  Books Read: 3      │
├────────────────────┴─────────────────────┤
│  Panel 3: Progress bar  81%             │
└──────────────────────────────────────────┘
```

Previously the footer panel (Streak/Read/ETA) was removed entirely. ETA moved to
BOOK STATS. Streak and Books Read moved to GLOBAL STATS. "Days" (distinct reading
days for the current book) added as a new metric.

### 15.2 Label Clarifications

| Metric | EN | IT |
|--------|-----|------|
| Already-read time | `Read` | `Letto` |
| Remaining time | `Left` | `Rimasto` |
| Distinct reading days | `Days` | `Giorni` |
| Current streak | `Curr. Streak` | `Serie attuale` |
| Finished books | `Books Read` | `Libri letti` |
| Goal reached | Geometric checkmark (drawLine, thickness 2) | ✓ |

### 15.3 Carousel Header

"Latest Recents (N)" / "Preferiti (N)" label at top-left (UI_12 font, 8px margin).
Does not affect carousel position or dot indicators.

### 15.4 Cache Versioning

Carousel cache directory: `/.crosspoint/marcoand75-cache-v3`. Bump
`MARCOAND75_CACHE_VERSION` in `src/activities/home/HomeActivity.cpp` when
the theme rendering logic changes.

### 15.5 Data source: summary.json fast path

The dashboard panel (GLOBAL STATS / BOOK STATS) and the carousel progress
badges/ETA/read-ribbon read their numbers through the **summary.json fast path**
(see [§22](#22-home-reading-stats-summary-json-fast-path)): at boot only the small
`/.crosspoint/summary.json` snapshot is loaded into RAM, so the full
`reading_stats.json` store (~41 KB) stays on SD. When the store is actually loaded
(reading, Reading Stats, Library, …), the getters transparently fall back to the
in-RAM store — no visible difference in the panel.

---

## 16. Image Rendering Tuning (Settings → Display)

Runtime-configurable parameters for cover/screensaver image rendering, centralized
in `lib/GfxRenderer/ImageRenderConfig.h/.cpp`.

| Setting | Type | Default | Range | Description |
|---------|------|---------|-------|-------------|
| Image Dithering | Toggle | ON | — | Enable error-diffusion dithering |
| Gamma LUT | Toggle | ON | — | Enable gamma-correction lookup table |
| Dither Algorithm | Enum | Atkinson | Atkinson / Floyd-Steinberg | Algorithm selection |
| Black Threshold | Value | 50 | 1–253 | Gray level below which = black |
| Dark Gray Threshold | Value | 120 | 2–254 | Gray level below which = dark gray |
| Light Gray Threshold | Value | 200 | 3–255 | Gray level below which = light gray |
| Gamma Value | Value | 15 (×10=1.5) | 5–30 (0.5–3.0) | Gamma correction multiplier |

**Failover**: all defaults match the historic X4 calibration. If settings are corrupted
or not yet loaded, failover values are used.

**Pipeline**: affects `Bitmap`, `JpegToBmpConverter`, `PngToBmpConverter`, and EPUB
inline converters via `DitherUtils.h`. `DitheringConfig.h` is now a backward-compat
wrapper around `ImageRenderConfig.h`.

**Settings UI**: value settings enter fast edit mode — up/down short = ±1 step,
long = ±5 step, Select confirms, Back cancels. Default values shown inline.

**Web interface**: full `WEB_SETTINGS` entries for `/api/settings`, GET/POST
steroids-settings handlers updated.

**New files**: `lib/GfxRenderer/ImageRenderConfig.h`, `lib/GfxRenderer/ImageRenderConfig.cpp`

---

## 17. Confirmation Dialog Multi-line Body

`ConfirmationActivity` body text changed from single-line `truncatedText` to
word-wrapped `wrappedText` (max 8 lines, max width = screen width minus 40px margins).
Long messages like the delete-book confirmation display fully across multiple lines.

Modified: `src/activities/util/ConfirmationActivity.h/.cpp`

---

## 18. Other Fixes

### 18.1 Clear Reading Cache Dialog (issue #37)

| EN | IT |
|----|-----|
| "This will clear cached book rendering data." | "Verranno cancellati i dati di rendering in cache." |
| "Your reading position and stats will be preserved." | "Posizione di lettura e statistiche saranno conservate." |

### 18.2 Side Button Hints Alignment

Side button hint boxes in Lyra themes:
- Height: 100px (was 78px) — prevents label overflow
- `topHintButtonY`: 318 (was 345) — physical button alignment
- Text Y centering: fixed `topHintButtonY + i * (buttonHeight + buttonSpacing)` instead
  of incorrectly adding `buttonSpacing` to the top button

---

*Last updated: 2026-08-10 — CPR-vCodex Steroids. Added §19 Quick Cards app, Select Long Press,
Settings dividers, X4 clock hide, clear cache fix, multi-device X3/X4 integration,
QR field parser.*
---

## 19. Quick Cards App

Image, QR code, and barcode viewer for quick-reference cards stored on the SD card.

### 19.1 Storage

```
/cards/
├── tessera.barcode     (Code-128 barcode, digits only, max 40 chars)
├── wifi_hotel.qr       (QR code: WIFI:T:WPA;S:Hotel;P:pass;;
│                         Second line = optional description)
├── photo.jpg           (JPEG — auto-converted to BMP, cached as .cache)
├── logo.bmp            (BMP — displayed directly)
└── icon.png            (PNG — conversion stable via PNGdec/openRAM)
```

`/cards/` is auto-created on first app launch.

### 19.2 File List

Cyberpunk panel style matching WikipediaActivity. Header shows QuickCards icon (32×32)
and title. Cards shown with type badge (`[IMG]`, `[QR]`, `[BAR]`) and filename without
extension. Long names truncated with `...`. Up/Down navigates, Confirm opens, Left
deletes (cache for images, file for QR/barcode), Back exits.

### 19.3 Image Cards (BMP/JPEG/PNG)

- BMP: direct rendering via `Bitmap` class with auto‑scaling (fit to screen, aspect
  ratio preserved).
- JPEG: auto-converted to 1-bit BMP via `JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize`
  and cached as `<filename>.cache` on SD. Subsequent opens use the cache.
- PNG: converted via `PNGdec` (openRAM, two-pass decode to 1-bit buffer), cached as
  `<filename>.cache`. Proven code path from `PngSleepRenderer`.
- Left button deletes only the cached BMP (not the source file). Cache files hidden
  from the card list (extension `.cache`, not scanned).

### 19.4 QR Cards (.qr files)

- Plain text file. The **QR payload** is everything before the description.
- The description separator is a **blank line** (`\n\n`). Multi-line QR formats
  such as vCard and calendar events should be kept intact as the payload, with a
  blank line before any optional description.
- Backward compatibility: for single-line payloads, the first line is still used
  as the QR code and the remaining lines are treated as description.
- `QrCardParser` extracts structured fields for 10 formats:

| Format | Prefix / marker | Extracted fields |
|--------|-----------------|-----------------|
| Wi‑Fi | `WIFI:...` | SSID, Password, Security, Hidden |
| vCard | `BEGIN:VCARD` | Name, Full Name, Organization, Phone, Email, URL, Address |
| MeCard | `MECARD:` | Name, Phone, Email, Note, URL, Address |
| Geo | `geo:` | Latitude, Longitude, Altitude |
| Email | `mailto:` | Email, Subject, Body |
| Phone | `tel:` | Phone |
| SMS | `sms:` / `smsto:` | Number, Message |
| OTP / 2FA | `otpauth://` | Account, Issuer, Type |
| Calendar event | `BEGIN:VEVENT` | Summary, Start, End, Location, Description |
| URL | `http://` or `https://` | URL, Domain |

- All values sanitized to printable ASCII (`0x20`–`0x7E`) for e-ink font compatibility.
- QR code rendered at 40% of available height, parsed fields below with `UI_12_FONT_ID`.
- Filename shown as title (without extension, bold).

### 19.5 Barcode Cards (.barcode / .bc files)

- Plain text file.
- **First line:** numeric digits only, **even count**, Code-128C encoded pairs (max 40 chars).
- If the first line contains non-digit characters or an odd number of digits, the
  viewer shows an error message instead of rendering bars.
- Remaining lines: optional free-text description.
- Barcode height: 1/3 of screen, centered vertically. Fullscreen: centered on screen.
- Digits rendered below the bars. Description wrapped in `UI_12_FONT_ID` below.

### 19.6 Fullscreen Mode

Confirm toggles fullscreen: only the image/QR/barcode is visible — no header,
footer, button hints, or count indicator. Filename shown centered at bottom
(UI_12, bold). Any button press exits fullscreen.

### 19.7 Registration

- App icon: 32×32 + 24×24 1-bit bitmaps (ID badge design).
- All Lyra theme variants updated: `LyraTheme`, `LyraCarousel`, `LyraMarcoand75`.
- `ShortcutRegistry`, `CrossPointSettings`, `JsonSettingsIOSteroids` — visibility,
  ordering, and persistence.
- Wired in both `AppsActivity` and `HomeActivity`.

### 19.8 Files

| Path | Purpose |
|------|---------|
| `src/activities/apps/QuickCardsActivity.h/.cpp` | Main activity |
| `src/util/QrCardParser.h` | Structured QR field extraction |
| `src/components/icons/quickcards.h` / `quickcards24.h` | App icon bitmaps |
| `src/images/icons/identity-svgrepo-com.svg` | Source SVG icon |

---

## 20. Clipping Navigation and Highlighting Fix

### 20.1 Problem

Clippings created under one layout (font, size, margins, alignment) would
open on the wrong page and highlight completely unrelated text after the
user changed any of those settings. The v1/v2 numeric offset system
(`absoluteWordStart`, `startWordIndex`) was reliable only within the same
layout and became stale after any pagination-affecting change.

### 20.2 Solution

Both **page positioning** and **highlight rendering** now use exclusively
text-search with the same consecutive-word matching algorithm proven in
`renderBookmarkHighlight` v3:

1. **Flat word array** — all words on the page are collected into a single
   `std::vector<PW>` with pointers to the original word strings
2. **Consecutive `strcmp` match** — starting from each word, the code checks
   if the next `minMatch` words match the clipping's tokens consecutively
3. **`minMatch = max(tokens.size() / 2, 3)`** — at least 3 tokens and at
   least 50% of the clipping text must match
4. **No fallback to numeric offsets** — if the text is not found on the page
   or in the chapter, nothing is highlighted (no false positives)

### 20.3 Page Positioning (from ClippingStore)

When the user taps "Go to clipping" in the ClippingsActivity, the callback
populates `pendingClippingText` from the clipping's `selectedText`. During
section load, the text-search scans **all pages** in the chapter via
`loadPageFromSectionFile()`, using the same consecutive-word algorithm, and
sets `nextPageNumber` to the page where the clipping text begins.

### 20.4 Multi-line Highlight Rendering

Consecutive matched words on the same line are grouped into a single
background `fillRectDither` covering spaces and punctuation between them.
Words on different lines get separate rectangles. All matched words are
redrawn in `EpdFontFamily::BOLD`.

### 20.5 `wordMatches` Helper

A `wordMatches()` function in the anonymous namespace handles case-insensitive
comparison with trailing punctuation skipping (`"Mercer"` matches `"Mercer,"`
or `"Mercer."`). Used only by the text-search across-pages for positioning.

### 20.6 Files Changed

| Path | Changes |
|------|---------|
| `src/activities/reader/EpubReaderActivity.cpp` | Rewritten `renderClippingHighlights` (-185/+59), new text-search positioning in `render()` |
| `src/activities/reader/EpubReaderActivity.h` | Added `pendingClippingText` member |

---

## 19.5. OTA Update Safety: Battery Check + Cancel (#68)

Added two safety safeguards around the OTA update flow:

**Minimum battery guard:** Before the download starts, `OtaUpdater::installUpdate()`
now checks `powerManager.getBatteryPercentage()` in `OtaUpdateActivity::loop()`
(WAITING_CONFIRMATION → Confirm). If battery is below 30% (configurable via
`OTA_MIN_BATTERY_PERCENT`), a `LOW_BATTERY_WARNING` state is entered instead,
showing a warning message with "Back" (cancel) and "Update" (proceed anyway)
buttons.

**Cancellable download phase:** The `OtaUpdater::installUpdate()` method now accepts
a `bool* cancelFlag` parameter, passed through to `HttpDownloader::downloadToFile()`
(which already supported cancellation). The progress callback in `OtaUpdater.cpp`
polls `gpio.isPressed(BTN_BACK)` every ~500 ms (during progress reporting) and sets
the cancel flag, causing the download to abort cleanly (`HttpDownloader::ABORTED`).
The downloaded partial file is removed. The render loop shows a "Press Back to
cancel download" hint below the progress bar.

**New error code:** `OtaUpdater::ABORTED` is returned on user cancellation, which
the activity handles by transitioning to `FAILED` state with an "Update cancelled"
message.

**Files:** `src/network/OtaUpdater.h/.cpp` (cancelFlag param, ABORTED code, gpio
check in progress callback), `src/activities/settings/OtaUpdateActivity.h/.cpp`
(battery guard, cancel handling, LOW_BATTERY_WARNING state, cancel hint),
`src/main.cpp` (untouched — power button logic not affected),
`lib/I18n/I18nKeys.h`, `lib/I18n/translations/english.yaml`,
`lib/I18n/translations/italian.yaml`.

## 21. Feature State & Changelog (base `07126f2b` → HEAD)

What actually changed from commit `07126f2b` (2026-08-09) up to `4eaf2371`
(2026-08-18). Repeatedly: only **maintained** changes are listed as active;
anything reverted is called out under *not active*.

### 21.1 Active (maintained) additions

- **CrossInk EPUB engine integration** (`9430b747`, merged in `102211a6`): replaces the
  EPUB parser/rendering stack with CrossInk (`lib/Epub/epub/*`, `lib/MiniBidi/*`,
  `lib/miniz` + `third_party/miniz.c`, arena allocators `lib/Memory/Arena.*` used
  **inside** the engine). The arena is **not** used on the reader render hot path.
  - Per-book reader settings + remember successful render mode to skip re-indexing.
  - Text-only **Safe Mode** as final fallback for large chapters + relaxed
    low-memory thresholds (LIGHT/SAFE).
  - Per-chapter image-dimension cache (less heap churn).
  - **Bionic-reading OFF fix** (`c2a65b20`): reader now passes `foregroundBlack=true` to
    `page->render()`; also fixed the prewarm scope that was clearing the font cache.
- **Widening/hook refinements:** hyphenation dictionaries are now opt-in
  (`CPR_ENABLE_*_HYPHENATION`, default OFF; EN/IT/PT enabled) — `d96809b8`. Flash
  dropped to ~98.2%.
- **SD-font advance table** grows in place via `realloc` (`0ce73e5f`) — maxAlloc
  stable (~34 KB) instead of spikey new[]/delete[].
- **Reading stats:** vCodex JSON pipeline retained; **CrossInk binary stats were
  removed** (`src/ReadingStats/` empty). Boot lazy-loads stores
  (`6c264043`), JSON deserialized from stream (no whole-file String, `4b9055ed`).
- **Screensaver / sleep PNG** stabilised and tuned: prewarm overlay glyphs *before*
  PNG decode (avoids OOM), release memory before each decode/no double SD write,
  shuffle/sequential first-image + persisted cycle index, and Image-rendering/gamma
  + grayscale + text-overlay ghosting fixes (`1a9eaa98`).
- **Library cover cache** now uses the same **FNV-1a 64** hash as `Epub` for EPUB
  covers (delete/align), plus adaptive (contain) cover thumbnails and wrapped
  pagination dots.
- **Settings stability:** fixed OOB read (settingsCount/currentSettings mismatch) and
  the dangling label pointer in the tab bar (label is now `std::string`).
- **Boot / perf:** lazy store load, `silentRestartToHome()` (defragments heap on
  reader exit instead of a popup), boot-stage `HCR-FRAG` diagnostics.
- **Home reading-stats summary fast path** — `/.crosspoint/summary.json` snapshot
  (global stats + per-book home badges) so the Home renders its dashboard/carousel
  without loading the full ~41 KB store at boot. BootActivity now preloads the
  summary instead of the store; the full store stays lazy (`ensureLoaded`). See
  [§22](#22-home-reading-stats-summary-json-fast-path).
- **SdCardFont fragmentation-resistant storage** (1.5.0.20 port, `72515f4f`):
  4 KiB chunked bitmap storage replaces single-buffer allocation, TextGetter
  prewarm + coverageHandler for CJK fallback, FrameBufferLoan for section builds,
   FontDecompressor raw-buffer refactor. See §23.
- **Status bar time-left expanded** to 5 modes — added Session Duration
  (resets per session) and Today Total (uses summary.json fast path). See §23.13.
- **Long-press button actions expanded** to 10 options — added Dictionary,
  Toggle Dark Mode, Force Full Refresh, Quick Settings for both side and front
  buttons. See [§8.5](#85-expanded-long-press-button-actions).
- **OTA update safety** — battery check before download (min 30%), cancel
  option during download via Back button, graceful shutdown screen. See §19.5 and §4.4.
- **Wikipedia overhaul** — see [§5](#5-wikipedia-app).
- **Quick Cards** — see [§19](#19-quick-cards-app).
- **Clipping navigation & highlight fix** — see [§20](#20-clipping-navigation-and-highlighting-fix).
- **X3/X4 multi-device** migration to **freeink-sdk** + `XteinkDetectExt`, hold power
  rails, HalSpiBus mutex, boot-sequence fixes.
  - **Unified auto-connect Wi-Fi (issue #90)**: Auto-connect to a saved in-range network (preferring last-connected, then strongest signal); only falls back to the manual network list when no saved network is reachable.
  - **Scrollbar normalization (#71)**: Added scrollbars to Achievements, Reading Stats, Reading Profile, and Home activities for improved navigation.
### 21.2 Not active (introduced then reverted / removed)

- **Arena "optimizations" in `EpubReaderActivity` render hot path** — introduced in
  `bac51044`, reverted in `afe9b42c` (dangling `ClippingWordInfo`, arena clear/checkpoint
  conflict, unbounded growth). Render path stays on `std::vector`/`std::string`.
- **Zip EOCD scan >1 KB / streaming** — introduced `d4026a42`/`9c5de90b`, reverted in
  `a086ee3d` back to the original **1 KB EOCD scan** (the wide scan caused a ~131 KB
  malloc failure and the streaming variant an infinite loop). Note: some non-standard
  zips (e.g. `Vengeful - V.E. Schwab.epub`) remain unloadable.
- **Inter as default UI font** — reverted in `ef8dd0c5`; the UI font is **Ubuntu**
  (ubuntu_10/12). The hyphenation-shrink part was kept independently (`d96809b8`).
- **CrossInk binary reading stats** — removed (`1a0dbcc1`); only the vCodex JSON store
  remains.
- **Reading-stats pool/string shrinkToFit** — introduced `20ed6787`, reverted `875fab16`.
- **`patch_pngdec.py` (upstream PNG optimization)** — removed `25bac0e3` because it is
  incompatible with the Steroids `PngSleepRenderer`.

### 21.3 Improvements that could still come (gaps / next candidates)

- Wikipedia **full-article indexing is slow** (first open builds `index.bin` by wrapping
  every page; SD-font glyph advance lookups dominate — see §5.3). A per-article
  precomputed `index.bin` already mitigates repeats, but the very first build is costly.
- Non-standard-zip EPUBs with oversized EOCD comments remain unloadable (locked to the
  1 KB EOCD scan).
- The CrossInk engine is the single largest divergence; future upstream merges touching
  `lib/Epub/epub/*`, `lib/MiniBidi`, `lib/Memory/Arena.*` must be reconciled manually.

### 21.4 Addictions / dependency notes

- Steroids now **depends** on the CrossInk engine artifacts (`lib/Epub/epub/*`,
  `MiniBidi`, `lib/miniz`, `lib/Memory/Arena.*`) and on **freeink-sdk** at a **new pinned
  submodule commit** (`SDCardManager::listFiles(..., includeDirectories)`). Any upstream
  merge that drops or restructures these will silently regress EPUB rendering and wiki
  cache listing unless re-pinned/re-added.
- `src/ReadingStats/` is intentionally empty at HEAD; an upstream merge that re-adds
  those binaries needs a decision (keep vCodex JSON, do not reintroduce binary).
- PNG/screensaver path must never take upstream `patch_pngdec.py` again.
- **`lib/Serialization/CredentialIntegrity.h`** — new dependency for WifiCredentialStore
  CRC-32 password validation (from upstream 1.5.0.20).
- **`lib/Memory/BuildScratch.h/cpp`** — new dependency for FrameBufferLoan during EPUB
  section builds (from upstream alignment, `f467593a`).

---

*Last updated: 2026-08-23 — added §23 SdCardFont fragmentation-resistant storage (1.5.0.20 port), §23.10 HAL crash detection (PANIC_CAPTURE_MAGIC), §23.12 carousel recents panel guard fix + icon count/RecentBooks expansion, §23.13 status bar time-left Session Duration + Today Total, updated §21.1 changelog and §8 enhancements.*

---

## 23. SdCardFont Fragmentation-Resistant Storage (1.5.0.20 port)

Ported from upstream CPR-vCodex 1.5.0.20 (`72515f4f`) to eliminate large contiguous
allocations for SD-card font glyphs on the ESP32-C3 (380 KB usable RAM, no PSRAM).

### 23.1 Problem
The original `SdCardFont` allocated a single contiguous buffer (`miniBitmap`) for the
full set of mini glyphs needed for a page render. On a fragmented heap this could
fail to find a contiguous block even when enough total free memory existed, causing
render failures or fallback to slower direct glyph measurement.

### 23.2 Solution: 4 KiB chunked storage
Replaced the single `miniBitmap` buffer with **24 × 4 KiB chunks** (`miniBitmapChunks[24]`),
totaling 96 KiB virtual address space backed by on-demand chunk allocation. Each chunk
is allocated independently, so the 380 KB heap only needs a single 4 KiB block at a
time instead of a large contiguous allocation.

Key additions:
- `miniGlyphBitmap()` — overflow-safe glyph bitmap retrieval with chunk fallback
- `TextGetter` callback typedef + `prewarm()` overload — pre-warm font data before
  rendering without re-allocating
- `loadKernLig` parameter — controls kerning/ligature loading during prewarm
- `onCoverageQuery()` static callback — dispatches to `EpdFontData::coverageHandler`
  for CJK font family fallback resolution
- `contentHash_` tracking — detects content changes without full re-scan

### 23.3 CJK fallback font resolution
Added `hasCodepoint()` to `EpdFont`, `EpfFontFamily`, and a `coverageHandler` field
on `EpdFontData` to support querying whether a font family can render a given Unicode
codepoint. `GfxRenderer::resolveTextFontId()` uses `utf8IsCjkCodepoint()` +
`hasCodepoint()` to detect CJK text and automatically select a fallback font family
that has coverage for those codepoints. This is wired into `getTextWidth()`,
`drawText()`, `getTextAdvanceX()`, and `drawTextRotated90CW()`.

**Note:** The Steroids font style enum preserved `SMALL_CAPS=64` and
`RUBY_CONTINUE=128` (upstream 1.5.0.20 removed SMALL_CAPS and renumbered
RUBY_CONTINUE to 64). These values are used in `ChapterHtmlSlimParser.cpp:610` and
must not be changed.

### 23.4 FrameBufferLoan integration
`GfxRenderer` gained a `FrameBufferLoan` RAII class (backed by `BuildScratch.h`)
that loans the framebuffer during EPUB section builds, allowing the inflate
stream to get a guaranteed ~43 KB contiguous window. This prevents
"Failed to init inflate stream" errors and the resulting render-mode fallback
cascade. `releaseFrameBufferForBuild()` / `restoreFrameBufferAfterBuild()` manage
the loan scope.

### 23.5 FontDecompressor refactor
Replaced `std::vector<uint8_t>` for `hotGroup`/`hotGlyphBuf` with raw `uint8_t*` +
capacity, using `malloc`/`realloc` for in-place growth. Removed the
`isInitialized()`/`_initialized` lazy-init guard (the decompressor now always
initializes in `init()`), simplifying the `main.cpp` boot logic.

### 23.6 SdCardFontManager refactor
`loadFamily()` refactored with standard-size detection (prefers sizes 12/14/16/18
in order). Added `loadFile()` helper and `loadFamilyExtraSize()` for non-standard
font sizes. `unloadAll()` now calls `clearFallbackFonts()` to release fallback
family references. `ensureSdCardFontReady()` in `GfxRenderer` updated to accept
`std::deque` (backward-compatible inline wrapper for `std::vector`) and a
codepoints overload via `fetchAdvancesForCodepoints()`.

### 23.7 FontCacheManager fix
Added `scanFontIdSet_` boolean flag to replace the `scanFontId_ < 0` sentinel check.
SD card font IDs can legitimately be negative (they're assigned by SdCardFontManager
at runtime), so the `< 0` sentinel was unreliable. The `scanFontIdSet_` flag
explicitly tracks whether a scan font ID was set during `recordText()`/`recordStyle()`.

### 23.8 Build safety
- Build: SUCCESS — RAM 16.0% (52276/327680), Flash 98.5%, 0 warnings
- `Utf8::utf8IsCjkCodepoint()` added for CJK range detection (also from 1.5.0.20)

### 23.9 Protected files
The following files are now in the protected list and must never be overwritten by
upstream merges:
- `lib/EpdFont/SdCardFont.h` / `SdCardFont.cpp`
- `lib/EpdFont/SdCardFontManager.h` / `SdCardFontManager.cpp`
- `lib/EpdFont/EpdFont.h` / `EpdFont.cpp`
- `lib/EpdFont/EpdFontFamily.h` / `EpdFontFamily.cpp`
- `lib/EpdFont/EpdFontData.h`
- `lib/EpdFont/FontDecompressor.h` / `FontDecompressor.cpp`
- `lib/GfxRenderer/FontCacheManager.h` / `FontCacheManager.cpp`
- `lib/GfxRenderer/GfxRenderer.h` / `GfxRenderer.cpp`

### 23.10 Completed: HAL crash detection (PANIC_CAPTURE_MAGIC)

- **HAL crash detection (`PANIC_CAPTURE_MAGIC`)**: ported from upstream 1.5.0.20
  (`d3e21a61`). Added `panicCaptureMarker` (RTC_NOINIT_ATTR volatile uint32_t)
  set in `__wrap_panic_abort` and `__wrap_panic_print_backtrace`. `isRebootFromPanic()`
  now treats watchdog resets as panic reboots ONLY when the magic marker is set,
  preventing false-positives on normal deep-sleep wake cycles. `checkPanic()`
  verifies write completeness and clears the marker on success. `CrashActivity`
  no longer calls `clearPanic()` explicitly — `checkPanic()` handles it.
  `HalSystem.h/cpp` are NOT in the protected list (they're simple enough to
  take from upstream if needed).

### 23.11 Remaining deferred from 1.5.0.20

- **SdCardFontRegistry case-insensitive dirs**: NOT NEEDED — Steroids uses a
  different font directory management approach.

### 23.12 Carousel recents panel + icon count (home screen)

- **Panel removal**: `HomeActivity::drawCarouselRecentsPanel()` was guarded by
  `isLyraCarouselTheme()`, which returns true for **both** `LYRA_CAROUSEL` and
  `LYRA_MARCOAND75`. The cyber panel showing "Carousel Recents (N)" was designed
  specifically for Lyra Marcoand75's layout. Changed the guard to check
  `UI_THEME::LYRA_MARCOAND75` specifically — the panel now only appears in that theme.
- **Icon count**: `kVisibleMenuSlots` in `LyraCarouselTheme::drawButtonMenu` increased
  from 5 to 7, matching `LyraMarcoand75Theme`. Both carousel themes now show 7 app
  icons before scrolling.
- **Recent books count**: `homeRecentBooksCount` in `LyraCarouselTheme` increased from
  3 to 20 (clamped to `HOME_MAX_BOOKS=10` by `HomeActivity`), allowing navigation
  through all 10 recent books.

**Files:** `src/activities/home/HomeActivity.cpp` (`drawCarouselRecentsPanel`
guard), `src/components/themes/lyra/LyraCarouselTheme.h` (`homeRecentBooksCount`),
`src/components/themes/lyra/LyraCarouselTheme.cpp` (`kVisibleMenuSlots`).

### 23.13 Status Bar Time-Left: Session Duration + Today Total

Steroids added two new modes to the status bar time-left display (settings: `Settings > Customize Status Bar`), expanding the enum from 3 to 5 values:

| Mode | Value | What it shows | Data source |
|------|-------|---------------|-------------|
| **Session Duration** | `TIME_LEFT_SESSION = 3` | Time read since the current reading session began | `ReadingStatsStore::getSessionReadingMs()` → `activeSession.accumulatedMs` (resets on every `beginSession()`/`endSession()`, i.e. book open/close) |
| **Today Total** | `TIME_LEFT_TODAY = 4` | Total reading time for today | `ReadingStatsStore::getTodayReadingMs()` — reads from `summary.json` fast path (already includes the current session via `noteActivity()` → `recordReadingTime()`) |

The existing Chapter/Book modes (pace-based estimates) are unchanged. Session/Today modes render directly without needing the page-based estimate heap check (`kMinSafeAllocForTimeLeft`).

The label was renamed from "Time Left" (EN) / "Tempo rimanente" (IT) to "Display Time" (EN) / "Tempo visualizzato" (IT) since the display now includes elapsed time, not just remaining time. Italian users see "Durata sessione" and "Totale oggi" for the two new options.

**Files:** `src/CrossPointSettings.h` (enum), `src/ReadingStatsStore.h/.cpp` (`getSessionReadingMs()`), `src/activities/reader/EpubReaderActivity.cpp` (rendering), `src/activities/settings/StatusBarSettingsActivity.cpp` (settings UI + preview), `src/SettingsList.cpp` (settings menu), `src/network/CrossPointWebServer.cpp` (web UI), `lib/I18n/I18nKeys.h`, `lib/I18n/translations/english.yaml`, `lib/I18n/translations/italian.yaml`.

---

## 22. Home Reading-Stats Summary (summary.json fast path)

The Home screen (Lyra/Marcoand75 dashboard, carousel progress badges/ETA, read
ribbon, book long-press menu) previously forced the full `reading_stats.json`
store (~41 KB: all books + `readingDays` + `sessionLog`) into RAM at boot just to
show the global stats panel and per-book progress. This feature removes that
requirement: the Home now renders from a small derived snapshot.

### 22.1 Storage & schema

`/.crosspoint/summary.json` — a lightweight, derived snapshot regenerated on every
stats change:

```json
{
  "summary": {
    "totalReadingMs": 0, "todayReadingMs": 0, "recent7ReadingMs": 0,
    "recent30ReadingMs": 0, "currentStreakDays": 0, "maxStreakDays": 0,
    "booksFinishedCount": 0, "goalReadingMs": 0, "referenceDayOrdinal": 0
  },
  "bookBadges": [
    { "bookId": "", "path": "", "progressPercent": 0, "totalReadingMs": 0,
      "sessions": 0, "readingDaysCount": 0, "completed": false }
  ]
}
```

Badges are written only for books that have progress / are completed / have read
time, keeping the file small (tens of bytes per entry). Each badge carries the
fields the Home data panel needs (total time, sessions, distinct days, completed)
so the panel renders correctly even when the store is not in RAM.

### 22.2 Write path

- `ReadingStatsStore::markDirty()` → `saveSummaryJSON()` rebuilds the summary cache
  and rewrites `summary.json` whenever the in-RAM store changes. Guarded by
  `loaded_` so it never clobbers a good file right after a network memory release.
- `saveToFile()` also refreshes the summary after every store save.
- `reset()` / `importFromFile()` regenerate it (empty or new) via the same paths.

### 22.3 Read path (Home fast path)

- `preloadHomeSummary()` (called from `BootActivity`) reads `summary.json` once and
  caches it (`summaryJson` + `summaryJsonValid_`). If the file is missing (upgrade
  from a pre-feature build), it loads the full store once, generates the summary,
  then releases the store again.
- Getters that work from the cached summary when the store is **not** loaded, and
  fall back to the in-RAM store when it is:
  `getGlobalSummary()`, `getBookProgressForHome()`, `getBookHomeStats()`,
  `getHomeBookStatsForRender()` (synthesizes a `ReadingBookStats` for the themes),
  plus the existing `getTotalReadingMs()` / `getTodayReadingMs()` /
  `getRecentReadingMs()` / `getCurrentStreakDays()` / `getMaxStreakDays()` /
  `getBooksFinishedCount()`.
- **Day-rollover handling:** `getTodayReadingMs()` returns 0 when the snapshot
  predates today; `getCurrentStreakDays()` returns 0 when the snapshot is more
  than one day old (streak possibly broken).

### 22.4 Consumers switched to the fast path

- `BootActivity` — `preloadHomeSummary()` instead of `ensureLoaded()`.
- `HomeActivity` — carousel badge hash/percent, long-press context menu (completed
  state) via `getBookProgressForHome()` / `getBookHomeStats()`.
- All Lyra themes (`LyraTheme`, `LyraCarouselTheme`, `LyraCustomTheme`,
  `LyraMarcoand75Theme`) — progress %, ETA, read ribbon via
  `getBookProgressForHome()` / `getHomeBookStatsForRender()`.

### 22.5 Correctness guards

- `beginSession()` calls `ensureLoaded()` first, so opening a book while the store
  is unloaded can never lose existing stats.
- Screens that need the full store (Reading Stats, Library, Achievements, Reader,
  …) still call `ensureLoaded()` and load it lazily as before.
- `releaseMemoryForNetwork()` keeps the cached summary valid, so the Home keeps
  rendering correctly after a network-heavy operation drops the store.

### 22.6 RAM impact & verification

At boot the ~41 KB store stays on SD; the Home reads only the small summary.
Verified on device via boot log: `Reading stats deferred (loaded on demand)`
followed by `HOME onEnter: global summary: total=163161649 today=721793 streak=2`
and the carousel frame cache `HIT` — the dashboard and badges render with correct
values while the full store is never loaded.

**Files:** `src/ReadingStatsStore.{h,cpp}` (SummaryJSON, save/load summary,
summary-aware getters), `src/activities/boot_sleep/BootActivity.cpp`,
`src/activities/home/HomeActivity.cpp`,
`src/components/themes/lyra/LyraTheme.cpp`, `LyraCarouselTheme.cpp`,
`LyraCustomTheme.cpp`, `LyraMarcoand75Theme.cpp`.

---

*Last updated: 2026-08-23 — added §23 SdCardFont fragmentation-resistant storage (1.5.0.20 port), §23.10 HAL crash detection completed, §23.12 carousel recents panel fix, §23.13 status bar time-left Session Duration + Today Total, §8 status bar time-left expanded to 5 modes, §8.5 expanded long-press button actions (Dictionary / Dark Mode / Full Refresh / Quick Settings), §8.5.2 per-directional long-press configuration (Up/Down side buttons, Left/Right front buttons, expanded power button + select long-press), §19.5 OTA update safety (battery check + cancel #68), §4.4 battery safety under WiFi load (#59), SdCardFont/TextGetter/FrameBufferLoan, §21.4 dependency notes.*
