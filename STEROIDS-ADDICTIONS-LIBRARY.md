# CPR-vCodex Steroids — Library Module (V3 Complete Technical Reference)

> **SCOPE:** Complete technical specification for the Library subsystem (Library V3, September 2026). Covers storage layout, index pipeline, query functions, UI rendering, sorting, user collections, auto series (metadata + folder), mixed view, state persistence, and RAM constraints.

---

## 1. Architecture Overview

The library is a **fixed-RAM, on-disk index** system designed for the ESP32-C3 (320 KB RAM, no PSRAM). It never loads the full book dataset into memory. Instead:

- **`library.dat`** — 256 B/record master file (max ~1000 books = 256 KB)
- **`scan_state.dat`** — 16 B/record for incremental scan
- **Index files** — 28 B/record (title/author) or 92 B/record (collections/series), sorted for O(log N) queries
- **Page cache** — 16 `BookRef` (~4 KB) populated on-demand per page
- **Scan** — streaming DFS with incremental mtime/size comparison
- **Mixed View** — standalone books + series/collection tiles merged in single index

**RAM Budget:** ~11 KB total (page cache + sort buffer + dat vector buffer + stack + I/O)

---

## 2. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Three Root View Modes** | Switchable via settings: **Flat** (all books), **Collections** (series/user collections grid), **Mixed** (series tiles + standalone books interleaved) |
| **Flat View** | Traditional paginated grid of all books; sortable by Title, Author, Recent, Progress |
| **Collections View** | Grid of series/collection tiles; tap to enter and browse books inside sorted by `seriesIndex` |
| **Mixed View (V3)** | Single unified list: series/collection tiles (with cover + "X books" badge) interleaved with standalone books; fully searchable & filterable |
| **User Collections (Manual)** | Long-press book → "Add to Collection" → create/manage named collections; reorder books by drag position; rename/delete collections |
| **Auto Series Detection** | **EPUB metadata:** `calibre:series` + `calibre:series_index` from OPF; **Folder fallback:** parent folder as series (opt-in via `libraryFolderCollections`) |
| **Natural Title Sort** | Alphanumeric: "Book 2" before "Book 10"; works across all views |
| **Full-Text Search** | Substring match on title + author (case-insensitive, accent-normalized); in Mixed view searches inside series too |
| **Filters** | All / Favourites / Latest Read / Unread / Completed / Hidden (from ReadingStats + HiddenBooksStore) |
| **Sort Modes** | Title A-Z/Z-A, Author A-Z/Z-A, Recent, Progress; all case-insensitive |
| **Cover Generation** | On-demand thumbnails (EPUB/XTC embedded, TXT title card); collection tiles show first existing cover or placeholder |
| **Page Frame Cache** | Persistent rendered frames (~48 KB each); instant reload on revisit (0.1s vs 2-4s) |
| **State Persistence** | Remembers view mode, filter, sort, search, scroll position, opened collection across reboots |
| **CJK Font Support** | On-demand SD font loading when CJK titles detected |
| **Library Shortcuts** | Home grid / Apps: Library, Favorites (filter shortcut), Recent Books |

---

## 3. Storage Layout

```
~/.crosspoint/LIBRARY/
├── library.dat                   (256 B/record × N books)
├── scan_state.dat                (16 B/record × N books)
├── idx_title.bin                 (28 B/record, sorted by natural title key)
├── idx_author.bin                (28 B/record, sorted by author)
├── idx_collections.bin           (92 B/record, merged auto series)
├── idx_user_collections.bin      (92 B/record, manual user collections)
├── idx_folder_collections.bin    (92 B/record, folder-based collections)
├── idx_metadata_series.bin       (92 B/record, EPUB calibre:series)
├── idx_mixed.bin                 (28 B/record, V3: series tiles + standalone books)
├── series.dat                    (92 B/record, per-book series membership)
├── tmp/chunk_*.tmp               (temporary merge-sort chunks, 4 KB each)
└── lib_epoch                     (uint32_t, incremented on rebuild)
```

---

## 4. Core Data Structures

### 4.1 `LibraryIndex::Record` (256 bytes — `library.dat`)
```cpp
struct Record {
    uint32_t id;              // Stable book ID (never reassigned)
    char title[64];
    char author[48];
    char path[128];           // Absolute SD path
    uint32_t file_size;
    uint8_t flags;            // bit0=tombstone, bit1=favorite, bit2=opened, bit3=completed
    uint32_t mtime;           // File modification timestamp
    uint8_t reserved[3];
};
static_assert(sizeof(Record) == 256);
```

