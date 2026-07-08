# Refactoring Plan: Hotspot 1 (ListSelectActivity) & Hotspot 4 (BookStore Base)

## Goal
Reduce firmware size and maintenance burden by generalizing repeated list-activity scaffolding and deduplicating store logic, while keeping ESP32-C3 constraints (380 KB RAM, no PSRAM, no RTTI/exceptions) and avoiding UI regressions.

## Assumptions Challenged

**User claim**: "Hotspot 1 sarebbe a impatto quasi zero."

**Finding**: This claim is **incorrect**. The 41 activities that call `GUI.drawList` are NOT uniform. They vary across at least five axes:

| Axis | Variations observed | Examples |
|---|---|---|
| Back/Confirm trigger | `wasReleased` (reader/apps) vs `wasPressed` (settings) | `FavoritesAppActivity` vs `StatusBarSettingsActivity` |
| Continuous nav | Full paging (`nextPageIndex`) vs single-step wrap (`nextIndex`) | `FlashcardsAppActivity` vs `StatusBarSettingsActivity` |
| Header type | `HeaderDateUtils::drawHeaderWithDate` vs `GUI.drawHeader` | `AppsActivity` vs `TimeZoneSelectActivity` |
| Extra render content | Plain list vs custom preview below list vs metric cards | `StatusBarSettingsActivity` (preview), `ReadingStatsActivity` (cards) |
| Data model | Static arrays, vectors of structs, store-backed, reader overlays | `TimeZoneSelectActivity` vs `RecentBooksActivity` vs `DictionarySuggestionsActivity` |

There are **no automated UI tests** (`test/` covers only differential rounding, hyphenation, and JSON parsing). Any regression must be caught on device.

**Conclusion**: Hotspot 1 has **HIGH impact but MEDIUM-HIGH risk**. Hotspot 4 has **MEDIUM impact but LOW risk**.

## Hotspot 4 — BookStore Base (RECOMMENDED FIRST)

### Problem
`FavoritesStore` (329 lines) and `RecentBooksStore` (440 lines) share ~80% of their logic but with different names and slight semantic differences. Specific duplications:

- `findBookIndex()` — identical normalization + bookId/path match logic
- `normalizeBook()` — identical normalizePath + resolveStableBookId + findMatchingBook
- `normalizeBooks()` — identical dedup-with-merge logic (RecentBooks adds max-size prune)
- `addBook()` / `updateBook()` / `updateBookPath()` / `removeBook()` — structurally identical, differing only in vector member name and max-size behavior
- **`fallbackTitleFromPath()`** — literally the same function with two different names (`getFallbackTitleFromPath` in FavoritesStore, `fallbackTitleFromPath` in RecentBooksStore)

### Strategy: Template Free Functions (NOT inheritance)

**Do NOT create a `BookStoreBase` class** with virtual methods. Reasons:
- `FavoriteBook` and `RecentBook` are both serialized to JSON with specific field names; a common base struct risks breaking deserialization.
- Virtual functions add vtable overhead on a class that is instantiated as a singleton (negligible runtime cost but adds complexity).
- The two stores have genuinely different semantics (`MAX_RECENT_BOOKS`, `pruneMissing()`, binary format, `toggleBook()`).

**Instead**: Extract identical algorithms into `template` free functions in a new `src/util/BookStoreUtils.h`:

```cpp
namespace BookStoreUtils {
  template<typename Book>
  int findBookIndex(const std::vector<Book>& books, const std::string& path, const std::string& bookId);

  template<typename Book>
  void normalizeBook(Book& book);

  template<typename Book>
  void normalizeBooks(std::vector<Book>& books, bool enforceMaxSize = false, size_t maxSize = 0);

  std::string fallbackTitleFromPath(const std::string& path);
}
```

Each store's methods become one-line delegations:

```cpp
int FavoritesStore::findBookIndex(const std::string& path, const std::string& bookId) const {
    return BookStoreUtils::findBookIndex(favoriteBooks, path, bookId);
}
```

