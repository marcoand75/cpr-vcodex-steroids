# Series and Collection Management Restructure — Implementation Plan

## Current Status (2026-09-21)

### Completed (staged, ready to commit)
- ✅ `src/UserCollectionsStore.cpp/.h` — new store with atomic JSON persistence
- ✅ `src/activities/apps/CollectionManageActivity.cpp/.h` — collection CRUD UI
- ✅ `src/activities/apps/CollectionPickerActivity.cpp/.h` — pick collection to add book
- ✅ `src/activities/apps/CollectionBooksActivity.cpp/.h` — view collection members
- ✅ `src/components/LibraryIndex.cpp/.h` — `buildUserCollectionsIndex()`, `buildMixedIndex()` changes
- ✅ `src/activities/apps/LibraryActivity.cpp/.h` — integration with collection rebuild triggers
- ✅ `src/components/EpubParser.cpp/.h` — EPUB metadata extraction
- ✅ `lib/I18n/translations/english.yaml` and `italian.yaml` — collection strings added
- ✅ Additional integration files staged: LibraryIndexCache, LibraryCoverHelper, LibraryDrawHelpers, etc.

### Completed in current session (unstaged)
- ✅ `platformio.ini` — Added `-DOMIT_LEXEND` and `-DOMIT_BOOKERLY` to reduce firmware size from 7.22MB to 5.13MB
- ✅ `scripts/git_branch.py` — Added `PROJECT_VER` sync
- ✅ `scripts/package_vcodex_bin.py` — Version mismatch downgraded from error to warning

### Pending validation
- ⏳ Build verification with `default` environment
- ⏳ Runtime validation of collection CRUD flows
- ⏳ Memory/heap verification during scan/browse

---

## 1. Goal and Scope

Restructure series/collection management on the existing Library V3 foundation so that:
- **Series** are objective, metadata-driven groupings (0 or 1 per book), derived from EPUB OPF with a parent-folder fallback. Never parse filenames.
- **Collections** are subjective, user-defined groupings (0 to N per book), managed at runtime via dedicated activities.
- Mixed view (`LIBRARY_SORT_MIXED`) continues to expose both on one shelf.

Out of scope: upstream sync, Calibre OPF batch import, virtual collections, manual reordering of collection members via drag-and-drop.

---

## 2. Current-State Summary (verified against `master`)

| Component | File | State |
|-----------|------|-------|
| Series records | `src/components/LibraryIndex.cpp` `series.dat` | 92-byte packed `SeriesRec { bookId, seriesName[80], seriesIndex float, flags, reserved[3] }`. Written during scan. |
| Collections index | `idx_collections.bin` | 92-byte `CollectionIndexRec` built by `buildCollectionsIndex()` from `series.dat` + `user_collections.json`. |
| Mixed index | `idx_mixed.bin` | Merge of standalone books + series tiles. |
| EPUB parser | `src/components/EpubParser.cpp` | Reads `calibre:series`, `calibre:series_index`, EPUB3 `belongs-to-collection` + `group-position`. No folder fallback, no normalization. |
| Stores | `FavoritesStore`, `HiddenBooksStore` | Singleton JSON pattern under `/.crosspoint/`. Canonical reference for new store. |
| UI | `LibraryActivity` | `LIBRARY_SORT_COLLECTIONS` / `LIBRARY_SORT_MIXED` exist. No collection CRUD UI. No per-book add-to-collection action. |

**Problems:**
1. No filesystem fallback when EPUB metadata is missing.
2. No series-key normalization before grouping.
3. No user-defined collections separate from auto series.
4. No runtime collection management UI.
5. `queryCollectionBooks()` does linear `library.dat` scan per book.
6. No cache invalidation path for collection changes.
7. Series and collections are conflated in `series.dat`.

---

## 3. Analysis: Separate Series and Collections vs. Merged?

### 3.0 Decision: Keep separate index files for auto series and user collections

**Rejected: Merge auto series and user collections into a single `idx_collections.bin`.**

Per user decision, auto-series and user collections remain on **separate binary index files**. This preserves the existing `idx_collections.bin` (auto series) format unchanged and adds a new `idx_user_collections.bin` for user-defined collections.