### 4.2 `LibraryIndex::BookRef` (260 bytes — in-RAM grid reference)
```cpp
struct BookRef {
    uint32_t id;
    char title[65];   // +1 safe null-term
    char author[49];
    char path[129];
    bool isFavorite;
    bool isOpened;
    bool isCompleted;
    bool isHidden;
    bool isCollection;  // true for series/collection tiles
    uint8_t reserved[3];
};
```

### 4.3 `LibraryIndex::IndexRec` (28 bytes — all flat indices)
```cpp
struct IndexRec {
    char sortKey[20];     // Sort key (title/author/collection)
    uint32_t bookId;      // Book ID (or 0x80000000 | collectionIdx for tiles)
    uint32_t recordOffset; // Offset in library.dat
};
static_assert(sizeof(IndexRec) == 28);
```

### 4.4 `LibraryIndex::CollectionIndexRec` (92 bytes — collection indices)
```cpp
struct CollectionIndexRec {
    char collectionName[80];
    uint32_t firstSeriesOffset;  // Byte offset into series.dat
    uint32_t bookCount;          // Number of books in this collection
    uint8_t flags;               // bit0 = user-defined collection
    uint8_t reserved[3];
};
static_assert(sizeof(CollectionIndexRec) == 92);
```

### 4.5 `LibraryIndex::SeriesRec` (92 bytes — `series.dat`)
```cpp
struct SeriesRec {
    uint32_t bookId;
    char seriesName[80];
    float seriesIndex;        // Position in series (1.0, 2.5, etc.)
    uint8_t flags;            // bit0 = folderFallback (true = folder-based)
    uint8_t reserved[3];
};
static_assert(sizeof(SeriesRec) == 92);
```

### 4.6 `LibraryIndex::ScanRec` (16 bytes — `scan_state.dat`)
```cpp
struct ScanRec {
    uint32_t pathHash;
    uint32_t mtime;
    uint32_t fileSize;
    uint32_t bookId;
};
```

---

## 5. Index Types & Constants

| Constant | Path | Purpose |
|----------|------|---------|
| `kIdxTitle` | `/.crosspoint/LIBRARY/idx_title.bin` | Title sort (natural key) |
| `kIdxAuthor` | `/.crosspoint/LIBRARY/idx_author.bin` | Author sort |
| `kIdxCollections` | `/.crosspoint/LIBRARY/idx_collections.bin` | All auto series (metadata + folder) |
| `kIdxUserCollections` | `/.crosspoint/LIBRARY/idx_user_collections.bin` | Manual user collections |
| `kIdxFolderCollections` | `/.crosspoint/LIBRARY/idx_folder_collections.bin` | Folder-based collections |
| `kIdxMetadataSeries` | `/.crosspoint/LIBRARY/idx_metadata_series.bin` | EPUB calibre:series |
| `kIdxMixed` | `/.crosspoint/LIBRARY/idx_mixed.bin` | Mixed view (series tiles + standalone books) |
| `kSeriesDat` | `/.crosspoint/LIBRARY/series.dat` | Per-book series membership |

---

## 6. Scan Pipeline (`LibraryIndex::scan()`)

### 6.1 Incremental Scan (Streaming, No Path Vector in RAM)
1. **Walk directories** — `walkDirs()` with `FileVisitor` callback (path + size from dirent)
2. **Binary search** against `scan_state.dat` (sorted by path hash) → detect new/changed/removed
3. **Parse changed files** — EPUB/TXT/XTC metadata extraction
4. **Update `library.dat`** — append new, mark deleted tombstones
5. **Rebuild indices** (`buildIndices()`) only if `added > 0 || removed > 0`

### 6.2 External Merge Sort (RAM-Efficient)
- **Chunk size:** 4 KB / 256 B = 16 records/chunk (`LIBIDX_CHUNK_RECS`)
- **K-way merge** with descending walk for reverse iteration
- **No full vectors** — only `prevScan` + `newScan` (16 B/book each = 32 B/book)
- **Dat vector buffer:** 4 × 4 KB blocks (16 KB) for cached record reads

### 6.3 Series Extraction (During Scan)
```cpp
// EPUB: calibre:series + calibre:series_index from OPF <meta>
EpubParser::extractMetadata(path, ..., &seriesName, &seriesIndex);

// Folder fallback (if SETTINGS.libraryFolderCollections):
// Parent folder name → series name; seriesIndex = 0
```

