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
| **Wikipedia** | `WikipediaActivity` + `WikiTxtReaderActivity` | Download/read Wikipedia articles. Search, save as markdown `.wiki` in cache, summary preview (in-app), full-article reading via a dedicated reader. Detail in [§5](#5-wikipedia-app). |
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
- **`WikiTxtReaderActivity`** — dedicated reader for the cached `.wiki` files.

### 5.1 Download flow (`fetchFullArticle`)
1. Calls `https://it.wikipedia.org/w/api.php?action=parse&page=<TITLE>&prop=wikitext&format=json`
   (wikitext JSON, not the mobile-html REST API).
2. Streams the JSON response to `raw_<title>.json` on SD (no full-RAM copy).
3. **`WikitextToMarkdown`** (`src/util/WikitextToMarkdown.{h,cpp}`) streams the JSON,
   scans the `"wikitext"` → `"*"` field, decodes JSON escapes on the fly, and writes a
   **markdown** `.wiki` cache file (`/.crosspoint/wikipedia-cache/<title>.wiki`).
   - wikitext `'''...'''`/`''...''` → markdown `**...**`/`*...*`; `==H==` → `# H`;
     lists `*`/`#`; links `[[X|Y]]` → display text; `{{templates}}`, `<ref>`, HTML
     comments and multi-line infobox templates stripped.
   - `HtmlToTxt` is **kept but unused** in the Wikipedia flow.

### 5.2 Reading flow
- **Summary preview** uses the original in-app Wikipedia rendering (`renderArticle()`).
- **Full downloaded article / cached `.wiki` files** open via **`WikiTxtReaderActivity`**
  (launched with `startActivityForResult`).

### 5.3 WikiTxtReaderActivity (dedicated reader)
Uses the **same reading/rendering system as `TxtReaderActivity`** but without the
book-reader side effects:
- **Markdown span parsing**: `**bold**`, `*italic*`, `#` headings, `-`/ordered lists,
  `>` blockquotes.
- **Page index** built in RAM (`buildPageIndex`) + **per-article `.bin` cache**
  (`<title>.wiki.bin`) with settings-validation (font / margin / lines / viewport).
- **Chunked file reading** (`loadPageAtOffset`) with span-aware wrapping and SD-font
  priming per chunk.
- **Two-pass prewarm rendering** (`renderPage`) + status bar with progress
  (`Pag. N/M`).
- `.wiki` content is always treated as markdown.
- **No** reading stats, achievements, recent books, progress files, completed-book
  mover, or orientation handling.

**Files:** `src/activities/apps/WikipediaActivity.{cpp,h}`,
`src/activities/reader/WikiTxtReaderActivity.{cpp,h}`, `src/util/WikitextToMarkdown.{cpp,h}`,
`src/util/MarkdownReader.{cpp,h}`.

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

- **Reading Time Left** in the reader status bar — pace-based estimates (Hide /
  Chapter / Book), per-book pace learned from natural page turns, short labels.
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
- **Power button / deep-sleep state machine** (see §4.3).

- **Select Long Press configuration** (`CrossPointSettings::selectLongPress`) — 3 modes:
  Bookmark (default, toggle bookmark on current page), Reading Timer (toggle reading
  time tracking pause/resume), Off. Popup feedback and `|| PAUSED` status bar indicator.
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
upstream `/.crosspoint/settings.json`. This isolates the 37 Steroids-specific
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
| `/.crosspoint/settings-steroids.json` | 37 Steroids-only settings with `formatVersion: 1` |
| `/.crosspoint/settings-steroids.json.bak` | **Pre-migration backup** of the original unified `settings.json`, created once during migration. Keep for manual rollback if needed. |

### Steroids-only fields (37 fields, 5 diverged from upstream)

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
| **Font/Rendering** | `guideReadingEnabled`, `dotsSpacing`, `epubRenderMode` |
| **Controls** | `frontLongPressBehavior`, `cycleScreensaverOnTap` |
| **Status bar** | `statusBarTimeLeft` |
| **Library** | `libraryLayout`, `libraryFilter`, `librarySort`, `librarySearchText`, `libraryRootDir`, `libraryUpdateMode`, `libraryLastCleanupDay` |
| **Screensaver** | `screenSaverDirectory`, `screenSaverOrder`, `screenSaverInterval`, `screenSaverWakeButton`, `screenSaverReaderDir`, `screenSaverReaderOrder`, `screenSaverText`, `screenSaverFontSize`, `screenSaverTextPosition`, `screenSaverTextStyle`, `screenSaverShowPanel`, `screenSaverPanelColor`, `screenSaverPanelOpacity`, `screenSaverMinBattery`, `screenSaverReplaceSleep` |
| **Shortcuts** | `libraryShortcut*`, `screenSaverShortcut*`, `clippingsShortcut*`, `wikipediaShortcut*` |

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
- `/steroids-settings` — all 37 Steroids fields via dedicated `/api/steroids-settings` endpoints

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

- First line: QR code payload (parsed by `QrCardParser`).
- Remaining lines: optional free-text description, displayed below parsed fields.
- `QrCardParser` extracts structured fields for 10 formats:

| Format | Prefix | Extracted fields |
|--------|--------|-----------------|
| Wi‑Fi | `WIFI:...;;` | SSID, Password, Security, Hidden |
| vCard | `BEGIN:VCARD` | Name, Full Name, Organization, Phone, Email, URL, Address |
| MeCard | `MECARD:` | Name, Phone, Email, Note, URL, Address |
| Geo | `geo:lat,lon` | Latitude, Longitude, Altitude |
| Email | `mailto:` | Email, Subject |
| Phone | `tel:` | Phone |
| SMS | `sms:`/`smsto:` | Number, Message |
| OTP | `otpauth://` | Account, Issuer |
| Event | `BEGIN:VEVENT` | Summary, Start, End, Location, Description |
| URL | `http(s)://` | URL, Domain |

- All values sanitized to printable ASCII (`0x20`–`0x7E`) for e-ink font compatibility.
- QR code rendered at 45% of available height, parsed fields below with `UI_12_FONT_ID`.
- Filename shown as title (without extension, bold).

### 19.5 Barcode Cards (.barcode / .bc files)

- First line: numeric digits (Code-128, max 40 chars).
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