### Risk Assessment
- **Low**: Template instantiation for 2 types (`FavoriteBook`, `RecentBook`) adds negligible code bloat on ESP32-C3.
- **Low**: No change to serialization format or public API.
- **Low**: `fallbackTitleFromPath()` unification is a pure rename, zero behavior change.

### Steps
1. Create `src/util/BookStoreUtils.h` with `fallbackTitleFromPath()`.
2. Replace both store implementations to use it.
3. Add template helpers for `findBookIndex`, `normalizeBook`, `normalizeBooks`.
4. Migrate `FavoritesStore.cpp` and `RecentBooksStore.cpp` to use helpers.
5. Build and verify with `python -X utf8 -m platformio run -e default -j 16`.

### Rollback
Each store file is independently migratable. If a template instantiation causes issues, revert that store's `.cpp` file only.

## Hotspot 1 — List Activity Scaffolding (RECOMMENDED SECOND)

### Problem
~34 activities repeat variations of:
- Layout calculation (`contentTop`, `contentHeight`, `pageItems`)
- Back/Confirm handling
- 4-callback ButtonNavigator wiring
- Render scaffold (`clearScreen` → header → `drawList` → `mapLabels` → `drawButtonHints` → `displayBuffer`)

### Strategy: Composition Helpers (NOT inheritance)

**Do NOT create a `ListSelectActivity` base class**. Reasons:
- A base class would need 10+ virtual hooks to accommodate the 5 variation axes identified above.
- Activities like `SettingsActivity` (1161 lines, category grid), `StatusBarSettingsActivity` (custom preview), and `ReadingStatsActivity` (metric cards) are fundamentally different from a simple list selector.
- Inheritance would force all subclasses into the same hierarchy, making future deviations harder.
- On ESP32-C3, virtual call overhead is tiny but the design rigidity is costly.

**Instead**: Create three standalone helpers in `src/activities/util/` that activities can adopt **independently**:

```cpp
// src/activities/util/ListLayout.h
struct ListLayout {
    int contentTop;
    int contentHeight;
    int pageItems;
    static ListLayout compute(const GfxRenderer& renderer, bool hasHeader = true, bool hasSubtitle = false, ...);
};

// src/activities/util/ListInputMapper.h
class ListInputMapper {
public:
    using BackHandler = std::function<void()>;
    using ConfirmHandler = std::function<void()>;
    using NavHandler = std::function<void(int delta)>;

    void setBackHandler(BackHandler);
    void setConfirmHandler(ConfirmHandler, bool useRelease = true);
    void setNavHandler(NavHandler);
    void loop(MappedInputManager& input, int itemCount, int& selectedIndex, bool requestUpdate = true);
};

// src/activities/util/ListRenderHelper.h
class ListRenderHelper {
public:
    static void drawStandardHeader(GfxRenderer& renderer, const char* title, const char* subtitle = nullptr);
    static void drawStandardList(GfxRenderer& renderer, const Rect& rect, int itemCount, int selectedIndex, ...);
    static void drawStandardHints(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* btn1, const char* btn2, const char* btn3, const char* btn4);
};
```

### Adoption Strategy: Incremental, per-activity

**Phase 1 — "Pure" list activities** (no deviations, ~15 activities):
Activities that use `wasReleased` + standard 4-callback nav + standard header + no extra render content:
- `TimeZoneSelectActivity`
- `LanguageSelectActivity`
- `FontSelectionActivity`
- `ShortcutOrderActivity`
- `ShortcutLocationActivity`
- `ShortcutVisibilityActivity`
- `OpdsServerListActivity`
- `KOReaderSettingsActivity`
- `OpdsSettingsActivity`
- `NetworkModeSelectionActivity`
- `ReadingStatsImportActivity`
- `ButtonRemapActivity`
- `ClearCacheActivity`

Each adoption reduces that activity by ~25-35 lines.

