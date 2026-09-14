# CPR-vCodex Steroids — App Definitions & Enhancements (Master Index)

> **SCOPE OF THIS FILE**
>
> `STEROIDS-ADDICTIONS.md` is the **master index** for all Steroids apps, screensavers, sleep/deep-sleep handling, and enhancements. Each major feature is documented in its own dedicated markdown file (see table below).
>
> The other Steroids definition files are:
> - `STEROIDS-ALIGN-TO-UPSTREAM.md` — Workflow to merge upstream while preserving Steroids features
> - `STEROIDS-OPTIMIZATION.md` — Shared procedures & optimization reference
> - `STEROIDS-ADDICTIONS-LUA.md` — Lua plugin development guide

---

## 1. Overview

CPR-vCodex Steroids is a fork of CPR-vCodex for the **Xteink X4** e-reader (tested also on X3 with DS3231 RTC). Design goals: stable reading first, then careful improvements — a full e-book library, cyber-style carousel panels, dual-mode e-ink screensaver, contextual book menus, reading pace tracking, bookmarks & clippings, reading statistics, flashcards, dictionary, and a set of quality-of-life additions. Built for the ESP32-C3 (~380 KB usable RAM, no PSRAM).

**Golden rule:** **Never sacrifice reading stability for feature size**; new features must respect RAM/heap/e-ink-refresh budgets.

---

## 2. Feature Documentation Index

| Feature | Document | Description |
|---------|----------|-------------|
| **Library** | [`STEROIDS-ADDICTIONS-LIBRARY.md`](STEROIDS-ADDICTIONS-LIBRARY.md) | Grid-based e-book library (EPUB/XTC/TXT/MD), sort/filter/search, cover generation, collections/series, mixed Series+Books view, state persistence |
| **Wikipedia** | [`STEROIDS-ADDICTIONS-WIKIPEDIA.md`](STEROIDS-ADDICTIONS-WIKIPEDIA.md) | Download/read Wikipedia articles, search, cache, summary preview, full-article reading |
| **Screensaver & Sleep** | [`STEROIDS-ADDICTIONS-SCREENSAVER.md`](STEROIDS-ADDICTIONS-SCREENSAVER.md) | Dual-mode e-ink screensaver, transparent PNG compositing, deep-sleep handling, power button state machine |
| **Reading Statistics** | [`STEROIDS-ADDICTIONS-READING-STATS.md`](STEROIDS-ADDICTIONS-READING-STATS.md) | Reading stats, pace tracking, heatmap, profile, achievements, manual corrections, summary.json fast path |
| **Dictionary** | [`STEROIDS-ADDICTIONS-DICTIONARY.md`](STEROIDS-ADDICTIONS-DICTIONARY.md) | Multi-dictionary lookup, failover/manual modes, reorderable list, orphan cleanup, reading-progress indicator |
| **Flashcards** | [`STEROIDS-ADDICTIONS-FLASHCARDS.md`](STEROIDS-ADDICTIONS-FLASHCARDS.md) | Spaced-repetition (SM-2), decks, review sessions, per-deck stats, recents, settings |
| **Bookmarks & Clippings** | [`STEROIDS-ADDICTIONS-BOOKMARKS-CLIPPINGS.md`](STEROIDS-ADDICTIONS-BOOKMARKS-CLIPPINGS.md) | Layout-independent absolute word indices, cross-book browsers, reader integration |
| **Quick Cards** | [`STEROIDS-ADDICTIONS-QUICK-CARDS.md`](STEROIDS-ADDICTIONS-QUICK-CARDS.md) | Image/QR/barcode viewer, 10 QR formats, Code-128, cyberpunk panel |
| **Apps System** | [`STEROIDS-ADDICTIONS-APPS.md`](STEROIDS-ADDICTIONS-APPS.md) | App registration, icon mapping, shortcut persistence, 5-piece mandatory checklist |

---

## 3. App Registration & Persistence (Quick Reference)

> **Full guide:** [`STEROIDS-ADDICTIONS-APPS.md`](STEROIDS-ADDICTIONS-APPS.md)

### 3.1 5 Mandatory Pieces for New App
1. **Sprite bitmap** (32px + 24px) in `src/components/icons/`
2. **`UIIcon` enum value** in `BaseTheme.h`
3. **`ShortcutDefinition`** in `ShortcutRegistry.h`
4. **`case` in `iconForName()`** of EVERY theme (Lyra, Carousel, Marcoand75, Custom)
5. **3 JSON fields** in `CrossPointSettings.h` + serialization in `JsonSettingsIOSteroids.cpp`