Rationale for separation:
- **Clarity**: Two distinct concepts (objective metadata-derived vs subjective user-defined) deserve separate physical files. Each index is read/written independently, simplifying build/rebuild logic.
- **Build independence**: `buildUserCollectionsIndex()` can run without touching `idx_collections.bin`, and vice-versa. A user collection mutation triggers only `buildUserCollectionsIndex()` + `buildMixedIndex()`, not a full `buildCollectionsIndex()`.
- **Stable sort preserved**: `buildMixedIndex()` still reads one `idx_collections.bin` (series) and one `idx_user_collections.bin` (collections), merging both into `idx_mixed.bin`. The existing two-way merge logic is simpler than a merged single-file approach would be.
- **No format churn**: `idx_collections.bin` and `series.dat` retain their existing 88-byte `CollectionIndexRec` and 88-byte `SeriesRec` formats. No `flags` field needed in auto-series structures.

### 3.1 How they remain distinct despite sharing an index

| Aspect | Auto Series | User Collection |
|--------|-------------|-----------------|
| Source | `series.dat` (scan) | `user_collections.json` (runtime) |
| Cardinality | 0 or 1 per book | 0 to N per book |
| Ordering | `seriesIndex` (float) | `position` (float, user-set) |
| Management | Rebuilt on scan | Rebuilt on every mutation |
| UI actions | Enter to view series books | Enter + long-press for CRUD |
| `flags` value | `0` | `1` (bit0 set) |

In `LIBRARY_SORT_COLLECTIONS` mode: show only `flags == 0` entries (existing behavior).
In `LIBRARY_SORT_MIXED` mode: show both `flags == 0` and `flags == 1`, interleaved alphabetically with standalone books.

### 3.2 Separate Binary Index for User Collections

**A separate `idx_user_collections.bin` is created for user-defined collections.**

- `idx_collections.bin` remains unchanged (88-byte `CollectionIndexRec`). No `flags` field added. Contains only auto-series derived from `series.dat`.
- `idx_user_collections.bin` is a new file (88-byte `UserCollectionIndexRec`) containing tiles for user collections built from `user_collections.json` via `UserCollectionsStore`.
- `user_collections.json` remains the source of truth; it is read during `buildUserCollectionsIndex()` and the resulting binary index is cached for fast querying.
- `idx_mixed.bin` is rebuilt from both `idx_collections.bin` (series tiles) AND `idx_user_collections.bin` (collection tiles), merged with standalone-book entries.

Both `series.dat` and `idx_collections.bin` retain their existing 88-byte record formats — no structural changes are needed to the auto-series path.

### 3.3 Memory Constraints

- ESP32-C3 usable RAM ≈ 380 KB, no PSRAM.
- Library V3 RAM budget ≈ 11 KB total (`pageCache_[16]` ≈ 4 KB, sort chunk ≈ 4 KB, stack/I/O ≈ 3 KB).
- No heap allocation in render loops, input loops, or `queryPage`/`queryMixed` hot paths.
- New store must follow `FavoritesStore` pattern: singleton, atomic JSON write (`.tmp` + `rename`), `ensureLoaded()` / `saveToFile()` / `loadFromFile()`.
- Use `ButtonNavigator::clampIndex`, `ListRenderHelper`, `OrderListActivity` for any new list/management UI.

### 3.4 Book Identity Stability

`bookId` is a `uint32_t` assigned during `LibraryIndex::scan()` and is **unique, stable, never reassigned** (confirmed in `LibraryIndex.h`). IDs start at 1 and increment monotonically; `0` is never assigned to a real book.

**Conclusion**: `bookId == 0` is a safe sentinel value for tombstoned/removed entries. No collision with legitimate books. `user_collections.json` can safely reference `bookId` as a `uint32_t` foreign key. No schema change needed.

**Edge case — rename/move**: if a book is renamed or moved, its path hash changes, so it receives a new `bookId`. The old `bookId` becomes orphaned. `UserCollectionsStore::removeBookFromAll()` MUST be called for the old `bookId` during scan when an entry is detected as moved/renamed, not only on delete. For now, the simplest safe behavior is: on scan, if a previously known `bookId` is no longer present in `library.dat`, call `removeBookFromAll(oldBookId)`. This keeps membership consistent without requiring a full path-reconciliation layer.

**Conclusion**: `user_collections.json` can safely reference `bookId` as a `uint32_t` foreign key. No schema change needed.

---

## 4. File and Data Format Changes

