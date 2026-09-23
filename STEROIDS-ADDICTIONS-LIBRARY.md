# CPR-vCodex Steroids — Library Module (Complete Technical Reference)

> **SCOPE:** This document contains the complete technical specification for the Library subsystem (Library V3, September 2026). It covers storage layout, index pipeline, query functions, UI rendering, sorting, collections/series, state persistence, and RAM constraints.

---

## 1. Architecture Overview

The library is a **fixed-RAM, on-disk index** system designed for the ESP32-C3 (320 KB RAM, no PSRAM). It never loads the full book dataset into memory. Instead:

- **`library.dat`** — 256 B/record master file
- **Index files** — 28 B/record fixed-length, sorted for O(log N) queries
- **Page cache** — 16 `BookRef` (~4 KB) populated on-demand per page
- **Scan** — streaming DFS with incremental mtime/size comparison

**RAM Budget:** ~11 KB total (page cache + sort buffer + stack + I/O)

---

## 2. Storage Layout

```
/.crosspoint/LIBRARY/
├── library.dat              (256 B/record × N books)
├── scan_state.dat           (16 B/record × N books)
├── idx_title.bin            (28 B/record, sorted by natural title key)
├── idx_author.bin           (28 B/record, sorted by author)
├── idx_collections.bin      (88 B/record, merged collections)
├── idx_user_collections.bin (88 B/record, manual user collections)
├── idx_folder_collections.bin (88 B/record, folder-based collections)
├── idx_metadata_series.bin  (88 B/record, EPUB calibre:series)
├── idx_mixed.bin            (28 B/record, V3: series tiles + standalone books)
├── series.dat               (88 B/record, per-book series membership)
└── tmp/chunk_*.tmp          (temporary merge-sort chunks)
```

---

## 3. Core Data Structures

### 3.1 `LibraryIndex::Record` (256 bytes)
```cpp
struct Record {
    uint32_t id;              // Stable book ID
    char title[64];
    char author[48];
    char path[128];
    uint32_t fileSize;
    uint32_t mtime;
    // ... metadata fields (series, seriesIndex, cover, etc.)
};
```

### 3.2 `LibraryIndex::BookRef` (lightweight grid reference)
```cpp
struct BookRef {
    uint32_t id;
    char title[64];
    char author[48];
    char path[128];
    bool isFavorite, isOpened, isCompleted, isHidden;
    bool isCollection;        // true for series/collection tiles
};
```

### 3.3 `LibraryIndex::CollectionIndexRec` (88 bytes)
```cpp
struct CollectionIndexRec {
    char collectionName[48];
    uint32_t bookCount;
    uint32_t firstSeriesOffset;
    uint8_t flags;            // bit 0 = user collection (manual)
};
```

### 3.4 `LibraryIndex::IndexRec` (28 bytes — all index files)
```cpp
struct IndexRec {
    char key[20];             // Sort key (title/author/collection)
    uint32_t id;              // Book ID (or 0x80000000 | collectionIdx for tiles)
    uint32_t offset;          // Offset in library.dat
};
```

---

## 4. Index Types & Constants

| Constant | Path | Purpose |
|----------|------|---------|
| `kIdxTitle` | `/.crosspoint/LIBRARY/idx_title.bin` | Title sort (natural key) |
| `kIdxAuthor` | `/.crosspoint/LIBRARY/idx_author.bin` | Author sort |
| `kIdxCollections` | `/.crosspoint/LIBRARY/idx_collections.bin` | All collections (legacy) |
| `kIdxUserCollections` | `/.crosspoint/LIBRARY/idx_user_collections.bin` | Manual collections |
| `kIdxFolderCollections` | `/.crosspoint/LIBRARY/idx_folder_collections.bin` | Folder collections |
| `kIdxMetadataSeries` | `/.crosspoint/LIBRARY/idx_metadata_series.bin` | EPUB series |
| `kIdxMixed` | `/.crosspoint/LIBRARY/idx_mixed.bin` | Mixed view (series + books) |
| `kSeriesDat` | `/.crosspoint/LIBRARY/series.dat` | Series membership |

---

## 5. Scan Pipeline (`LibraryIndex::scan()`)