**Phase 2 — App hub activities** (~8 activities):
Activities with hub-style lists (Browse/Recents/Stats/Settings pattern):
- `FavoritesAppActivity`
- `FlashcardsAppActivity`
- `SleepAppActivity`
- `ScreenCleanActivity`
- `FavoritesBrowserActivity`
- `FavoritesOrderActivity`
- `FlashcardStatsActivity`
- `FlashcardSettingsActivity`
- `FlashcardRecentsActivity`
- `FlashcardBrowserActivity`

Each adoption reduces by ~20-30 lines.

**Phase 3 — Complex activities** (manual review only):
- `BookmarksAppActivity` — long-press delete deviates from standard Confirm
- `RecentBooksActivity` — long-press delete + custom data loading
- `ReadingStatsActivity` — metric cards, not standard list rows
- `StatusBarSettingsActivity` — custom preview + no continuous nav
- `SettingsActivity` — category grid, not a list
- `DictionarySuggestionsActivity` — reader overlay

These may never be migrated, or may be migrated with custom overrides.

### Risk Assessment
- **Medium**: `wasPressed` vs `wasReleased` is a behavioral difference. `ListInputMapper` must be configurable.
- **Medium**: Layout math varies slightly (`contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing` vs `* 2`). `ListLayout` must preserve existing calculations per activity.
- **Medium**: No automated UI tests. Every migrated activity must be manually verified on X4 in all 4 orientations.
- **Low**: Helpers are additive. Non-migrated activities continue to work unchanged.

### Validation Criteria
For each migrated activity, verify on device:
1. List renders correctly in portrait CW, portrait CCW, landscape CW, landscape CCW.
2. Back button exits/cancels correctly.
3. Confirm opens/selects correctly.
4. Continuous hold on Next/Previous pages correctly.
5. No visual shift in list position, header height, or button hint placement.

### Rollback
Each activity is migrated independently. If a helper causes a visual regression, revert that activity's `.cpp` to use direct calls.

## Recommendation

**Proceed with both, in this order:**

1. **Hotspot 4 first** (BookStoreUtils) — 1-2 days, low risk, immediate code reduction (~200-300 lines), establishes the pattern of extracting helpers without changing class hierarchy.
2. **Hotspot 1 second** (ListInputMapper/ListLayout/ListRenderHelper) — 1-2 weeks, medium risk, larger reward (~1000-1400 lines across 34 activities), requires device validation per activity.

**Do NOT proceed with**:
- Hotspot 2 (ChapterSelectionActivity base) — only 2 files, 90% similar but render differences (landscape hints, empty state) make it low ROI for the design complexity.
- Hotspot 3 (main.cpp split) — timing-sensitive boot code, global state (`deepSleepInProgress`, `silentRebootMagic`), high merge friction with upstream. Defer until after Hotspot 1 is stable.

## Open Questions

1. **Should `ListInputMapper` default to `wasReleased` or `wasPressed`?**
   - **Recommendation**: Default to `wasReleased` (reader/apps majority), with explicit `setUsePressTrigger(true)` for settings-style activities. This matches existing behavior.

2. **Should `ListLayout` expose `pageItems` via `UITheme::getNumberOfItemsPerPage` or compute its own?**
   - **Recommendation**: Expose `UITheme::getNumberOfItemsPerPage` as a parameter, defaulting to `(true, false, true, true)`. Activities with special needs (e.g., `FileBrowserActivity` with `pathReserved`) pass custom args.

3. **Should `ListRenderHelper::drawStandardList` use `GUI.drawList` directly or wrap it?**
   - **Recommendation**: Wrap it. This allows us to add orientation-aware gutters (landscape CW/CCW, portrait inverted) in one place, which is currently duplicated in ~10 reader overlay activities.

## Success Metrics

| Metric | Target |
|---|---|
| Lines of code removed (Hotspot 4) | ~200-300 |
| Lines of code removed (Hotspot 1, Phase 1+2) | ~1000-1400 |
| Activities migrated without regression (device validation) | 100% of migrated activities |
| Binary size change (gh_release build) | ≤ 0% (no growth) |
| New heap allocations in render loop | 0 |
