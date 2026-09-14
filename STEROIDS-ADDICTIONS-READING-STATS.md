# CPR-vCodex Steroids — Reading Statistics, Heatmap & Profile

> **SCOPE:** Complete technical reference for reading statistics, pace tracking, heatmap, and profile subsystems.

---

## 1. Architecture Overview

```
ReadingStatsStore (singleton, JSON)  →  Core persistence
       │
       ├─→ summary.json (fast path for Home/Carousel)
       ├─→ ReadingStatsActivity (main stats screen)
       ├─→ ReadingStatsDetailActivity (per-book detail)
       ├─→ ReadingStatsExtendedActivity (extended view)
       ├─→ ReadingHeatmapActivity (calendar heatmap)
       ├─→ ReadingProfileActivity (pace/settings)
       └─→ AchievementsActivity
```

---

## 2. Data Model (`ReadingStatsStore`)

### 2.1 Per-Book Record
```cpp
struct BookStats {
    uint32_t bookId;                    // Stable book ID
    std::string path;                   // Book path (for identity)
    uint32_t totalReadingMs;            // Total time reading
    uint32_t lastReadAt;                // Unix timestamp
    uint8_t lastProgressPercent;        // 0-100
    bool completed;                     // Finished reading
    uint32_t sessionCount;              // Number of sessions
    uint32_t avgSecondsPerForwardPage;  // **Steroids: pace tracking**
    uint32_t paceSampleCount;           // **Steroids: pace tracking**
    // Reading days (per-day aggregates)
    std::vector<ReadingDay> days;
};
```

### 2.2 Steroids Pace Fields (Preserved from Upstream Removal)
```cpp
// Upstream 1.5.0 removed these — Steroids preserves for "time left" status bar
uint32_t avgSecondsPerForwardPage;  // Weighted average
uint32_t paceSampleCount;           // Number of samples

// Implementation:
void recordForwardPageRead(uint32_t pageDurationMs) {
    // Weighted moving average
    avg = (avg * count + pageDurationMs) / (count + 1);
    count++;
}
```

### 2.3 Reading Day Aggregate
```cpp
struct ReadingDay {
    int dayOrdinal;          // Days since epoch
    uint32_t readingMs;      // Total ms this day
    uint32_t sessionCount;
    // ... per-book breakdown
};
```

---

## 3. Home Summary Fast Path (`summary.json`)

### 3.1 Purpose
Avoid loading full `reading_stats.json` (~41 KB) at boot. Home dashboard renders from small derived summary.

### 3.2 Structure
```json
{
  "formatVersion": 1,
  "global": {
    "totalReadingMs": 123456789,
    "totalDays": 42,
    "dailyAverageMs": 8500000,
    "streakDays": 7
  },
  "books": [
    { "bookId": 101, "progressPercent": 45, "completed": false, "lastReadAt": 1699999999 }
  ]
}
```

### 3.3 Accessors
```cpp
// Fast path (no full store load)
SummaryJSON getSummaryJSON();              // Global panel values
BookProgress getBookProgressForHome(id);   // Carousel progress badges
BookHomeStats getBookHomeStats(id);        // Read ribbon, long-press menu
preloadHomeSummary();                      // Called from BootActivity
```

### 3.4 Persistence
- Written on every `markDirty()` / `saveToFile()`
- One-shot migration from old store on first boot
- Upgrade path generates from existing store

---

## 4. Reading Heatmap

### 4.1 `ReadingHeatmapActivity`
- Calendar view (month grid)
- Color intensity = reading time per day
- Navigation: month/year
- Data from `ReadingStatsStore::getReadingDays()`

### 4.2 Data Source
```cpp
std::vector<ReadingDay> getReadingDays();
// Aggregated per-day readingMs from all books
```

---

## 5. Reading Profile / Pace

### 5.1 `ReadingProfileActivity`
- Target daily reading time
- Pace estimation (avgSecondsPerForwardPage)
- Session duration tracking
- Goal progress

### 5.2 Pace Estimation
```cpp
// "Time left" in status bar:
remainingMs = (pagesRemaining * avgSecondsPerForwardPage) * 1000;
```

---

## 6. Achievements

### 6.1 `AchievementsActivity`
- Milestone-based (books read, days streak, time milestones)
- Defined in `AchievementsStore` (JSON)
- Toast notifications on unlock (`AchievementPopupUtils`)

---

## 7. Manual Reading Corrections

### 7.1 Activities
| Activity | Purpose |
|----------|---------|
| `SyncDayActivity` | Daily goal sync |
| `ManualDateActivity` | Manual date entry |
| `ReadingDateSelectionActivity` | Calendar picker |
| `ReadingDayDetailActivity` | Per-day breakdown |
| `BookReadingAdjustmentActivity` | Adjust book reading time |
| `BookStatsActionsActivity` | Mark read/unread, reset stats |

---

## 8. Key Files

| File | Role |
|------|------|
| `src/ReadingStatsStore.h/cpp` | Core store + pace + summary.json |
| `src/activities/apps/ReadingStatsActivity.h/cpp` | Main stats screen |
| `src/activities/apps/ReadingStatsDetailActivity.h/cpp` | Per-book detail |
| `src/activities/apps/ReadingStatsExtendedActivity.h/cpp` | Extended view |
| `src/activities/apps/ReadingHeatmapActivity.h/cpp` | Calendar heatmap |
| `src/activities/apps/ReadingProfileActivity.h/cpp` | Profile/pace settings |
| `src/activities/apps/AchievementsActivity.h/cpp` | Achievements browser |
| `src/activities/apps/SyncDayActivity.h/cpp` | Daily goal sync |
| `src/activities/apps/ReadingDateSelectionActivity.h/cpp` | Date picker |
| `src/activities/apps/BookReadingAdjustmentActivity.h/cpp` | Manual corrections |
| `src/util/ReadingStatsBackupManager.h/cpp` | Backup/export/import paths |

---

## 9. Upstream Alignment (Critical)

### Upstream 1.5.0 Removed (Steroids Preserves)
- `avgSecondsPerForwardPage`
- `paceSampleCount`
- `recordForwardPageRead()`
- `mergeBookInto()` pace data merge

### 2026-08-04 Full Alignment Audit (Restored from Upstream)
- `loadReadingStatsDocument` function split
- Aggregate reconciliation block (formatVersion ≥ 6)
- `std::stable_sort` of `sessionLog`
- `importFromFile` rollback path
- `markLoadSkippedForRecovery()` in `loadFromFile`
- `clampPercent` in `normalizeBook`
- `loadJsonDocumentFromFile` for direct JSON load
- `maybeCreateAutoBackup` no pre-removal

### Steroids-Only Features (Always Preserve)
- Pace tracking fields + serialization
- `recordForwardPageRead()` weighted average
- `SummaryJSON` + `summary.json` fast path
- `dailyAverageMs` for Home trend indicator
- `.reserve()` fragmentation fixes
- Meyers' Singleton pattern

---

## 10. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (ReadingStatsStore critical)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` §12.4
- **Library:** `STEROIDS-ADDICTIONS-LIBRARY.md` (lazy stats integration)

---

*Last updated: 2026-09-14*