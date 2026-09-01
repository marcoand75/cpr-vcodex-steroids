# Boot → Home Memory Audit & Arena Optimization Plan
> Focus: ESP32-C3, SRAM budget ~200–250 KB utile, lyra-marcoand75 theme

## 1. Current Heap Hotspots (boot → Home)

### 1.1 `main.cpp` — boot phase
| Location | Pattern | Estimated cost | Issue |
|---|---|---|---|
| `startReplacementScreenSaver()` | `std::make_unique<ScreenSaverActivity>` | ~80–120 B heap | Runtime heap alloc from main loop |
| `ActivityManager::stackActivities` | `std::vector<std::unique_ptr<Activity>>` | Grows with push/pop | Heap fragmentation from activity transitions |
| `ActivityManager::pendingActivity` | `std::unique_ptr<Activity>` | Per-transition alloc | One extra heap block per activity change |
| Font objects (`EpdFont`, `EpdFontFamily`) | Static globals | ~10–15 KB `.bss`/`.data` | OK — static storage, no heap |
| `snapshotPluginName[32]` | Stack buffer | 32 B | OK |

### 1.2 `HomeActivity` — worst offender
| Location | Pattern | Estimated cost | Issue |
|---|---|---|---|
| `coverBuffer` | `malloc`/`free` in `storeCoverBuffer()` | ~40–63 KB per buffer | **Heap fragmentation**: free → malloc creates holes; Lyra carousel stores/restores cover region every frame |
| `pruneCarouselFrameCache()` | `std::vector<uint32_t> validHashes` | ~N×4 B (N=recent books) | Per-prune heap alloc |
| `getHomeShortcutEntries()` | Returns `std::vector<HomeShortcutEntry>` | ~10–20 entries × 8–16 B | Per-frame alloc in `loop()` and `render()` |
| `buildCarouselEntries()` | Returns `std::vector<HomeShortcutEntry>` | Same as above | Per-frame alloc |
| `loadRecentBooks()` | `std::vector<std::string> staleFavorites` | Up to N×64 B strings | Favorites-only path |
| `getCarouselFrameCachePathFromHash()` | Returns `std::string` by value | ~32–40 B per call | Temp string every hash lookup |
| `render()` → `homeEntries` | `std::vector<HomeShortcutEntry>` local | ~10–20 entries | **Every frame** allocation |
| `drawRecentBookCover()` | `std::string thumbPath` locals | ~64–128 B per cover | Per-cover temp string (5 covers × N indices) |
| `saveCarouselFrameToStorage()` | `std::string cachePath` | ~40 B | Per-frame-save temp string |
| `loadCarouselFrameFromStorage()` | `std::string cachePath` | ~40 B | Per-load temp string |
| `BookContextMenuActivity` launch | `std::make_unique<...>` | ~100–200 B each | Heap alloc per sub-activity |

### 1.3 Lyra-Marcoand75 specific overhead
- 5-cover carousel: each cover path lookup creates `std::string thumbPath` (up to 4 `Storage.exists` checks per cover × 5 covers = 20 temp strings per frame in worst case)
- `drawDataPanel()`: 8–10 `snprintf` into stack buffers (OK), but `getEta()` returns `std::string` ("~XhXm")
- `drawCyberPanel` + `drawSegmentProgressBar`: no heap, good
- Carousel frame cache: writes entire framebuffer (~60–100 KB) to SD; reads it back on cache miss. This is I/O, not heap, but the `std::string cachePath` is heap temp

## 2. Fragmentation Risk Assessment

### Critical (must fix)
1. **`coverBuffer` malloc/free cycle**: `storeCoverBuffer()` → `restoreCoverBuffer()` → `freeCoverBuffer()` → `onExit() free()`. On Lyra-Marcoand75, cover region is large (~40–63 KB). Repeated free/malloc creates heap holes that later activities (Library cover generation) cannot fit into. This is the **single largest fragmentation source**.
2. **Per-frame `std::vector<HomeShortcutEntry>`**: `render()` calls `getHomeShortcutEntries()` + `buildCarouselEntries()` every frame. Each call allocates from heap, then frees at end of frame. At 1–2 fps e-ink refresh, this is low frequency but still fragments over time.

### Moderate (should fix)
3. **`pruneCarouselFrameCache()` vector**: allocates once per prune, but prune happens on every `onEnter()` for carousel themes. Still small (N×4 B).
4. **`std::string` return values from path helpers**: `getCarouselFrameCachePathFromHash`, `getCarouselCenterThumbPath` return by value. These are small but appear in hot paths (render, cache load/save).

### Low (optional)
5. **`std::make_unique` for sub-activities**: These survive for the lifetime of the sub-activity, so they are not "malloc/free per frame". They fragment the heap once per transition. Acceptable if we move to an Activity arena.

## 3. Arena Allocation Strategy