---

## 7. Index Building (`buildIndices()`)

```cpp
bool buildIndices() {
    buildTitleIndex();           // idx_title.bin (natural sort key)
    buildAuthorIndex();          // idx_author.bin
    buildCollectionsIndex();     // idx_collections.bin + user/folder/metadata
    buildMixedIndex();           // idx_mixed.bin (V3: series tiles + books)
    return true;
}
```

### 7.1 `buildCollectionsIndex()` — Auto Series Aggregation
1. **Read `series.dat`** → validate against `library.dat` (skip tombstoned)
2. **Sort by normalized series name + seriesIndex**
3. **Group into collections:**
   - **Metadata series:** group by normalized name (`cmpSortKey`)
   - **Folder series:** group by exact parent path match
4. **Write `idx_collections.bin`** (merged), plus separate:
   - `idx_metadata_series.bin` (if `libraryMetadataSeries` enabled)
   - `idx_folder_collections.bin` (if `libraryFolderCollections` enabled)
5. **Write `idx_user_collections.bin`** from `UserCollectionsStore`

### 7.2 `buildMixedIndex(SortMode)` — V3: Series Tiles + Standalone Books
1. **Collect all collection book IDs** (series + user collections) → sorted vector
2. **Phase 1a:** Emit standalone books from `library.dat` (skip if in collection IDs)
3. **Phase 1b:** Emit series/collection tiles:
   - User collections (always)
   - Metadata series (if `libraryMetadataSeries`)
   - Folder collections (if `libraryFolderCollections`)
4. **Tile ID:** `0x80000000 | collectionIndex` (high bit = isCollection)
5. **Sort:** by requested `SortMode` (TITLE_ASC/DESC, AUTHOR_ASC/DESC)
6. **External merge sort** → `idx_mixed.bin`

---

## 8. Natural Title Sort (`makeTitleSortKey()`)

```cpp
// Zero-pads digit runs to 4 digits: "Book 2" → "Book 0002", "Book 10" → "Book 0010"
// Ensures "Lightlark 2" sorts before "Lightlark 10"
void makeTitleSortKey(const char* title, char* outKey);
```

---

## 9. Query Functions

### 8.1 `queryPage()` — Normal Book Browsing
```cpp
int queryPage(BookRef* out, int page, int pageSize,
              SortMode sort, const char* search, FilterMode filter,
              int coverW, int coverH);
```
- Uses `idx_title` / `idx_author` / `idx_mixed` based on sort mode
- Search: O(N) full-text scan on normalized keys
- Filter: applied in-memory on page cache (16 items max)

### 8.2 `queryCollections()` — Auto Series Grid
```cpp
int queryCollections(BookRef* out, int page, int pageSize, int coverW, int coverH);
```
- Merges metadata + folder series based on settings
- **Cover preview:** scans collection books by `seriesIndex` for first existing cover BMP
- `ref.path` = first book with cover (or empty → placeholder)
- Subtitle = parent folder basename (for folder-fallback series)

### 8.3 `queryUserCollections()` — Manual Collections Grid
```cpp
int queryUserCollections(BookRef* out, int page, int pageSize, int coverW, int coverH);
```
- Reads `idx_user_collections.bin`
- **Accurate book count** from `UserCollectionsStore.memberCount()`
- Cover: first member book with existing thumbnail (by position)

### 8.4 `queryMixed()` — Mixed View (Series + Books)
```cpp
int queryMixed(BookRef* out, int page, int pageSize,
               const char* search, FilterMode filter,
               int coverW, int coverH, SortMode sort);
```
- Reads `idx_mixed.bin` (series tiles + standalone books)
- **In-memory sort** by requested mode (case-insensitive via `cmpSortKeyCI`)
- Filter applies to books within series (series shown if ≥1 book matches)
- Series tiles: `id & 0x80000000u`, `title` = series name, `author` = "X books"

### 8.5 `queryCollectionBooks()` — Inside a Collection
```cpp
int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx);
```
- Sorts by `seriesIndex` (numeric) inside collection
- Used when tapping a series/collection tile

### 9.6 `queryUserCollectionBooks()` — Inside User Collection
```cpp
int queryUserCollectionBooks(BookRef* out, int page, int pageSize, const char* collectionId);
```
- Sorts by `position` (float) inside user collection