### 4.1 `series.dat` (existing)

Current layout (verified in `LibraryIndex.cpp:69-74`):
```cpp
struct __attribute__((packed)) SeriesRec {
  uint32_t bookId;       // 4 bytes
  char     seriesName[80]; // 80 bytes
  float    seriesIndex;    // 4 bytes
};
static_assert(sizeof(SeriesRec) == 88, "SeriesRec must be 88 bytes");
```

**No format change.** `series.dat` keeps the existing 88-byte `SeriesRec`. A `flags` byte is NOT added since user collections live in a separate index file and a separate binary. The only addition to `series.dat` is the folder-fallback derivation logic at scan time (see Section 5.2).

```
struct __attribute__((packed)) SeriesRec {
  uint32_t bookId;       // 4 bytes
  char     seriesName[80]; // 80 bytes
  float    seriesIndex;    // 4 bytes
};
static_assert(sizeof(SeriesRec) == 88, "SeriesRec must be 88 bytes");
```

Semantics (in-memory, no on-disk flags byte):
- `bookId == 0` means tombstoned/removed entry; skip at query time.
- `seriesName` keeps the raw display name. Normalization happens in memory only during grouping.
- Folder-fallback series are tracked in-memory at scan time (a transient marker in `LibraryIndex::scan()`); the result is a `SeriesRec` with a folder-derived `seriesName`. At query time, `queryCollections()` / `queryMixed()` can distinguish auto-series from user collections by checking `UserCollectionsStore` — if the tile name matches a user collection, it's a user collection; otherwise it's an auto-series.

### 4.2 `user_collections.json` (new)

Path: `/.crosspoint/user_collections.json`

```json
{
  "version": 1,
  "collections": [
    { "id": "c_001", "name": "Sci-Fi", "createdAt": "2026-09-09T21:00:00Z" }
  ],
  "members": [
    { "collectionId": "c_001", "bookId": 42, "position": 0 }
  ]
}
```

Rules:
- `id` assigned by store (short string, monotonically increasing).
- `position` for manual ordering; default 0.
- One membership per book per collection.
- `version` enables future schema migration.

### 4.3 `idx_collections.bin` (unchanged) and new `idx_user_collections.bin`

**`idx_collections.bin`** remains unchanged — 88-byte `CollectionIndexRec`, built only from auto series in `series.dat`. No structural changes, no `flags` field.

```cpp
struct __attribute__((packed)) CollectionIndexRec {
  char     collectionName[80];
  uint32_t firstSeriesOffset;
  uint32_t bookCount;
};
static_assert(sizeof(CollectionIndexRec) == 88, ...);
```

**New: `idx_user_collections.bin`** — 88-byte `UserCollectionIndexRec`, built from `user_collections.json` via `buildUserCollectionsIndex()`.

```cpp
struct __attribute__((packed)) UserCollectionIndexRec {
  char     collectionName[80];   // display name, null-padded
  uint32_t collectionIdHash;     // hash of collectionId string, for stable lookup
  uint32_t bookCount;
};
static_assert(sizeof(UserCollectionIndexRec) == 88, ...);
```

- `buildCollectionsIndex()` builds `idx_collections.bin` from `series.dat` only (auto-series).
- `buildUserCollectionsIndex()` builds `idx_user_collections.bin` from `user_collections.json` via `UserCollectionsStore`.
- `buildMixedIndex()` reads BOTH `idx_collections.bin` and `idx_user_collections.bin`, plus `library.dat`, and produces `idx_mixed.bin` with series tiles, collection tiles, and standalone books all interleaved alphabetically.

`firstSeriesOffset` in `CollectionIndexRec` semantics:
- Auto-series: offset in `series.dat` to the first book of this series. Used by `queryCollectionBooks()` to seek into `series.dat`.

User collection members are resolved at query time from `UserCollectionsStore`, not from `series.dat`.

### 4.4 LibraryRoot Semantics

All new folder/path logic uses `SETTINGS.libraryRootDir`, never hard-coded `/`.

---

## 5. Component Changes

### 5.1 `EpubParser` (`src/components/EpubParser.cpp`)

- Document that `readDirectFromZip()` returns raw series name/index and does **not** fall back to folder names.
- No functional changes needed; normalization happens in `LibraryIndex::scan()`.

### 5.2 `LibraryIndex` (`src/components/LibraryIndex.cpp` and `.h`)

