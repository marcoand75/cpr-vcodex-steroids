# CPR-vCodex Steroids — Reading Statistics (Streaming Architecture)

> **SCOPE:** Complete technical reference for the streaming reading stats system: incremental JSON loader, binary journal for detached sessions, `reading_stats_summary.json` fast path, import/export streaming, lazy loading integration.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Reading Streak** | Consecutive days with reading ≥ daily goal (configurable) |
| **Daily Goal** | User-set target (default 15 min), drives streak calculation |
| **Per-Book Stats** | Total time, sessions, progress %, first/last read, chapter progress |
| **Achievements** | Milestone-based (books finished, streak days, total hours) |
| **Manual Correction** | Adjust reading time per day (Books → Stats → Edit) |
| **Import/Export** | JSON file (51 KB typical), streaming — never loads full file in RAM |
| **Web Editor** | Browser-based stats editor at `/stats-editor` |

---

## 2. Technical Architecture

### 2.1 Storage Format (SD Only)
```
~/.crosspoint/
├── reading_stats.json        (51 KB — canonical JSON, formatVersion 6)
├── reading_stats_summary.json (~6 KB — compact index for Home/UI)
└── reading_sessions.jrn       (48 B V2 records, 4-byte "RSJ2" header — binary journal for detached sessions)
```