### 9.7 Totals & Counts
| Function | Purpose |
|----------|---------|
| `totalBooks()` | Fast count from index header |
| `totalMatching()` | Full-text filtered count |
| `totalMixed()` / `totalMixedMatching()` | Mixed view pagination |
| `totalCollections()` / `totalUserCollections()` | Collection counts |
| `collectionBookCount(idx)` | Auto series book count |
| `userCollectionBookCount(id)` | Manual collection count (accurate) |

---

## 10. Sorting Implementation (V3 — Case-Insensitive)

### 10.1 `cmpSortKeyCI()` — Case-Insensitive Comparison
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

### 10.2 Sort Modes (All Case-Insensitive)
| Mode | Key Used | Notes |
|------|----------|-------|
| `TITLE_ASC/DESC` | `title` | Collections use collection name |
| `AUTHOR_ASC/DESC` | `author` (books) / `title` (collections) | Collections sort by name |
| `RECENT` | `lastReadAt` (ReadingStats) | Fallback to title |
| `PROGRESS` | `completed`, `progress%` | Fallback to title |
| `MIXED` | Per-request (via `queryMixed` sort param) | Default TITLE_ASC |

### 10.3 AUTHOR Sort Logic (Mixed View)
```cpp
// In queryMixed() sort lambda:
const char* authorA = a.isCollection ? a.title : a.author;
const char* authorB = b.isCollection ? b.title : b.author;
int c = cmpSortKeyCI(authorA, authorB);
```
**Key Design:** `ref.author` stays `"X books"` for display; sorting uses internal logic without overwriting display field.

---

## 11. User Collections (Manual)

### 10.1 Storage: `UserCollectionsStore` (JSON)
```json
{
  "formatVersion": 1,
  "collections": [
    { "id": "c1", "name": "My Collection", "createdAt": 1726848000 },
    { "id": "c2", "name": "Favorites", "createdAt": 1726934400 }
  ],
  "members": [
    { "collectionId": "c1", "bookId": 101, "position": 0.0 },
    { "collectionId": "c1", "bookId": 102, "position": 1.0 }
  ]
}
```

### 10.2 Management Flow
1. **Create:** Long-press book → "Add to Collection" → "New Collection"
2. **Add/Remove:** `CollectionPickerActivity` (shows membership ✓)
3. **Manage:** `CollectionManageActivity` (rename, reorder, delete books, delete collection)
4. **Persist:** JSON save on every mutation (`bumpGeneration()`)

### 11.3 Grid Display (User Collections)
- **Title:** Collection name
- **Author:** `"X books"` (accurate count from `memberCount()`)
- **Cover:** First member book with existing thumbnail (by `position` order)
- **Flags:** `flags & 1` = user collection

---

## 12. Auto Series Detection

### 12.1 Sources
1. **EPUB Metadata:** `calibre:series` + `calibre:series_index` from OPF `<meta>`
2. **Folder-based:** `libraryFolderCollections` toggle (disabled by default)

### 12.2 Indices
| Index | Source | Setting Gate |
|-------|--------|--------------|
| `idx_metadata_series.bin` | EPUB scan | `libraryMetadataSeries` |
| `idx_folder_collections.bin` | Folder structure | `libraryFolderCollections` |
| `series.dat` | Per-book series membership (seriesIndex ordering) | Both |

### 12.3 Settings Gates
```cpp
// CrossPointSettings
bool libraryMetadataSeries = true;   // calibre:series
bool libraryFolderCollections = false; // folder-based
```

---

## 13. State Persistence (`CrossPointSettings`)

### 13.1 Saved on `LibraryActivity::onExit()`
```cpp
SETTINGS.libraryViewMode        // 0=flat, 1=mixed, 2=collections
SETTINGS.libraryFilter          // ALL, FAVOURITES, LATEST_READ, UNREAD, COMPLETED, HIDDEN
SETTINGS.librarySort            // TITLE_ASC, TITLE_DESC, AUTHOR_ASC, AUTHOR_DESC, RECENT, PROGRESS, MIXED
SETTINGS.librarySearchText      // Search query
SETTINGS.librarySelectorIndex   // Absolute grid position
SETTINGS.libraryCollectionIdx   // Opened collection (-1 = none)
SETTINGS.libraryCollectionName  // Collection name for restoration
```

### 13.2 Restored on `LibraryActivity::onEnter()`
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

## 14. Cover Generation (`CoverGenerator`)