### 3.2 Current Apps (21 Total)

| App | ShortcutId | UIIcon | Settings Prefix |
|-----|------------|--------|-----------------|
| Library | `Library` | `Library` | `libraryShortcut` |
| Wikipedia | `Wikipedia` | `Wikipedia` | `wikipediaShortcut` |
| Quick Cards | `QuickCards` | `QuickCards` | `quickCardsShortcut` |
| Screensaver | `ScreenSaver` | `ScreenSaver` | `screenSaverShortcut` |
| Sleep | `Sleep` | `Sleep` | `sleepShortcut` |
| Screen Clean | `ScreenClean` | `ScreenClean` | `screenCleanShortcut` |
| Reading Stats | `ReadingStats` | `ReadingStats` | `readingStatsShortcut` |
| Reading Heatmap | `ReadingHeatmap` | `ReadingHeatmap` | `readingHeatmapShortcut` |
| Reading Profile | `ReadingProfile` | `ReadingProfile` | `readingProfileShortcut` |
| Achievements | `Achievements` | `Achievements` | `achievementsShortcut` |
| Flashcards | `Flashcards` | `Flashcards` | `flashcardsShortcut` |
| Dictionary | `Dictionary` | `Dictionary` | `dictionaryShortcut` |
| IfFound | `IfFound` | `IfFound` | `ifFoundShortcut` |
| Bookmarks | `Bookmarks` | `Bookmarks` | `bookmarksShortcut` |
| Clippings | `Clippings` | `Clippings` | `clippingsShortcut` |
| Favorites | `Favorites` | `Favorites` | `favoritesShortcut` |
| Sync Day | `SyncDay` | `SyncDay` | `syncDayShortcut` |
| Reading Date | `ReadingDate` | `ReadingDate` | `readingDateShortcut` |
| Apps Hub | `Apps` | `Apps` | `appsShortcut` |
| Plugin Browser | `Plugins` | `Plugins` | `pluginsShortcut` |
| Lua Plugin | `LuaPlugin` | (dynamic) | — |

---

## 4. Screensaver, Sleep & Deep-Sleep (Quick Reference)

> **Full guide:** [`STEROIDS-ADDICTIONS-SCREENSAVER.md`](STEROIDS-ADDICTIONS-SCREENSAVER.md)

### 4.1 Dual-Mode Architecture
| Mode | Trigger | Config |
|------|---------|--------|
| **General** | Auto idle / Manual | `Settings → Screensaver` |
| **In-Book** | Long-press power (reading) | `Settings → Screensaver (reading)` |

### 4.2 Key Technical Differences from Upstream
| Upstream | Steroids |
|----------|----------|
| Unconditional `enterDeepSleep()` on power press | Short/long-press state machine |
| Only `SLEEP` mode works | All `shortPwrBtn` modes reachable |

### 4.3 Transparent PNG Compositing
- Snapshot framebuffer to SD before drawing
- Restore snapshot on each frame → transparent areas show original
- File-based cache (replaces 48 KB in-memory vector)
- Heap-friendly PNG decoder with `releaseDecoder()`

---

## 5. Reading Statistics (Quick Reference)

> **Full guide:** [`STEROIDS-ADDICTIONS-READING-STATS.md`](STEROIDS-ADDICTIONS-READING-STATS.md)

### 5.1 Critical Steroids Fields (Upstream Removed in 1.5.0)
```cpp
// Preserved in ReadingStatsStore for "time left" status bar
uint32_t avgSecondsPerForwardPage;  // Weighted average pace
uint32_t paceSampleCount;           // Sample count
void recordForwardPageRead(uint32_t pageDurationMs);
```

### 5.2 Home Summary Fast Path (`summary.json`)
- Avoids loading full `reading_stats.json` (~41 KB) at boot
- Home dashboard renders from small derived summary
- Written on every `markDirty()`/`saveToFile()`