### Incremental Scan (Streaming, No Path Vector in RAM)
1. **Walk directories** — `walkDirs()` with `FileVisitor` callback (path + size from dirent)
2. **Binary search** against `scan_state.dat` (sorted by path hash) → detect new/changed/removed
3. **Parse changed files** — EPUB/TXT/XTC metadata extraction
4. **Update `library.dat`** — append new, mark deleted tombstones
5. **Rebuild indices** (`buildIndices()`) only if `added > 0 || removed > 0`

### External Merge Sort (RAM-Efficient)
- **Chunk size:** 4 KB / 256 B = 16 records/chunk (`LIBIDX_CHUNK_RECS`)
- **K-way merge** with descending walk for reverse iteration
- **No full vectors** — only `prevScan` + `newScan` (16 B/book each = 32 B/book)

---

## 6. Index Building (`buildIndices()`)

```cpp
// Called by scan() when library contents changed
void buildIndices() {
    buildTitleIndex();        // idx_title.bin (natural sort key)
    buildAuthorIndex();       // idx_author.bin
    buildCollectionsIndex();  // idx_collections.bin + user/folder/metadata
    buildMixedIndex();        // idx_mixed.bin (V3: series tiles + books)
    buildSeriesDat();         // series.dat
}
```

### 6.1 `buildMixedIndex()` (V3 — Series + Books)
```cpp
// Merges library.dat + idx_collections.bin into idx_mixed.bin
// Output: one IndexRec per standalone book + one per series/collection tile
// Tile ID uses high bit: 0x80000000u | collectionIndex
// Both sorted by natural title key (makeTitleSortKey)
```

### 6.2 Natural Title Sort (`makeTitleSortKey()`)
```cpp
// Zero-pads digit runs to 4 digits: "Book 2" → "Book 0002", "Book 10" → "Book 0010"
// Ensures "Lightlark 2" sorts before "Lightlark 10"
void makeTitleSortKey(const char* title, char* outKey);
```

---

## 7. Query Functions

### 7.1 `queryPage()` — Normal Book Browsing
```cpp
int queryPage(BookRef* out, int page, int pageSize,
              SortMode sort, const char* search, FilterMode filter,
              int coverW, int coverH);
```
- Uses `idx_title` / `idx_author` / `idx_mixed` based on sort mode
- Search: O(N) full-text scan on normalized keys
- Filter: applied in-memory on page cache (16 items max)

### 7.2 `queryCollections()` — Collections Grid
```cpp
int queryCollections(BookRef* out, int page, int pageSize, int coverW, int coverH);
```
- Merges user + metadata + folder collections based on settings
- **Accurate book count** from `UserCollectionsStore.memberCount()` for user collections
- **Cover preview:** scans collection books by `seriesIndex` for first existing cover BMP
- `ref.path` = first book with cover (or empty → placeholder)

### 7.3 `queryMixed()` — Mixed View (Serie + Libri)
```cpp
int queryMixed(BookRef* out, int page, int pageSize,
               const char* search, FilterMode filter,
               int coverW, int coverH, SortMode sort);
```
- Reads `idx_mixed.bin` (series tiles + standalone books)
- **In-memory sort** by requested mode (case-insensitive via `cmpSortKeyCI`)
- Filter applies to books within series (series shown if ≥1 book matches)
- Series tiles: `id & 0x80000000u`, `title` = series name, `author` = "X books"

### 7.4 `queryCollectionBooks()` — Inside a Collection
```cpp
int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx);
```
- Sorts by `seriesIndex` (numeric) inside collection
- Used when tapping a series/collection tile

### 7.5 `totalMixedMatching()` / `totalMatching()` / `totalCollections()`
- Count matching entries for header pagination
- Search/filter applied during count

### 7.6 `collectionBookCount()` — Accurate Count
```cpp
int collectionBookCount(int collectionIdx);
```
- Returns `USER_COLLECTIONS.memberCount()` for user collections
- Returns `ci.bookCount` for auto series/folder collections

---

## 8. Sorting Implementation (V3 — Case-Insensitive)

### 8.1 `cmpSortKeyCI()` — Case-Insensitive Comparison
```cpp
static int cmpSortKeyCI(const char* a, const char* b) {
    for (int i = 0; i < 20; ++i) {
        unsigned char ca = std::tolower(static_cast<unsigned char>(a[i]));
        unsigned char cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
    return 0;
}
```