### 14.1 Pipeline
1. **Check heap guard:** `ESP.getMaxAllocHeap()` > per-format minimum
2. **Parse metadata:** EPUB cover image, XTC cover, TXT/MD title card
3. **Generate thumbnail:** `thumbPathFor(path, w, h)` → `/thumbs/<hash>_<WxH>.bmp`
4. **Dithering:** Atkinson, 4-level grayscale (shared `DitheringConfig.h`)
5. **Progressive render:** One cover per frame, triggers `requestUpdate()`

### 14.2 Collection Tile Covers
- **Never generated by grid** — only shows existing covers
- `queryCollections()`/`queryMixed()` scan for first book with `Storage.exists(thumb)`
- If none, `path` empty → placeholder with white-title black ribbon

---

## 15. Page Frame Cache (V3 Optimization)

### 15.1 Location: `/.crosspoint/libframes/fr_<sig>_<pageStart>.bin`
- **Signature:** sort/filter/search/mode/collection/layout + `libEpoch_`
- **Size:** ~48 KB raw framebuffer (1-bit)
- **Hit condition:** All covers on page exist + `pageCoversComplete()`
- **Load path:** `tryLoadPageFrame()` → restores frame + redraws overlay only
- **Speedup:** ~2-4 s full render → ~0.1 s frame load

### 15.2 Invalidation
- On `scanSd()` (indices rebuilt)
- On `pendingCollectionsRebuild_`
- On grid-affecting actions (add/remove collection, delete book)

---

## 16. Reading Stats Integration (Lazy Loading)

### 16.1 Before V3: Full store loaded on library entry (~41 KB, fragmented heap)
### 16.2 V3: Lazy Loading
- **Home/Carousel badges:** `summary.json` fast path (`getHomeBookStatsForRender`)
- **Grid filters:** `summary.json` (completed/unread flags)
- **Full store (`ReadingStatsStore`):** Only materialized for:
  - `RECENT` / `PROGRESS` sorts
  - Mark read/unread actions
  - ReadingStatsDetailActivity

---

## 17. Search & Filter

### 17.1 Search (Substring Match)
- **Normalized keys:** lowercase + accent-folded (NFD)
- **In `queryPage`:** Full scan of `library.dat` (O(N))
- **In `queryMixed`:** Applied to both books and series (series shown if ≥1 match)

### 17.2 Filters
| Filter | Logic |
|--------|-------|
| `ALL` | No filter |
| `FAVOURITES` | `isFavorite` (from FavoritesStore) |
| `LATEST_READ` | `lastReadAt` > 0 (ReadingStatsStore) |
| `UNREAD` | `!isOpened` (ReadingStatsStore) |
| `COMPLETED` | `isCompleted` (ReadingStatsStore) |
| `HIDDEN` | `isHidden` (HiddenBooksStore) |

---

## 18. CJK Font Support

### 18.1 On-Demand Loading (`SdCardFontSystem::ensureCjkFontLoaded()`)
1. Samples visible titles for CJK codepoints
2. Finds best installed SD CJK family (full coverage preferred)
3. Registers alongside reader font family
4. `GfxRenderer::resolveTextFontId()` falls back for missing glyphs

---

## 20. Build Metrics (Current)

```
Flash:  5,390,063 B / 6,553,600 B  (82.2%)
RAM:       53,356 B /   327,680 B  (16.3%)
```

---

## 21. V2 → V3 Migration Summary

| Area | V2 | V3 |
|------|-----|-----|
| Title sort | ASCII truncation | Natural/alphanumeric (1, 2, 10) |
| Root grid | flat books OR Collections | flat, Collections, **Series + Books** |
| Series tiles | placeholder | cover of first existing cover book + white-title black ribbon |
| Search | flat books only | full-text inside Series + Books |
| Filters | flat books only | applied in `queryMixed()` |
| Books in series | scan order | sorted by `seriesIndex` |
| Back from series | resets to top | returns to same page/tile |
| User collections | N/A | Full CRUD + position ordering |
| Folder collections | N/A | Parent folder as series (opt-in) |

---

## 22. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (LibraryActivity, LibraryIndex protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` §14
- **Reading Stats Integration:** `STEROIDS-ADDICTIONS-READING-STATS.md` §15
- **Home Themes:** `STEROIDS-ADDICTIONS-HOME-THEMES.md` (carousel uses same query)

---

*Last updated: 2026-09-28 — Library V3 complete with case-insensitive sorting, user collection book count fix, page restoration fix, folder collections, mixed view, external merge sort*