### 5.3 2026-08-04 Alignment Audit (Restored from Upstream)
- `loadReadingStatsDocument` split
- Aggregate reconciliation (formatVersion ≥ 6)
- `std::stable_sort` sessionLog
- `importFromFile` rollback path
- `markLoadSkippedForRecovery()` in `loadFromFile`
- `clampPercent` in `normalizeBook`
- `loadJsonDocumentFromFile` direct load
- `maybeCreateAutoBackup` no pre-removal

---

## 6. Library V3 (Quick Reference)

> **Full guide:** [`STEROIDS-ADDICTIONS-LIBRARY.md`](STEROIDS-ADDICTIONS-LIBRARY.md)

### 6.1 Key V3 Capabilities (vs V2)
| Area | V2 | V3 |
|------|-----|-----|
| Title sort | ASCII truncation | Natural/alphanumeric (1, 2, 10) |
| Root grid | flat books OR Collections | flat, Collections, **Series + Books** |
| Series tiles | placeholder | cover of first existing cover + white-title black ribbon |
| Search | flat books only | full-text inside Series + Books |
| Filters | flat books only | applied in `queryMixed()` |
| Books in series | scan order | sorted by `seriesIndex` |
| Back from series | resets to top | returns to same page/tile |

### 6.2 Fixed-RAM Architecture
- **Page cache:** 16 `BookRef` ≈ 4 KB
- **Index files:** 28 B/record fixed-length (never fully loaded)
- **Scan:** Streaming DFS, 32 B/book RAM (prevScan + newScan)

### 6.3 Latest Fixes (2026-09-14)
- **Case-insensitive sorting** (`cmpSortKeyCI`) for all modes
- **User collection book count** from `UserCollectionsStore.memberCount()`
- **Mixed view AUTHOR sort** uses title for collections, author for books
- **Page restoration fix** — `refreshPageCache()` after restoring selector position

---

## 7. Other Enhancements (Summary)

| Enhancement | Details |
|-------------|---------|
| **Status bar time-left** | 5 modes: Hide / Chapter / Book / Session Duration / Today Total |
| **Home daily average** | Shows today's time + historical average + trend arrow |
| **Custom app icons** | Hand-crafted monochrome icons visible in all themes |
| **Boot/sleep logo** | Custom "Steroids" 350×96 logo |
| **STRING settings** | On-device support (e.g. "Library root directory") |
| **List helpers** | `ListInputMapper`, `ListLayout`, `ListRenderHelper` (~1,350 lines de-duped) |
| **Text utilities** | `text_draw::`, `text_overlay::` extracted from Sleep/Screensaver |
| **Book-Store dedup** | `BookStoreUtils.h` shared by Favorites/Recent |
| **Performance** | Inventory caching, font-decompressor lazy init, lower cover-gen guards |
| **OPDS cancellation** | Back button cancels download, cleans partial file |
| **OPDS persistent visibility** | Store reloaded on boot, shortcut stays visible |
| **Crash timestamps** | `/logs/crash_report_<YYYYMMDD_HHMMSS>.txt` + legacy |
| **OPDS fetch robustness** | Null checks, parser error returns, UI errors not crashes |
| **Per-directional buttons** | `longPressUp/Down`, `frontLongPressLeft/Right`, `shortPwrBtn` (16 options), `selectLongPressBehavior` (14 options) |
| **QR parser** | 10 formats: Wi-Fi, vCard, MeCard, Geo, Email, Phone, SMS, OTP, Calendar, URL |
| **SdCardFont 4 KiB chunks** | `miniBitmapChunks[24]`, TextGetter, CJK fallback |
| **Lua Plugin System** | Sandboxed Lua 5.4.7, 64 KB cap, 100K instruction hook, custom API |

---

## 8. Build & Verification

```powershell
# Main build
python -X utf8 -m platformio run -e default -j 16

# Release build
python -X utf8 -m platformio run -e gh_release -j 16
```

**Current Footprint:**
- **RAM:** 16.3% (53,356 B / 327,680 B)
- **Flash:** 82.2% (5,390,063 B / 6,553,600 B)

---

## 9. Related Documents

| Document | Purpose |
|----------|---------|
| `STEROIDS-ALIGN-TO-UPSTREAM.md` | Upstream merge workflow (protected files list) |
| `STEROIDS-OPTIMIZATION.md` | Shared utilities, optimization patterns, pre-merge checklist |
| `STEROIDS-ADDICTIONS-LUA.md` | Lua plugin development guide |

---

*Last updated: 2026-09-14 — Restructured into modular per-feature documents*