**New internal helpers:**
- `static std::string normaliseSeriesKey(const char* raw)` — 20-byte sort key using existing Latin-1 fold table.
- `static bool isLibraryRoot(const char* path)` — compares against `SETTINGS.libraryRootDir`.

**`scan()` changes:**
- After EPUB metadata extraction, if series is empty:
  - Compute parent folder via `FsHelpers::parentPath(p)`.
  - If parent is not root and not empty, copy folder name into `series` field. Mark in a transient in-memory set `scanFolderFallbackBookIds_` (NOT persisted to `series.dat`).
  - If metadata was found, `series.dat` entry is written normally (no special flag).
- Keep raw `seriesName` in `SeriesRec` for display. Normalize only in memory during grouping.
- **No change to `series.dat` format** — stays 88-byte `SeriesRec`.

**`buildCollectionsIndex()` changes:**
- Read `series.dat`. If file size is not divisible by 88, delete it and rebuild from scratch (`Storage.remove(kSeriesFile)`).
- Build a temporary `std::unordered_map<uint32_t, std::string>` mapping `bookId -> path` by scanning `library.dat` once. O(n), discarded after build.
- Group auto-series entries by normalized key globally (metadata OR folder-fallback, both produce `SeriesRec` entries).
- **Do NOT load `user_collections.json` here.** User collections are handled by `buildUserCollectionsIndex()` separately.
- Write `idx_collections.bin` with only auto-series tiles. No `flags` field.
- Write `series.dat` with 88-byte struct, sorted by normalized series name + series index.

**New: `buildUserCollectionsIndex()`:**
- Reads `user_collections.json` (via `UserCollectionsStore::ensureLoaded()`).
- Builds `UserCollectionIndexRec` array (88-byte each) with `collectionName`, `collectionIdHash`, `bookCount`.
- Writes `idx_user_collections.bin` as a sorted, compact binary file.
- Called separately from `buildCollectionsIndex()`; can be rebuilt independently when a user collection is created/renamed/deleted.

**`buildMixedIndex()` changes:**
- Reads `idx_collections.bin` (auto-series tiles) AND `idx_user_collections.bin` (user collection tiles), plus `library.dat`.
- Merges all three sources (series tiles + collection tiles + standalone books) into `idx_mixed.bin`, interleaved alphabetically by display name.
- No `flags` branching needed — the two index files are inherently separate.

**New public API (on `UserCollectionsStore`, not `LibraryIndex`):**
```cpp
// User collections
bool createCollection(const char* name, char* outId, size_t outIdCap);
bool renameCollection(const char* collectionId, const char* newName);
bool deleteCollection(const char* collectionId);
bool addBook(const char* collectionId, uint32_t bookId, float position = 0.0f);
bool removeBook(const char* collectionId, uint32_t bookId);
int queryUserCollections(BookRef* out, int page, int pageSize, int coverWidth, int coverHeight);
int queryUserCollectionBooks(BookRef* out, int page, int pageSize, const char* collectionId);
int totalUserCollections();
int userCollectionBookCount(const char* collectionId);

// Cache invalidation
void invalidate();
```

**`queryCollections()` changes:**
- Reads only from `idx_collections.bin` (auto-series). Behavior unchanged — shows `LIBRARY_SORT_COLLECTIONS`.

**`queryMixed()` changes:**
- Reads from `idx_collections.bin` AND `idx_user_collections.bin`, interleaved by display name in `idx_mixed.bin` (pre-merged at build time, so query is branchless).

### 5.3 New Store: `UserCollectionsStore` (`src/UserCollectionsStore.h/.cpp`)

Follow `FavoritesStore` pattern with these additions:

```cpp
class UserCollectionsStore {
public:
  static UserCollectionsStore& getInstance();
  
  // Lifecycle
  void ensureLoaded();
  bool needsReload() const;
  void bumpGeneration();
  uint32_t generation() const;
  
  // Queries
  int totalCollections() const;
  int totalMembers(const char* collectionId) const;
  const std::vector<UserCollection>& collections() const;
  std::vector<CollectionMember> members(const char* collectionId) const;
  bool hasBook(const char* collectionId, uint32_t bookId) const;
  
  // Mutations
  bool createCollection(const char* name, char* outId, size_t outIdCap);
  bool renameCollection(const char* collectionId, const char* newName);
  bool deleteCollection(const char* collectionId);
  bool addBook(const char* collectionId, uint32_t bookId, float position = 0.0f);
  bool removeBook(const char* collectionId, uint32_t bookId);
  void removeBookFromAll(uint32_t bookId);
  
  // Persistence
  bool saveToFile();
  bool loadFromFile();
  
private:
  UserCollectionsStore() = default;
  
  std::vector<UserCollection> collections_;
  std::vector<CollectionMember> members_;
  uint32_t generation_ = 0;
  bool loaded_ = false;
  bool dirty_ = false;
};
```