### 8.2 Sort Modes (All Case-Insensitive)
| Mode | Key Used | Notes |
|------|----------|-------|
| `TITLE_ASC/DESC` | `title` | Collections use collection name |
| `AUTHOR_ASC/DESC` | `author` (books) / `title` (collections) | Collections sort by name |
| `RECENT` | `lastReadAt` (ReadingStats) | Fallback to title |
| `PROGRESS` | `completed`, `progress%` | Fallback to title |

### 8.3 AUTHOR Sort Logic (Mixed View)
```cpp
// In queryMixed() sort lambda:
const char* authorA = a.isCollection ? a.title : a.author;
const char* authorB = b.isCollection ? b.title : b.author;
int c = cmpSortKeyCI(authorA, authorB);
```

**Key Design:** `ref.author` stays `"X books"` for display; sorting uses internal logic without overwriting display field.

---

## 9. User Collections (Manual)

### Storage: `UserCollectionsStore` (JSON)
```json
{
  "formatVersion": 1,
  "collections": [
    { "id": 1, "name": "My Collection", "members": [101, 102, 103] }
  ],
  "nextId": 2
}
```

### Management Flow
1. **Create:** Long-press book → "Add to Collection" → "New Collection"
2. **Add/Remove:** CollectionPickerActivity (shows membership ✓)
3. **Manage:** CollectionManageActivity (rename, reorder, delete books, delete collection)
4. **Persist:** JSON save on every mutation (`bumpGeneration()`)

### Grid Display (User Collections)
- **Title:** Collection name
- **Author:** `"X books"` (accurate count from `memberCount()`)
- **Cover:** First member book with existing thumbnail (by `seriesIndex` order)
- **Flags:** `flags & 1` = user collection

---

## 10. Auto Series Detection

### Sources
1. **EPUB Metadata:** `calibre:series` + `calibre:series_index`
2. **Folder-based:** `libraryFolderCollections` toggle (disabled by default)

### Indices
- `idx_metadata_series.bin` — from EPUB scan
- `idx_folder_collections.bin` — from folder structure (when enabled)
- `series.dat` — per-book series membership (seriesIndex ordering)

---

## 11. State Persistence (`CrossPointSettings`)

### Saved on `LibraryActivity::onExit()`
```cpp
SETTINGS.libraryViewMode        // 0=flat, 1=mixed, 2=collections
SETTINGS.libraryFilter          // ALL, FAVORITES, UNREAD, COMPLETED, HIDDEN
SETTINGS.librarySort            // TITLE_ASC, TITLE_DESC, AUTHOR_ASC, AUTHOR_DESC, RECENT, PROGRESS, MIXED
SETTINGS.librarySearchText      // Search query
SETTINGS.librarySelectorIndex   // Absolute grid position
SETTINGS.libraryCollectionIdx   // Opened collection (-1 = none)
SETTINGS.libraryCollectionName  // Collection name for restoration
```

### Restored on `LibraryActivity::onEnter()`
```cpp
// 1. Scan SD (incremental, rebuilds indices if needed)
scanSd();

// 2. Restore view/filter/sort/search
currentFilter_ = SETTINGS.libraryFilter;
currentSort_ = SETTINGS.librarySort;
currentSearchText_ = SETTINGS.librarySearchText;

// 3. Restore selector position
selectorIndex_ = SETTINGS.librarySelectorIndex;

// 4. Restore opened collection
if (SETTINGS.libraryCollectionIdx >= 0) {
    USER_COLLECTIONS.ensureLoaded();
    const UserCollection* uc = USER_COLLECTIONS.findCollectionByName(...);
    if (uc) { currentCollectionIdx_ = ...; }
}

// 5. CRITICAL: Refresh page cache for restored position
refreshPageCache();  // FIXED: ensures correct page rendered on open

// 6. Rebuild totals
totalBooks_ = ...;
totalPages_ = ...;
```

---

## 12. Cover Generation (`CoverGenerator`)

### Pipeline
1. **Check heap guard:** `ESP.getMaxAllocHeap()` > per-format minimum
2. **Parse metadata:** EPUB cover image, XTC cover, TXT/MD title card
3. **Generate thumbnail:** `thumbPathFor(path, w, h)` → `/thumbs/<hash>_<WxH>.bmp`
4. **Dithering:** Atkinson, 4-level grayscale (shared `DitheringConfig.h`)
4. **Progressive render:** One cover per frame, triggers `requestUpdate()`

### Collection Tile Covers
- **Never generated by grid** — only shows existing covers
- `queryCollections()`/`queryMixed()` scan for first book with `Storage.exists(thumb)`
- If none, `path` empty → placeholder with white-title black ribbon