**No binary `.pack` file** — removed (didn't reduce RAM; bottleneck was resident store ~50 KB).

### 2.2 Data Structures

#### `ReadingStatsStore` (Resident when loaded)
```cpp
struct ReadingBookStats {
    std::string bookId, path;
    std::vector<std::string> knownPaths;
    std::string title, author, coverBmpPath, chapterTitle;
    uint64_t totalReadingMs = 0;
    uint32_t sessions = 0;
    uint32_t lastSessionMs = 0;
    uint32_t firstReadAt = 0, lastReadAt = 0, completedAt = 0;
    uint8_t lastProgressPercent = 0, chapterProgressPercent = 0;
    bool completed = false;
    std::vector<ReadingDayStats> readingDays;  // per-book daily
};

struct ReadingDayStats {
    uint32_t dayOrdinal;
    uint64_t readingMs;
};

struct ReadingSessionLogEntry {
    uint32_t dayOrdinal;
    uint64_t sessionMs;
    int16_t bookIndex;        // -1 = orphan (path/title stored inline)
    std::string path, title;  // only when bookIndex == -1
};
```

#### `SummaryJSON` (Loaded at boot, ~6 KB)
```cpp
struct BookBadge {
    std::string bookId, path;
    bool completed = false;
    uint8_t progressPercent = 0;
    uint64_t totalReadingMs = 0;
    uint32_t sessions = 0;
    uint32_t readingDaysCount = 0;
};

struct GlobalSummary {
    uint64_t totalReadingMs, todayReadingMs, recent7ReadingMs, recent30ReadingMs;
    uint32_t currentStreakDays, maxStreakDays;
    uint32_t booksFinishedCount;
    uint64_t goalReadingMs, dailyAverageMs;
    uint32_t referenceDayOrdinal;
};
```

### 2.3 RAM Budget

| State | Free Heap | Max Alloc | Notes |
|-------|-----------|-----------|-------|
| Boot (no store) | ~112 KB | ~90 KB | Only reading_stats_summary.json loaded |
| Store loaded | ~68 KB | ~50 KB | Full vectors materialized |
| Import streaming | ~78 KB | ~61 KB | 1 KB buffer + compact day vector |
| Network release | ~107 KB | ~86 KB | Store cleared, summary kept |

---

## 3. Core Pipelines

### 3.1 Boot Loading (`preloadHomeSummary()`)
```cpp
// main.cpp:1322
READING_STATS.preloadHomeSummary();
// → loadSummaryJSON() reads reading_stats_summary.json (6 KB)
// → summaryJsonValid_ = true
// → loaded_ = false (fat store NOT materialized)
```
**Gate:** `preloadHomeSummary()` runs at boot (`main.cpp`); the summary read itself is small, while the full store load is deferred below 40 KB of largest free block (see 3.2).

### 3.2 Lazy Full Load (`ensureLoaded()`)
Triggered by:
- ReadingStatsDetailActivity / ReadingStatsActivity
- End of reading session (`endSession()`)
- Import/Export
- RECENT/PROGRESS sort in Library

```cpp
bool ReadingStatsStore::ensureLoaded() {
    if (loaded_) { /* merge pending journal, save summary */ return true; }
    if (!boot_load_gate::ready()) return false;
    return loadFromFile();  // streaming two-pass loader; defers below 40 KB maxA
}
```

### 3.3 Two-Pass Streaming Loader (`ReadingStatsStreamingLoader.cpp`)
**Pass 1:** Scan days + books (skip sessionLog with `skipValue()`)
**Pass 2:** Parse sessionLog, resolve identities via `bookIndex`

```cpp
// Prenotations capped by file size:
booksEstimate = min(48, max(8, size/1400));
daysEstimate  = min(512, max(16, size/650));
sessionsEstimate = min(160, max(16, size/400));
```
**Buffer:** 1 KB (`kReadStreamBytes`)
**No `shrink_to_fit()`** — caused abort (realloc 9 KB with max block 6.5 KB)

### 3.4 Session Identity Interning
```cpp
// ReadingSessionLogEntry.bookIndex (int16_t, -1 = orphan)
1. Pass 1: collect books, assign indices
2. Pass 2: resolve session bookId→index, path→index
3. Orphan sessions: materialize path/title strings inline
4. On book removal: remap orphans, materialize strings
```

### 3.5 Binary Journal (`reading_sessions.jrn`, 48 B V2 records)
```text
File: [u32 magic "RSJ2" (0x52534A32)] + N × 48-byte records
Rec:  [u32 dayOrdinal][u32 sessionMs][u8 progressPercent][u8 completed]
      [u8 bookId[32]][u8 reserved[6]]
```
The full 32-byte KOReader bookId hash is stored verbatim (no truncation).
`detectJournalFormat()` also accepts legacy headerless V1 records (32 B, 16-byte id)
and V2 files written before the `O_CREAT` magic fix (48 B records, zero padding at [42..47]).
**Flow:**
1. `beginSession()` with `!loaded_` → detached session (no `ensureLoaded()`)
2. `noteActivity()` / `tickActiveSession()` / `endSession()` → append to journal
3. `updateSummaryFromJournal()` folds only unapplied bytes (`journalAppliedBytes`) into the summary
4. `mergeSessionJournal()` (full load, import, next `ensureLoaded()`) folds records into
   books + sessionLog + days, then removes the journal file

### 3.6 Summary Regeneration (`saveSummaryJSON()`)
Called on every store modification (`markDirty()`):
- Aggregates: today/7d/30d, streak, maxStreak, finished count
- Emits `BookBadge` for each book (enriched: totalReadingMs, sessions, readingDaysCount)
- Streaming write via `JsonStreamWriter` (no full JsonDocument)

---

## 4. Import/Export Streaming

### 4.1 Export (`exportToFile()`)
- Streams `reading_stats.json` → temp file → verified rename
- No RAM materialization

### 4.2 Import (`importFromFile()` → `ReadingStatsImportStreaming`)
```cpp
// 1. Release resident store (free ~40 KB)
releaseMemoryForNetwork();

// 2. Validate structure (skipValue, count arrays)
validateStatsStreaming(sourcePath, counters);

// 3. Copy source → target (byte-count verified)
copyWithVerification(source, target, sourceSize);

// 4. Regenerate reading_stats_summary.json from copied file (streaming)
parseReadingDayArray() + parseBooksSummaryLite() + aggregate

// 5. Invalidate in-memory state
loaded_ = false;
summaryJsonValid_ = false;
invalidateSummaryCache();
```

**Peak RAM:** ~20 KB (1 KB buffer + compact vectors + summary doc)

---

## 5. Home/UI Integration (Lazy Loading)

### 5.1 Fast Path (No Store Loaded)
```cpp
// ReadingStatsStore::getHomeBookStatsForRender()
if (!loaded_) {
    const auto& summary = getSummaryJSON();  // loads reading_stats_summary.json if needed
    for (badge : summary.bookBadges) match by bookId/path
    return synthesized ReadingBookStats from badge;
}
```

### 5.2 HomeActivity Reload Fix
```cpp
// HomeActivity::onEnter()
if (!READING_STATS.isSummaryValid()) {
    READING_STATS.preloadHomeSummary();  // reloads reading_stats_summary.json after import
}
```

### 5.3 ReadingStatsDetailActivity
```cpp
// Always ensureLoaded() for per-book detail
READING_STATS.ensureLoaded();
const auto* book = findBook(bookPath);  // works with full store
```

---

## 6. File Inventory

| File | Role |
|------|------|
| `src/ReadingStatsStore.h/cpp` | Core store, lazy loading, journal, summary, import/export |
| `src/util/ReadingStatsStreamingLoader.h/cpp` | Two-pass incremental JSON parser |
| `src/util/ReadingStatsImportStreaming.h/cpp` | Streaming import (validate+copy+summary regen) |
| `src/util/ReadingStatsAnalytics.h/cpp` | Streak, goals, achievements calculations |
| `src/JsonSettingsIO.cpp` | JSON writer/reader (identity resolution, summary emit) |
| `src/activities/apps/ReadingStatsDetailActivity.cpp` | Per-book detail UI |
| `src/activities/apps/ReadingStatsActivity.cpp` | Global stats screen |
| `src/activities/settings/SettingsActivity.cpp` | Import/Export UI, achievement rebuild |
| `src/main.cpp` | Boot preload hook |

---

## 7. Upstream Merge Notes

### Protected (Do Not Auto-Merge)
- `ReadingStatsStore` — Complete rewrite with streaming, journal, lazy loading
- `ReadingStatsStreamingLoader` — New incremental parser
- `ReadingStatsImportStreaming` — New streaming import pipeline
- `JsonSettingsIO` save/load for reading stats — identity resolution + summary emit

### Safe Cherry-Picks
- `saveSummaryJSON()` streaming write pattern
- `getHomeBookStatsForRender()` fast path design
- `releaseMemoryForNetwork()` pattern for heap management
- Journal concept for detached sessions

### Conflicts Likely
- `ReadingStatsStore` API surface (new methods: `isSummaryValid`, `preloadHomeSummary`, journal methods)
- `ReadingSessionLogEntry` struct (added `bookIndex`)
- Boot loading gate logic

---

## 8. Validation Checklist

- [ ] Import 51 KB JSON → completes at any heap state (tested: 78 KB free, 61 KB maxA)
- [ ] Post-import Home panels show updated progress % without opening stats screen
- [ ] Detached session (≥3 min read from cold boot) → journal written → merged on next load
- [ ] ReadingStatsDetail from context menu loads full store, shows all metrics
- [ ] Export → Import roundtrip preserves all data (streaks, per-book days, sessions)
- [ ] Web editor at `/stats-editor` loads/saves correctly
- [ ] Achievements rebuild after import (`ACHIEVEMENTS.rebuildProgressFromCurrentStats()`)

---

## 9. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-LIBRARY.md` — Library integration (lazy loading)
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — Reader exit releases store
- `STEROIDS-OPTIMIZATION.md` — RAM/Flash optimization details

---

*Last updated: 2026-09-28 | Commit: 1ae55a93 (import streaming fixes + home reload + detail load)*