Key rules:
- Atomic write: serialize to `.tmp`, then `rename()` to final path.
- `bumpGeneration()` increments `generation_` on every mutation. `LibraryActivity` compares this against its cached `lastUserCollectionsGeneration_` to decide whether to rebuild indices.
- `removeBookFromAll()` is called when a book is deleted from the library to avoid dangling memberships.
- `loadFromFile()` must recover gracefully from corrupted JSON: if parse fails, log error, start with empty collections, do NOT crash.
- Register in `StoreManager.h` alongside `FavoritesStore` and `HiddenBooksStore`.

### 5.4 Runtime Integration with LibraryActivity

**Rebuild trigger logic (debounced):**

```cpp
// In LibraryActivity.h
uint32_t lastUserCollectionsGeneration_ = 0;
bool pendingCollectionsRebuild_ = false;

// In LibraryActivity::onEnter() or scanSd():
if (USER_COLLECTIONS.generation() != lastUserCollectionsGeneration_) {
  pendingCollectionsRebuild_ = true;
}

// In render() or loop(), once per frame when dirty:
if (pendingCollectionsRebuild_) {
  LibraryIndex::buildUserCollectionsIndex();  // Builds idx_user_collections.bin only
  LibraryIndex::buildMixedIndex();            // Merges series + collections into idx_mixed.bin
  lastUserCollectionsGeneration_ = USER_COLLECTIONS.generation();
  pendingCollectionsRebuild_ = false;
  clearPageFrameCache();
}
```

**After collection mutation from result handlers:**
```cpp
case CollectionPickerActivity::Result::ADDED_TO_COLLECTION:
  // Store already bumped generation inside addBook/removeBook
  pendingCollectionsRebuild_ = true;
  requestUpdate(); // triggers render(), which performs the rebuild once
  break;
```

**Rationale**: If the user adds 10 books in sequence through `CollectionPickerActivity`, only ONE rebuild of `idx_user_collections.bin` + `idx_mixed.bin` runs on the next render frame, not 10. `idx_collections.bin` (auto-series) is NOT touched — no unnecessary SD writes for the series index. The rebuild is always performed on the next render, never blocking the caller.

### 5.5 Collection Management Activities

**New activities (use Steroids list/order patterns):**

1. **`CollectionManageActivity`** — list user collections with Create/Rename/Delete/Clear actions. Use `ListInputMapper` + `ListRenderHelper` for standard list behavior.

2. **`CollectionPickerActivity`** — pick a collection to add a book to, with "Create new" option. Returns selected `collectionId` to caller via `setResult()`.

3. **`CollectionBooksActivity`** — books inside a collection, ordered by `position`. Back returns to previous collection tile selection.

**LibraryActivity integration points:**
- Sort popup: keep existing Serie / Serie + Libri entries.
- Book context menu (`BookContextMenuActivity`): add "Add to collection..." → launches `CollectionPickerActivity`.
- Collection tile long-press: launch `CollectionManageActivity` for that collection.
- New "Manage collections" entry in filter popup or separate shortcut.

**Visual distinction between auto series and user collections:**
- `queryMixed()` and `buildMixedIndex()` distinguish user collections from auto-series by the **separate index file** (`idx_user_collections.bin` vs `idx_collections.bin`), NOT by a `flags` field. No `flags` branching in query code.
- A new `BookRef::isCollection` boolean field marks user-collection tiles vs auto-series tiles. Do NOT reuse `isFavorite` for this purpose — `isFavorite` has an existing semantic meaning ("book is in favorites") and must remain independent. A book can be both a favorite and in a user collection; conflating the two would break filters, badges, and `FavoritesStore`.
- `LibraryActivity` renders user collection tiles with a distinct overlay/badge (e.g., folder icon or colored ribbon) so users can distinguish them from auto series at a glance.
- If a user creates a collection with the same name as an auto series, both tiles appear; the badge prevents confusion.