### 3.1 Memory Budget (ESP32-C3 realistic)
| Arena | Capacity | Rationale |
|---|---|---|
| **Static Root (`.bss`/`.data`)** | ~80–100 KB | Global objects: `MappedInputManager`, `GfxRenderer`, `ActivityManager`, fonts, `FontDecompressor`, `FontCacheManager`, stores. Verified by existing firmware footprint (RAM 16.2% / 53 KB used in STEROIDS-OPTIMIZATION.md). |
| **Application Lifetime Arena** | 32 KB | Persistent services: WiFi state, settings runtime caches, reading-stats runtime summaries. No free() at runtime. |
| **Activity Scratch Arena** | 64 KB per activity | Temporary allocations for the active activity: `HomeActivity` vectors, carousel hash cache, cover buffer slot, temp strings. Reset on activity switch via `arena_reset()`. |
| **Framebuffer** | ~60–100 KB (depends on display mode) | Allocated once at boot via `heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Never reallocated. |
| **FreeRTOS Task Stacks** | ~8–12 KB total | Main loop + render task. |
| **Total** | ~180–220 KB | Within 250 KB utile budget. |

### 3.2 Arena Placement
- **Application Lifetime Arena**: Static global array `static uint8_t g_appArena[32 * 1024];`
- **Activity Scratch Arena**: Static global array `static uint8_t g_activityArena[64 * 1024];`
- The active activity receives a reference to `g_activityArena` on construction/`onEnter()`. On `onExit()` or activity switch, the arena is reset in O(1).

### 3.3 Lyra-Marcoand75 Optimizations
1. **Cover buffer**: Move `coverBuffer` into the Activity Scratch Arena instead of `malloc`. Use `arena.push(needed)` and keep the pointer as a member. Reset on exit.
2. **Carousel frame hash cache**: Replace `std::vector<uint32_t> validHashes` with a `StaticVector<uint32_t, 32>` (max 20 recent books × ~1.5 = 30 slots max) allocated on the arena.
3. **Path strings**: Replace `std::string` return values with `char[]` fixed buffers or `std::string_view` over a pre-allocated arena string. For hot paths, use stack buffers (`char path[96]`).
4. **Home shortcut entries**: Replace `std::vector<HomeShortcutEntry>` with a `StaticVector<HomeShortcutEntry, 32>` (max ~25 entries) inside `HomeActivity` or on the arena.
5. **Sub-activity launches**: Replace `std::make_unique<T>` with arena-allocated activities. The `ActivityManager` pushes onto a stack; each activity is constructed in the arena.

## 4. Implementation Plan

### Phase 1: Arena Infrastructure (no behavior change)
- [x] Add `src/util/ArenaAllocator.h`
- [ ] Add `StaticVector<T, N>` helper (or use `std::array` + manual size)
- [ ] Declare global arenas in `main.cpp`

### Phase 2: ActivityManager Arena Migration
- [ ] Change `ActivityManager` to construct activities in the current arena
- [ ] Replace `std::vector<std::unique_ptr<Activity>>` with arena-backed storage or at least arena-constructed activities
- [ ] Ensure `pushActivity`/`replaceActivity` use arena allocation

### Phase 3: HomeActivity Arena Migration
- [ ] Move `coverBuffer` to arena
- [ ] Replace `std::vector<HomeShortcutEntry>` locals with `StaticVector` or arena-allocated storage
- [ ] Replace `std::vector<uint32_t> validHashes` with arena-backed array
- [ ] Replace `std::string` temp paths with `char[]` stack buffers
- [ ] Replace `std::string` return values with `std::string_view` or fixed buffers

### Phase 4: Lyra-Marcoand75 Render Path Optimization
- [ ] Pre-allocate carousel cover path strings on arena
- [ ] Cache `thumbPath` resolution to avoid repeated `Storage.exists` calls
- [ ] Consider pre-warming the carousel frame cache path strings

### Phase 5: Verification
- [ ] Build with `python -X utf8 -m platformio run -e default -j 16`
- [ ] Check heap logs (`HCR-FRAG`, `HOME`, `BOOT`) for regressions
- [ ] Verify Lyra-Marcoand75 carousel renders correctly
- [ ] Verify activity transitions (Home → Library → Home) do not fragment

## 5. Forbidden Patterns (enforced by this refactor)

```cpp
// BANNED in HomeActivity and new code:
std::vector<T> localVec;            // Use StaticVector<T,N> or arena
std::string temp = ...;             // Use char buf[N] or string_view
malloc / free / new / delete        // Use Arena::push / arena reset
std::make_unique<Activity>(...)     // Use arena-constructed Activity
```

## 6. Open Questions / Risks
- `ActivityManager` currently uses `std::unique_ptr<Activity>` for ownership. Switching to arena requires careful lifetime management: the arena must outlive all activities on the stack.
- `RenderLock` and the render task may access `currentActivity` while the main loop switches activities. Need to ensure arena reset happens only when no render is in flight.
- Lyra-Marcoand75 `drawRecentBookCover` captures `std::string thumbPath` inside a lambda (`drawStackedCover`). This lambda is called during render; replacing the string with a stack buffer requires the lambda to capture the buffer or use a member.