---

## 13. Page Frame Cache (V3 Optimization)

### Location: `/.crosspoint/libframes/fr_<sig>_<pageStart>.bin`
- **Signature:** sort/filter/search/mode/collection/layout + `libEpoch_`
- **Size:** ~48 KB raw framebuffer (1-bit)
- **Hit condition:** All covers on page exist + `pageCoversComplete()`
- **Load path:** `tryLoadPageFrame()` → restores frame + redraws overlay only
- **Speedup:** ~2-4 s full render → ~0.1 s frame load

### Invalidation
- On `scanSd()` (indices rebuilt)
- On `pendingCollectionsRebuild_`
- On grid-affecting actions (add/remove collection, delete book)

---

## 14. Reading Stats Integration (Lazy Loading)

### Before V3: Full store loaded on library entry (~41 KB, fragmented heap)
### V3: Lazy Loading
- **Home/Carousel badges:** `summary.json` fast path (`getHomeBookStatsForRender`)
- **Grid filters:** `summary.json` (completed/unread flags)
- **Full store (`ReadingStatsStore`):** Only materialized for:
  - `RECENT` / `PROGRESS` sorts
  - Mark read/unread actions
  - ReadingStatsDetailActivity

---

## 15. CJK Font Support

### On-Demand Loading (`SdCardFontSystem::ensureCjkFontLoaded()`)
1. Samples visible titles for CJK codepoints
2. Finds best installed SD CJK family (full coverage preferred)
3. Registers alongside reader font family
4. `GfxRenderer::resolveTextFontId()` falls back for missing glyphs

---

## 16. Search & Filter

### Search (Substring Match)
- **Normalized keys:** lowercase + accent-folded (NFD)
- **In `queryPage`:** Full scan of `library.dat` (O(N))
- **In `queryMixed`:** Applied to both books and series (series shown if ≥1 match)

### Filters
| Filter | Logic |
|--------|-------|
| `ALL` | No filter |
| `FAVORITES` | `isFavorite` (from FavoritesStore) |
| `UNREAD` | `!isOpened` (ReadingStatsStore) |
| `COMPLETED` | `isCompleted` (ReadingStatsStore) |
| `HIDDEN` | `isHidden` (HiddenBooksStore) |

---

## 17. Key Files

| File | Role |
|------|------|
| `src/components/LibraryIndex.h` | Public API, structs, constants, SortMode/FilterMode enums |
| `src/components/LibraryIndex.cpp` | Scan, index build, query functions, sorting |
| `src/activities/apps/LibraryActivity.h` | UI state, view modes, collection navigation |
| `src/activities/apps/LibraryActivity.cpp` | Rendering, input, page cache, frame cache, state persistence |
| `src/components/UserCollectionsStore.h` | Manual collections JSON store |
| `src/components/CrossPointSettings.h` | Settings persistence fields |
| `src/components/EpubParser.h` | EPUB metadata (series, cover) |
| `src/util/CoverGenerator.h` | Cover thumbnail generation |
| `src/ReadingStatsStore.h` | Reading stats for RECENT/PROGRESS sort |

---

## 18. Build Metrics (Current)

```
Flash:  5,390,063 B / 6,553,600 B  (82.2%)
RAM:       53,356 B /   327,680 B  (16.3%)
```

---

## 19. V2 → V3 Migration Summary

| Area | V2 | V3 |
|------|-----|-----|
| Title sort | ASCII truncation | Natural/alphanumeric (1, 2, 10) |
| Root grid | flat books OR Collections | flat, Collections, **Series + Books** |
| Series tiles | placeholder | cover of first existing cover book + white-title black ribbon |
| Search | flat books only | full-text inside Series + Books |
| Filters | flat books only | applied in `queryMixed()` |
| Books in series | scan order | sorted by `seriesIndex` |
| Back from series | resets to top | returns to same page/tile |

---

## 20. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (LibraryActivity, LibraryIndex protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` §14
- **Lua Plugins:** `STEROIDS-ADDICTIONS-LUA.md` (unrelated)
- **Wikipedia:** `STEROIDS-ADDICTIONS-WIKIPEDIA.md` (unrelated)

---

*Last updated: 2026-09-14 — Library V3 complete with case-insensitive sorting, user collection book count fix, page restoration fix*