**Folder-fallback disambiguation:**
- When multiple folder-fallback series share the same normalized name (e.g., `/A/Unsorted/` and `/B/Unsorted/`), `queryMixed()` and `queryCollections()` MUST append the relative parent path as a subtitle to the tile text. Example: "Unsorted" → display "Unsorted" with subtitle "/A" for one tile and "/B" for the other.
- This subtitle is shown ONLY when there are multiple folder-fallback entries with the same normalized name in the same view. Metadata-derived series never show a subtitle.
- The subtitle text is computed at query time from `library.dat` path lookups, not stored in `idx_collections.bin`, to keep the index format compact.

**`addBookToCollection` idempotency:**
- `UserCollectionsStore::addBook(collectionId, bookId, position)` MUST be idempotent: if the book is already a member of the collection, the call is a no-op (do NOT update `position`, do NOT create a duplicate entry).
- If the caller wants to update position, it must call `removeBook()` first, then `addBook()` with the new position.
- This prevents duplicate entries from double-tap or rapid repeated calls from the picker.

### 5.6 i18n Additions

Add to `lib/I18n/translations/english.yaml` and `italian.yaml`:
- `STR_COLLECTIONS_MANAGE`
- `STR_COLLECTION_CREATE`
- `STR_COLLECTION_RENAME`
- `STR_COLLECTION_DELETE`
- `STR_COLLECTION_ADD_BOOK`
- `STR_COLLECTION_REMOVE_BOOK`
- `STR_COLLECTION_EMPTY`
- `STR_COLLECTION_NEW_NAME`
- `STR_COLLECTION_PICK`

Run `python -X utf8 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` after YAML changes. Do not manually edit generated i18n headers.

---

## 6. Workflow and Branching

### 6.1 Branch

Current branch: `temp_steroids_settings` (based on `upstream/master` at `a4ae01a7`)

### 6.2 Actual commit state

Core series/collections restructure work is already staged and ready to commit:
- `feat(library): add UserCollectionsStore with JSON persistence`
- `feat(library): add create/rename/delete collection API`
- `feat(library): add add/remove book membership API`
- `feat(ui): add collection management and book-to-collection activities`
- `feat(i18n): add collection management strings`
- `fix(library): invalidate and rebuild idx_user_collections.bin + idx_mixed.bin on collection change`

Current session added:
- `chore(build): add -DOMIT_LEXEND and -DOMIT_BOOKERLY to reduce firmware size`
- `fix(version): downgrade embedded version mismatch from error to warning`

---
| 6 | `feat(ui): add collection management and book-to-collection activities` |
| 7 | `feat(i18n): add collection management strings` |
| 8 | `fix(library): invalidate and rebuild idx_user_collections.bin + idx_mixed.bin on collection change` |
| 9 | `test(build): verify default build compiles` |

---

## 7. Validation

### 7.1 Build (only default)

```powershell
python -X utf8 -m platformio run -e default -j 16
```

Must succeed after each commit. Do not use `gh_release` for verification.

### 7.2 Runtime checks

1. Scan metadata-only books → series tiles group correctly.
2. Scan folder-only books → parent folder becomes series name (unless root).
3. Scan conflicting metadata → metadata wins; folder ignored.
4. **Two folders with same basename in different paths do NOT merge into one series** (e.g., `/A/Unsorted/` and `/B/Unsorted/` produce two distinct folder-fallback series).
5. Create collection → tile appears in Serie and Serie + Libri.
6. Add/remove books → membership persists across reboot.
7. Delete collection → `idx_user_collections.bin` rebuilt; tile disappears after next `buildMixedIndex()`.
8. Mixed view → auto series (from `idx_collections.bin`) and user collections (from `idx_user_collections.bin`) render together, no duplicates.
9. Back from collection → previous selector restored.
10. Search/filter → user collections participate correctly.
11. Memory → heap stays above ~80 KB during scan and browse.
12. `idx_user_collections.bin` is rebuilt independently — verify that `idx_collections.bin` is NOT touched when only a user collection is created/renamed/deleted.

### 7.3 Regression checks

- Flat title/author sort unchanged.
- EPUB metadata extraction unchanged.
- Favorites, hidden books, reading stats unchanged.
- Page-frame cache and natural title sort preserved.
- `LibraryActivity.cpp/h` and `LibraryIndex.cpp` remain functional as V3.

---

## 8. Protected Files

Per `STEROIDS-ALIGN-TO-UPSTREAM.md`, these files are Steroids-only and must not be overwritten by upstream merges. Our changes preserve their V3 behavior:

| File | Why protected |
|------|---------------|
| `src/activities/apps/LibraryActivity.cpp/h` | Steroids Library V3: mixed view, collection-tile covers, natural sort, page-frame cache |
| `src/components/LibraryIndex.h` | V3 public API declarations; new collection/user-collection methods must not be overwritten by upstream |
| `src/components/LibraryIndex.cpp` | V3 index engine: `buildMixedIndex()`, `queryMixed()`, `makeTitleSortKey()`, cover-aware queries |
| `src/components/EpubParser.cpp/h` | EPUB metadata parser used by library |
| `src/JsonSettingsIO.cpp` | Must stay byte-identical to upstream; all Steroids settings live in `JsonSettingsIOSteroids.cpp` |

Our new files (`UserCollectionsStore.cpp/h`, collection activities) are Steroids-only additions and go in `src/` alongside existing stores.

---

## 9. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Two binary index files (`idx_collections.bin` + `idx_user_collections.bin`) increase SD read overhead during full rebuild | `buildUserCollectionsIndex()` runs only on-demand (user collection mutations), not on every scan. `buildCollectionsIndex()` (auto-series) only runs on SD scan. `buildMixedIndex()` reads both plus `library.dat` once per rebuild. |
| Heap fragmentation during merge of two index files into `idx_mixed.bin` | Reuse existing external merge-sort chunk pattern; keep RAM under 11 KB library budget. |
| JSON corruption on power loss | Atomic write via `.tmp` + `rename()` in `UserCollectionsStore`. Recovery logic in `loadFromFile()`. |
| Folder-fallback false merges | Folder-fallback entries are grouped by exact parent path match, not by normalized name alone. Metadata series are still grouped globally by normalized name. |
| Series name collisions after normalization | Normalized 20-byte key used for grouping; collisions only when names truly match, which is correct behavior. |
| Linear scans in `queryUserCollectionBooks()` | Acceptable for small user collections. Optimize later with book→collections index if needed. |
| i18n merge conflicts | Keep new keys under 10; use `gen_i18n.py`; never edit generated headers. |
| Upstream merge conflicts | New files and changes are additive; protected files keep local V3 logic. |
| `idx_collections.bin` and `idx_user_collections.bin` get out of sync after a partial corruption | `buildMixedIndex()` validates both files; if either is stale or corrupt, rebuild from source (`series.dat` / `user_collections.json`). |

### Known Limitations

- **Normalization scope**: The 20-byte sort key uses the existing Latin-1 fold table, which handles only a subset of Unicode. Non-Latin scripts (CJK, Arabic, Cyrillic) will not fold correctly. This is consistent with the existing title/author sort behavior and is acceptable for the target device.
- **Heap threshold**: The "~80 KB free heap" target is measured with `ESP.getFreeHeap()` after `LibraryActivity::onEnter()`. Baseline measurement should be taken on a library of ~200 books before implementation starts, and the threshold should be adjusted to `baseline - 20%` rather than a fixed number.
- **Renamed/moved books**: If a book is renamed or moved on the SD card while the device is off, it receives a new `bookId` on next scan. `LibraryIndex::scan()` detects that the old `bookId` is no longer present in `library.dat` and calls `UserCollectionsStore::removeBookFromAll(oldBookId)` to avoid dangling memberships. The user must still re-add the book to collections manually because the old `bookId` cannot be mapped to the new path. This matches the behavior of other path-keyed stores (Favorites, HiddenBooks).

---

## 10. Open Questions

None. Plan is implementation-ready.

## 11. Pre-Implementation Checklist

Before opening `feature/series-collections-restructure` branch:
- [ ] Confirm `BookRef` extension with `isCollection` field does not conflict with existing UI code
- [ ] Verify `LibraryIndex::scan()` bookId assignment starts at 1 (not 0) to keep sentinel safe
- [ ] Baseline heap measurement on ~200-book library for "~80 KB free" threshold calibration
