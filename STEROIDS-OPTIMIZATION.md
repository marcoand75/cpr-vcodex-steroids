# CPR-vCodex Steroids — Shared Procedures & Optimization Reference

> **SCOPE OF THIS FILE**
>
> `STEROIDS-OPTIMIZATION.md` is the **single source of truth for the shared
> utility procedures** that the Steroids fork has extracted to reduce
> code duplication, ensure consistent behavior, and minimize heap pressure.
>
> The other two Steroids definition files are:
> - `STEROIDS-ADDICTIONS.md` — All Steroids apps, screensavers, sleep/deep-sleep handling, and every enhancement.
> - `STEROIDS-ALIGN-TO-UPSTREAM.md` — Workflow to merge a new upstream release while preserving Steroids features.
>
> Use this file as a **mandatory line-guida** when:
> - Adding a new app / screen / panel that draws text or lists.
> - Modifying an existing screen that hand-rolls the same logic.
> - Reviewing PRs that introduce inline copy-pastes of the helpers below.
>
> The rule of thumb: **if a procedure already lives in a util, you must call
> that util**. Inline re-implementations are rejected at review.

---

## 1. Overview

Steroids has progressively de-duplicated ~1,500 lines of common rendering and
input-mapping code into 7 shared utilities, plus a small set of math
helpers and reusable activity bases. Every Steroids contributor must be
familiar with the table below before opening a PR.

| # | Util | Path | When to use |
|---|---|---|---|
| 1 | `text_overlay::` | `src/util/TextOverlay.{h,cpp}` | Configurable text + box + outlined text overlay on top of an image (screensaver, sleep) |
| 2 | `text_draw::` | `src/util/TextDrawer.h` | Stateless panel helpers: clipped text, right-aligned text, label+value row, progress bar, checkbox, percent math |
| 3 | `ListLayout::` | `src/activities/util/ListLayout.h` | One-call `compute(renderer, hasHeader, hasSubtitle, extraReservedHeight)` returning `{contentTop, contentHeight, pageItems}` for any list-style screen. Used internally by every other List util. |
| 4 | `ListRenderHelper::` | `src/activities/util/ListRenderHelper.h` | List rendering (header, list rows, empty state, hint hints) and the standard `drawEmptyCentered` helper |
| 5 | `ListInputMapper::` | `src/activities/util/ListInputMapper.h` | List input pipeline (back, confirm, nav) with press/continuous/release lambdas |
| 6 | `OrderListActivity<>` | `src/activities/util/OrderListActivity.h` | CRTP base for "user can reorder entries" screens (Back/Up/Down with moveMode toggle, plus an optional `handleConfirmHold` hook for hold-to-delete) |
| 7 | `ButtonNavigator::clampIndex` | `src/util/ButtonNavigator.h` | One-call list-state clamp: `clampIndex(current, total)`. Replaces every `std::clamp(selectedIndex, 0, total - 1)` and `std::max(0, size - 1)` site. |

All 7 rendering/input utilities are **tested, building, and used in
production**. They are the canonically correct way to write Steroids UI
code. `ListLayout` is the foundation; `ListRenderHelper` and
`OrderListActivity` both depend on it.

In addition, two reusable **activities** are shared:

- `ConfirmationActivity` (`src/activities/util/ConfirmationActivity.{h,cpp}`) — yes/no dialog with a multi-line body. Always use it instead of an inline `loop()`-driven confirmation.
- `FullScreenMessageActivity` (`src/activities/util/FullScreenMessageActivity.{h,cpp}`) — a transient text-only screen with configurable font style and refresh mode. Used for boot/error/loading messages that have no input.

---

## 2. `text_overlay::` — Image Overlay Text

### When to use
You are drawing **any** user-configurable text label on top of an image
(screensaver, sleep screen, custom wallpaper, custom future screens).
Includes:
- Multi-line word-wrapping at the screen width.
- One of six positions (top-left/right, bottom-left/right, center, random).
- One of four text styles (white, black, white-outlined, black-outlined).
- Optional 16-pixel black or white dithered background panel.
- Resolves the correct Bookerly fontId for the configured size.

### How to use
```cpp
#include "util/TextOverlay.h"

// 1. Cheap check: skip font loading entirely if text is empty/null.
if (text_overlay::shouldDraw(myText)) {
  // 2. Resolve font + style up front (e.g. before an image decode).
  text_overlay::OverlayConfig cfg;
  cfg.position   = SETTINGS.screenSaverTextPosition;
  cfg.textStyle  = SETTINGS.screenSaverTextStyle;
  cfg.drawPanel  = SETTINGS.screenSaverShowPanel != 0;
  cfg.panelColor = SETTINGS.screenSaverPanelColor == 0 ? Color::Black : Color::White;
  cfg.cachedRandomPosition = &randomCache;  // shared across BW/LSB/MSB passes
  text_overlay::resolveFontFromSize(SETTINGS.screenSaverFontSize, cfg.fontId, cfg.fontStyle);

  // 3. Prewarm font cache NOW (before heap gets fragmented by the image decode).
  fcm->prewarmCache(cfg.fontId, myText, styleMask);

  // 4. Call the drawer at every render pass. Returns false on empty text.
  text_overlay::draw(renderer, myText, cfg);
}
```

### Heap-saving behaviour
When the user has cleared the text setting (`shouldDraw()` returns false),
the helper returns false and **the caller can skip**:
- `FontDecompressor::restoreFontMemory()` (~40-48 KB)
- `FontCacheManager::prewarmCache()`
- All `renderer.wrappedText()` and `drawText()` calls

This is the central heap optimization of the screensaver. If you add a
new caller, mirror this pattern: **build the config up front, gate the
expensive font work on `shouldDraw()`, then call `draw()` per pass**.

### Random position cache
The `cachedRandomPosition` pointer is a `int*` the caller owns. The helper
reads it when `position == SCREENSAVER_TEXT_POS_RANDOM`, and writes a
new value to it **only if** the pointed-to value is negative. This means
the BW / LSB / MSB grayscale passes of the same image always share the
same random spot, preventing the "ghost jump" between passes. Always
reset to -1 at the start of a new frame (see `ScreenSaverActivity::render`).

### See also
- `ScreenSaverActivity::render` — full reference consumer.
- Future: SleepActivity `renderCustomSleepScreen` (currently a no-text
  screen, but if a future version adds a custom sleep text overlay, it
  MUST use this helper).

---

## 3. `text_draw::` — Panel Text + Checkbox + Progress Helpers

### When to use
You are building a **panel-style screen** with:
- A label on the left, a value on the right.
- A right-aligned or vertically-centered text.
- A percent / progress bar.
- A small filled checkbox.

Typical users: SleepActivity dashboard, cover-stats panels, achievement
panels, library progress overlays, settings value previews.

### How to use
```cpp
#include "util/TextDrawer.h"

// All helpers are inline — no namespace aliasing needed.
text_draw::drawTextClipped(renderer, fontId, x, y, text, maxWidth, black, style);
text_draw::drawRightText(renderer, fontId, rightEdge, y, text, style);
text_draw::drawTextWithRightValue(renderer, fontId, x, rightEdge, y, label, value, labelStyle, valueStyle);
text_draw::drawCheckBox(renderer, x, y, checked);
text_draw::drawProgressBar(renderer, rect, percent, lineWidth = 2);

// Math helpers
int pct = text_draw::percentOf(value, target);   // 0..100, safe on target==0
std::string label = text_draw::formatPercent(pct);  // "73%"
```

### Why not use `renderer.drawText` directly?
- `drawTextClipped` automatically truncates with ellipsis when the text
  overflows `maxWidth`. Without it, a long book title would visually
  overlap the right value column.
- `drawRightText` and `drawTextWithRightValue` compute the X offset for
  you. Without them, every callsite has to hand-roll
  `right - getTextWidth(...)` arithmetic.
- `drawProgressBar` matches the SleepActivity / Steroids-panel visual
  style (1px outline + filled bar) and rounds to nearest integer
  percent. Theme's `BaseTheme::drawProgressBar(size_t current, size_t
  total)` is the wrong fit when you have a percent instead of
  current/total.

### When NOT to use
- Do NOT use `drawProgressBar` if the surrounding panel already calls
  `GUI.drawProgressBar` (theme virtual) or `drawHelpText`. Pick one
  and stay consistent within a screen.
- Do NOT use `drawTextClipped` for free-form text where overflow is
  desired (truncation would surprise the user).

### See also
- `SleepActivity::renderReadingDashboardSleepScreen` — full reference consumer.
- `SleepActivity::drawLatestBookPanel` / `drawCoverStatsOverlay` — reference usage of the four-helper combo.

---

## 4. `ListRenderHelper::` — List Screens

### When to use
You are building any list-like activity (settings list, file list, app
grid, books list, etc.). The helper provides:
- `drawHeader(renderer, title)` — top header strip.
- `drawStandardHints(renderer, mappedInput)` — standard BACK/SELECT/UP/DOWN.
- `drawHints(renderer, mappedInput, btn1, btn2, btn3, btn4)` — custom hints.
- `drawListOrEmpty(renderer, layout, count, selectedIndex, titleFn, emptyText)` — auto-empty-state.
- `drawEmptyCentered(renderer, contentTop, text)` — single-line centered empty.
- `drawList(renderer, layout, count, selectedIndex, titleFn, ...)` — full list with sub-row + value column.

### How to use
```cpp
#include "../util/ListRenderHelper.h"

void MyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto layout = ListLayout::compute(renderer, true, true);
  ListRenderHelper::drawHeader(renderer, tr(STR_MY_TITLE));

  if (entries_.empty()) {
    ListRenderHelper::drawEmptyCentered(renderer, layout.contentTop, tr(STR_NO_ENTRIES));
  } else {
    ListRenderHelper::drawList(renderer, layout,
                                static_cast<int>(entries_.size()), selectedIndex_,
                                [this](int i) { return entries_[i].title; },
                                [this](int i) { return entries_[i].subtitle; },
                                [this](int i) { return entries_[i].icon; },
                                [this](int i) { return entries_[i].value; });
  }
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}
```

### Conventions enforced by the helper
- Layout is computed once per frame via `ListLayout::compute(renderer, ...)`.
- Hints are always drawn last so the button-hints bar is never overlapped.
- `displayBuffer()` is the LAST call (after hints, after empty-state).
- Empty state and list rendering share the same vertical region — switching between them does not cause layout shift.

### Forbidden patterns
The following manual patterns are now banned in Steroids list code:
```cpp
// BANNED: manual button-hints pair
const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
// USE INSTEAD:
ListRenderHelper::drawStandardHints(renderer, mappedInput);
```
```cpp
// BANNED: manual empty-state draw
renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
// USE INSTEAD:
ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_ENTRIES));
```
```cpp
// BANNED: manual `std::clamp(selectedIndex, 0, totalItems - 1)` (or the
// `std::max(0, ... - 1)` variant). USE INSTEAD:
selectedIndex = ButtonNavigator::clampIndex(selectedIndex, totalItems);
```

---

## 5. `ListInputMapper::` — List Input Pipeline

### When to use
You are building a list activity and want the standard Back / Confirm /
Up / Down handling with optional continuous-hold paging, **without**
hand-rolling `ButtonNavigator` lambda capture and `mappedInput.wasPressed`
polling in `loop()`.

### How to use
```cpp
#include "util/ListInputMapper.h"

class MyActivity : public Activity {
  ListInputMapper listInputMapper;
  // ...

  void onEnter() override {
    Activity::onEnter();
    // ...

    listInputMapper.setBackHandler([](void* ctx) {
      static_cast<MyActivity*>(ctx)->finish();
    }, this, false);  // useRelease=false → back fires on press

    listInputMapper.setConfirmHandler([](void* ctx) {
      static_cast<MyActivity*>(ctx)->openSelected();
    }, this, false);

    auto onNav = [](void* ctx, int delta) {
      auto* self = static_cast<MyActivity*>(ctx);
      if (self->entries_.empty()) return;
      if (delta > 0) {
        self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, self->entries_.size());
      } else {
        self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, self->entries_.size());
      }
      self->requestUpdate();
    };

    listInputMapper.setNavReleaseAndContinuous(onNav, onNav, this);
  }

  void loop() override { listInputMapper.loop(mappedInput); }
};
```

### Why lambdas?
- `setBackHandler` / `setConfirmHandler` / `setNavReleaseAndContinuous`
  require **two callback args + ctx** (the function + a context pointer)
  because the helpers are pure free functions that cannot access
  `this`. The lambdas defined inside `onEnter()` are the canonical
  pattern — they capture `this` via the ctx argument and have access to
  the full activity.

### Variants
- `setBackHandler(fn, ctx, useRelease)` — single tap or release-detect back.
- `setConfirmHandler(fn, ctx, useRelease)` — single tap or release-detect confirm.
- `setNavPressAndContinuous(pressFn, continuousFn, ctx)` — press once + auto-repeat when held.
- `setNavReleaseAndContinuous(releaseFn, continuousFn, ctx)` — release once + auto-repeat.
- `setNavAll(fn, ctx)` — both press and continuous route to the same lambda (used for simple "next/prev" wrap).

### Deliberate exclusions
The following activities are **excluded** from ListInputMapper migration
per the Steroids refactoring policy (in `STEROIDS-ADDICTIONS.md` and the
branch commit `b9a4db52`):
- `FlashcardRecentsActivity`, `FlashcardStatsActivity` — page-nav with continuous paging at the page boundary.
- `BookmarksActivity`, `ClippingsActivity`, `OpdsBookBrowserActivity` — preview / scrolling views.
- `WifiSelectionActivity` — multi-state dynamic hints.
- `KeyboardEntryActivity` — keyboard nav.
- `ButtonActionSelectorActivity` — short-press wrap + long-press autofire.

For these, raw `buttonNavigator` + `mappedInput.wasPressed` is still the
correct pattern. Do NOT migrate them blindly.

---

## 6. `OrderListActivity<>` — CRTP Reorderable-List Base

### When to use
You are building a "user can reorder entries" screen (settings list
order, shortcuts order, plugins order, etc.). The base provides:
- `onEnter` / `onExit` / `loop` / `render` lifecycle.
- `moveMode` boolean state with `setNavHandlers` for the move cursor
  (cursor follows Up/Down in moveMode, or moves the entry in non-moveMode).
- `setBackHandler` that exits moveMode if active, otherwise finishes.
- `setConfirmHandler` that toggles moveMode.
- Standard `drawListOrEmpty` rendering via ListLayout.
- Standard hint display via ListRenderHelper.

### How to use
1. Subclass as a CRTP:
   ```cpp
   class MyOrderActivity final : public OrderListActivity<MyOrderActivity, MyEntry> {
   public:
     explicit MyOrderActivity(GfxRenderer& r, MappedInputManager& m)
         : OrderListActivity("MyOrder", r, m) {}

     void reloadEntries() override { entries_ = getMyEntries(); /* sort */ }
     void save() override { /* persist */ }
     void moveSelectedEntry(int delta) override {
       const int target = selectedIndex_ + delta;
       if (target < 0 || target >= static_cast<int>(entries_.size())) return;
       std::swap(entries_[selectedIndex_], entries_[target]);
       selectedIndex_ = target;
     }
     const char* getTitle() const override { return tr(STR_MY_ORDER_TITLE); }
     std::string getEntryTitle(MyEntry entry) const override { return entry.name; }
   };
   ```
2. Override `render(RenderLock&&)` if you need a custom layout
   (date header, subtitle, icon column, etc.). The default
   `OrderListActivity::render` calls `ListLayout::compute` + a standard
   header + `drawListOrEmpty` + standard hints.
3. For custom confirm behavior (e.g. hold-to-delete before reordering),
   override `handleConfirmHold(unsigned long heldMs)` — return true to
   consume the confirm press and skip the moveMode toggle.

### Why CRTP
The `Derived` type is needed to call `Derived::reloadEntries()`,
`Derived::save()`, `Derived::moveSelectedEntry()` from the base
callbacks (lambdas). The pure-virtual methods ensure derived classes
implement them.

### See also
- `ReaderMenuOrderActivity` — reference consumer.
- `ShortcutOrderActivity` — second reference consumer.
- `FavoritesOrderActivity` — consumer with `handleConfirmHold` override for hold-to-delete.

---

## 7. `ButtonNavigator::clampIndex` — List-State Clamp Helper

### When to use
You are clamping `selectedIndex` (or any other list cursor) into the valid
`[0, totalItems)` range **after** the list has been rebuilt. Typical
post-condition of a `reloadEntries()`, a result handler from a sub-activity
(e.g. `startActivityForResult(...)`), or a filter change.

### How to use
```cpp
#include "util/ButtonNavigator.h"

selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(entries.size()));
```

### Why not use `std::clamp` / `std::min` / `std::max`?
- `std::clamp(selectedIndex, 0, totalItems - 1)` — silently wrong on `totalItems == 0` (becomes `0`, -1, but with an "off-by-one" risk if totalItems is `int` and `totalItems - 1` is negative).
- `std::min(selectedIndex, totalItems - 1)` — overflow when `totalItems == 0`.
- `std::max(0, totalItems - 1)` — loses the original `selectedIndex` if it was already valid.
- `if (selectedIndex >= size) { selectedIndex = std::max(0, size - 1); }` — verbose, three lines, off-by-one-prone.

`ButtonNavigator::clampIndex(current, total)` collapses all of the above
into a single safe call:
- Returns `0` when `total <= 0` (empty-list safe).
- Returns `total - 1` when `current >= total`.
- Returns `current` unchanged when `current` is in range.

### Forbidden patterns
The following manual patterns are now banned in Steroids list code (see §4
for the full "Forbidden patterns" block):
```cpp
// BANNED:
if (selectedIndex >= size) { selectedIndex = std::max(0, size - 1); }
// or
selectedIndex = std::clamp(selectedIndex, 0, size - 1);
// USE INSTEAD:
selectedIndex = ButtonNavigator::clampIndex(selectedIndex, size);
```

### See also
- The `clampIndex` family in `ButtonNavigator` (header only):
  `clampIndex`, `nextIndex`, `previousIndex`, `nextPageIndex`, `previousPageIndex`.
- Used in every `OrderListActivity` consumer (ReaderMenuOrderActivity,
  ShortcutOrderActivity, FavoritesOrderActivity) and in every
  recently-migrated list screen (AppsActivity, OpdsServerListActivity,
  ReaderMenuOrderActivity, …).

---

## 8. Optimization Patterns (Heap & RAM)

### 7.1 Skip font work on empty text
Any user-configurable text overlay (screensaver, sleep, custom wallpaper)
**MUST** check `text_overlay::shouldDraw()` before calling
`FontDecompressor::restoreFontMemory()` or `FontCacheManager::prewarmCache()`.
On ESP32-C3 these routines hold ~40-48 KB of heap. When the user has cleared
the text, the screensaver should not allocate it at all.

```cpp
if (text_overlay::shouldDraw(myText)) {
  fcm->prewarmCache(cfg.fontId, myText, styleMask);
}
```

### 7.2 Clamp instead of std::min/std::max
Use `ButtonNavigator::clampIndex(current, total)`:
- Replaces `std::clamp(selectedIndex, 0, total - 1)`.
- Replaces `std::max(0, static_cast<int>(entries.size()) - 1)`.
- Replaces `if (selectedIndex >= size) { selectedIndex = std::max(0, size - 1); }`.
- Returns 0 on `total <= 0` (empty-list safe).
- Rounds up to `total - 1` if `current >= total`.

This is now the standard clamp for every list-screen state.

### 7.3 `mappedInput.mapLabels + GUI.drawButtonHints` → `ListRenderHelper::drawHints` / `drawStandardHints`
This 2-line ritual appeared in ~80 activities before the refactor. It
is now banned. The single-call replacement handles the `const` qualifier
correctly (drawHints accepts `const MappedInputManager&`).

### 7.4 `OrderListActivity` over hand-rolled order screens
Any "user can reorder entries" activity must derive from
`OrderListActivity<Derived, Entry>`. The base provides all of the
back/confirm/nav state machine plus the empty-state + standard hints
rendering. New order screens are ~30 lines instead of ~120.

---

## 9. Build & Footprint Reference

After all refactors, the current footprint is:
- **RAM 16.2% (53 180 B / 327 680 B)**
- **Flash 81.1% (5 312 697 B / 6 553 600 B)**

Every util is small enough to fit comfortably. The helpers themselves add
< 1 KB of code combined; the de-duplication savings dominate.

Build command (Steroids always):
```powershell
python -X utf8 -m platformio run -e default -j 16
```

Never build with `-e gh_release` for verification — that is release-only.

---

## 10. Pre-Merge Checklist (When Upgrading Upstream)

When merging a new upstream release, verify:

**Rendering / input pipeline (§1-7):**
- [ ] `src/util/TextOverlay.{h,cpp}` — Steroids-added, **never** overwrite from upstream.
- [ ] `src/util/TextDrawer.h` — Steroids-added, **never** overwrite from upstream.
- [ ] `src/activities/util/ListRenderHelper.h` — Steroids-added, **never** overwrite.
- [ ] `src/activities/util/ListInputMapper.h` — Steroids-added, **never** overwrite.
- [ ] `src/activities/util/ListLayout.h` — Steroids-added, **never** overwrite.
- [ ] `src/activities/util/OrderListActivity.h` — Steroids-added, **never** overwrite.

**Reusable activities:**
- [ ] `src/activities/util/ConfirmationActivity.{h,cpp}` — Steroids-added, **never** overwrite.
- [ ] `src/activities/util/FullScreenMessageActivity.{h,cpp}` — Steroids-added, **never** overwrite.

**Branch-introduced util modules (§12):**
- [ ] `src/util/ButtonNavigator.h` — the `clampIndex` / `nextIndex` / `previousIndex` / `nextPageIndex` / `previousPageIndex` extensions are Steroids; the rest of the class is pre-existing.
- [ ] `src/util/StringUtils.{h,cpp}` — Steroids-added.
- [ ] `src/util/PopupUtils.h` — Steroids-added.
- [ ] `src/util/WiFiUtils.{h,cpp}` — Steroids-added (centralized WiFi lifecycle).
- [ ] `src/util/ReadingStatsBackupManager.{h,cpp}` — Steroids-added.

**Pre-existing protected Steroids utilities (unchanged but still protected):**
- [ ] `src/util/HeaderDateUtils.{h,cpp}` — Steroids-added.
- [ ] `src/util/PngSleepRenderer.{h,cpp}` — Steroids-added; **never** take upstream `patch_pngdec.py` (incompatible).
- [ ] `src/util/SleepScreenCache.{h,cpp}` — Steroids-added.
- [ ] `src/util/SleepImageUtils.{h,cpp}` — Steroids-added.
- [ ] `src/SilentRestart.h` — Steroids-added `silentRestart*` family.
- [ ] `src/activities/reader/ReaderUtils.h` — Steroids button-action dispatch.

If upstream modifies an activity that uses these utils, **keep the
Steroids activity** and re-apply the `text_overlay::` / `text_draw::` /
`ListRenderHelper::` / `ListInputMapper::` / `OrderListActivity<>` /
`ButtonNavigator::clampIndex` / `StringUtils::` / `PopupUtils::` /
`WiFiUtils::` / `ReadingStatsBackup::` calls.

---

## 11. Document Maintenance

This file is the **first thing** a new Steroids contributor must read.
When adding a new shared util:
1. Add a row to the table in §1.
2. Add a full "How to use" section modeled on §2-7 and §12-13.
3. Update the "Pre-Merge Checklist" in §10.
4. Run the build command in §9 and verify the footprint delta.
5. Add a commit named `feat: <util name>` with a description of the
   refactor and a list of migrated consumers.

When adding a new app / screen that uses an existing util:
1. The util's "When to use" section already covers your use case.
2. **Do not** add a new copy-pasted variant. Add a new section only if
   the new util has a distinctly different API surface from the
   existing utility set (§1 + §12).
3. Reference this document in your PR description so reviewers know
   you used the canonical pattern.

When refactoring an existing screen that hand-rolls something a util
already provides:
1. Confirm the util's `When to use` matches your case.
2. Replace the hand-rolled code with the util call.
3. Build, run the affected screens on device.
4. Commit named `refactor: <activity> -> <util>` with the diff stat and
   any behavior preservation notes (e.g. random-position cache reset).

---

## 12. Other Steroids Utilities (Branch-Introduced)

The 7 utilities catalogued in §1 are the rendering/input pipeline. The
branch additionally introduced (or significantly extended) these
Steroids-specific utilities, all of which are also **protected from
upstream overwrites** and should be reused instead of re-invented:

### 12.1 `StringUtils` (`src/util/StringUtils.{h,cpp}`)

Safe string helpers that replace the hand-rolled `strncpy + NUL` ritual
which appeared in ~20 activities before this branch.

```cpp
#include "util/StringUtils.h"

// Replace strncpy(buf, s.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
StringUtils::copyToFixedBuffer(buf, sizeof(buf), s);

// Sanitize a string for use as a filename (replace invalid chars, trim).
StringUtils::sanitizeFilename(s, 100);  // maxBytes = 100 default

// Lowercase ASCII in-place.
auto lower = StringUtils::toLowerAscii(s);
```

Use `StringUtils::copyToFixedBuffer` everywhere instead of the
`strncpy` + manual `NUL`-terminate pattern. The "is the buffer really
NUL-terminated?" bug is gone.

### 12.2 `PopupUtils` (`src/util/PopupUtils.h`)

Three inline popup helpers that replace the
`requestUpdateAndWait + RenderLock + GUI.drawPopup + delay` ritual in
SettingsActivity, ReadingStatsActivity, SyncDayActivity, and all reader
timer toggle sites.

```cpp
#include "util/PopupUtils.h"

// Blocking popup with optional progress bar.
PopupUtils::showTransientPopup(*this, tr(STR_LOADING), 20 /*progress*/, 120 /*delayMs*/);

// Reader timer toggle feedback.
PopupUtils::showTimerPauseFeedback(renderer, /*nowPaused=*/true);

// Generic error toast.
PopupUtils::showErrorToast(renderer, tr(STR_ERROR_GENERAL_FAILURE), 500);
```

### 12.3 `WiFiUtils` (`src/util/WiFiUtils.{h,cpp}`)

Centralizes every WiFi radio lifecycle action. Replaces ~10 copies of
the same `WiFi.disconnect() + softAPdisconnect() + WiFi.mode()` etc.
sequences that used to live in WifiSelectionActivity, ClockSyncActivity,
KOReaderAuthActivity, OtaUpdateActivity, FontDownloadActivity, and
CrossPointWebServerActivity.

```cpp
#include "util/WiFiUtils.h"

// Full shutdown before a non-network activity.
WiFiUtils::wifiOff();

// Disconnect + silent restart.
WiFiUtils::gracefulDisconnectAndSilentRestart();

// Prepare for STA scanning / connecting.
WiFiUtils::enterStationMode();
WiFiUtils::disableModemSleep();           // keep radio responsive on weak networks
WiFiUtils::abortAutoConnectAndClearNvs();  // WifiSelectionActivity pre-scan

// Web-server teardown.
WiFiUtils::stopAp();

// Deep-sleep / power-off paths.
WiFiUtils::forceDisconnect();
WiFiUtils::powerOff();
```

The `disableNvsAutoPersist()` helper is **critical** for the
WifiCredentialStore: the Arduino core tries to auto-persist WiFi
credentials in a hidden NVS partition, which would silently overwrite
our on-SD store. Every WiFi boot path must call this once.

### 12.4 `ReadingStatsBackupManager` (`src/util/ReadingStatsBackupManager.{h,cpp}`)

Centralizes the path constants + day-ordinal helpers used by
auto-backup, export, and import flows. Replaces inline `strncpy` +
ad-hoc date math scattered across the reading-stats code paths.

```cpp
#include "util/ReadingStatsBackupManager.h"

constexpr char READING_STATS_FILE_JSON[]        // "/.crosspoint/reading_stats.json"
constexpr char READING_STATS_BACKUP_FILE_JSON[] // "/.crosspoint/reading_stats.json.bak"
constexpr char READING_STATS_SUMMARY_JSON[]    // "/.crosspoint/summary.json"
constexpr char READING_STATS_EXPORT_DIR[]      // "/exports"
constexpr size_t MAX_READING_STATS_AUTO_BACKUPS = 30;

// Use the helpers rather than the raw paths in any new code.
auto path = ReadingStatsBackup::getAutoBackupPathForDayOrdinal(dayOrdinal);
ReadingStatsBackup::pruneAutoBackupsToLimit(30);
```

The branch also exports `textWindowShowsReadingStatsData()` and
`statsFileAppearsToHaveData()` for the data-validation pre-flight that
runs before any import or import-preview.

### 12.5 `ButtonNavigator` extensions (`src/util/ButtonNavigator.h`)

The pre-existing `ButtonNavigator` class was extended with the
`clampIndex` / `nextIndex` / `previousIndex` / `nextPageIndex` /
`previousPageIndex` static helpers (see §7). These helpers are pure
math (`int → int`); they are the canonical way to keep `selectedIndex`
in range after a `reloadEntries()` or a sub-activity result handler.

The class itself wraps `ButtonNavigator::onNext(…)` /
`onNextPress(…)` / `onNextRelease(…)` / `onNextContinuous(…)` /
`onPressAndContinuous(…)` — but in **new** code, prefer
`ListInputMapper` (§5) which provides a single coherent setup that
uses these internally.

### 12.6 Pre-existing Steroids utilities (kept unchanged)

These utilities were already in the codebase before the refactor branch
and are NOT touched by this work, but they are still the canonical
helpers for their respective domains:

- `HeaderDateUtils` (`src/util/HeaderDateUtils.{h,cpp}`) — the
  `drawHeaderWithDate(renderer, title, subtitle)` helper used by
  Activities that show a date in the header strip.
- `PngSleepRenderer` (`src/util/PngSleepRenderer.{h,cpp}`) — the
  Steroids-specific PNG sleep/screensaver decoder. **Never** merge
  upstream `patch_pngdec.py` (see STEROIDS-ALIGN-TO-UPSTREAM.md).
- `SleepScreenCache` (`src/util/SleepScreenCache.{h,cpp}`) —
  SleepScreenCache::load / save helpers for the 1-bit screen
  pre-render cache.
- `SleepImageUtils` (`src/util/SleepImageUtils.{h,cpp}`) — directory
  resolution + image-listing helpers used by SleepActivity,
  ScreenSaverActivity, and their previews.
- `SilentRestart` (`src/SilentRestart.h`) — `silentRestart()`,
  `silentRestartToHome()`, `silentRestartToApps()`,
  `silentRestartToReader()`, `silentRestartToPluginBrowser()`,
  `silentRestartToPlugin(name, fromApps, returnToPluginBrowser)`.
  All non-trivial exits from network activities MUST go through one
  of these to avoid heap fragmentation between sessions.
- `ReaderUtils` (`src/activities/reader/ReaderUtils.h`) — button
  action dispatch, legacy long-press migration, and reader-side
  helpers.
- `ShortcutRegistry` / `ShortcutUiMetadata`
  (`src/util/ShortcutRegistry.{h,cpp}`,
  `src/util/ShortcutUiMetadata.h`) — the shortcut config layer
  used by the Apps hub + Home.
- `AchievementPopupUtils` (`src/util/AchievementPopupUtils.h`) — the
  on-reader achievement toast.

---

## 13. Main / Loop / Boot Patterns

`src/main.cpp` is the second-most-edited file in the codebase. The
following patterns are mandated after the post-refactor cleanup pass.

### 13.1 Power-button state machine (3-phase)

The main loop's power-button handling is split into 3 sub-functions
inside an anonymous namespace, all backed by a `PowerButtonState`
struct at namespace scope so the values survive across `loop()`
iterations:

  - `handlePowerButtonPressEdge()` — record start time; remember if a
    screen saver is active so the release edge suppresses
    `shortPwrBtn` (the wake-key event is consumed by the screen
    saver).
  - `handlePowerButtonLongPressHold()` — held >= configured duration
    → if reader + `screenSaverReplaceSleep` + battery ok, push the
    replacement screensaver; otherwise `enterDeepSleep()`. Returns
    `true` when the caller MUST return from `loop()`.
  - `handlePowerButtonReleaseEdge()` — `FORCE_REFRESH` redraws the
    screen on every short press; `TOGGLE_STATUS_BAR` and `PAGE_TURN`
    are handled inside the reader activity's `loop()`, not here.

Each phase returns a "must return" bool so the loop body stays flat:

```cpp
if (handlePowerButtonPressEdge()) return;
if (handlePowerButtonLongPressHold()) return;
handlePowerButtonReleaseEdge();
```

New code MUST follow this 3-phase pattern. The 4 static variables
that used to live at the top of `loop()` are now grouped inside
`PowerButtonState`, so reset locations are visible in one place.

### 13.2 Boot phase comment blocks

`setup()` is sectioned into 7 PHASE blocks with a leading
`// =====` comment banner. Every contributor who adds a new boot
stage MUST add a new PHASE banner, not append inline to an existing
phase. The current phases are:

  1. Hardware init (HalSystem, BoardConfig, GPIO, PRNG, gamma LUT, WiFi utils)
  2. Storage + recovery (SD card, SdFat date callback, panic check, `BootRecovery::initialize`)
  3. Core settings + UI theme (Settings, Language, KOReader, OPDS, UiTheme)
  4. Wakeup handling (PowerButton / AfterUSBPower / AfterFlash / Other)
  5. Display + fonts + boot screen
  6. Data stores that don't block boot (State, ReadingStats, RecentBooks, Favorites, Flashcards, Achievements)
  7. Route decision + boot completion (crash report, reader resume, apps, plugin, Home)

### 13.3 `BootRecovery::runBootStage()` helper

The 6-times-repeated `if (shouldSkip) logSkip else enterStage + loader + log heap`
ritual in `setup()` is consolidated into a single helper:

```cpp
if (BootRecovery::runBootStage(BootRecovery::BootStage::Settings,
                               BootRecovery::shouldSkipSettings(),
                               "settings",
                               [] { SETTINGS.loadFromFile(); })) {
  imageRenderConfigApplySettings();
  LOG_DBG("BOOT", "After settings: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}
```

The 4 deferred-load sites (Favorites, Flashcards, Achievements,
ReadingStats) pass `nullptr` for `loader` and use the bool return
only as a "did we enter the stage?" signal for the heap diagnostic.
`BootRecovery::setSkipLogFn()` plugs a custom skip-log emitter so
the helper routes skip events through `CPR_VCODEX_LOG_EVENT` (so
the cpr-vcodex-logs recovery file picks them up).

### 13.4 `renderBatteryShutdownScreen()`

The "battery critically low" shutdown path is extracted from
`loop()` so the same layout is not duplicated when the path is
reached from a different call site (e.g. a future
`critical_battery_pending` from a reader timer). The function is
forward-declared above `enterDeepSleep()` and defined immediately
after it, so the call chain `battery check → shutdown screen →
enter deep sleep` reads top-to-bottom.

### 13.5 Silent-reboot subsystem grouping

All silent-reboot declarations are grouped in one anonymous
namespace block right above the `silentRestart*()` wrappers:

  - The 4 `SILENT_REBOOT_TARGET_*` constants (`HOME`, `APPS`,
    `PLUGIN`, `PLUGIN_BROWSER`). `SILENT_REBOOT_TARGET_READER` was
    removed as dead code; the reader-resume path is handled by the
    non-silent-reboot branch in `setup()` (the
    `bootToHome = false` arm reads `APP_STATE.openEpubPath` and
    routes to the reader).
  - The 5 `silentReboot*` `RTC_NOINIT_ATTR` variables
    (`silentRebootMagic`, `silentRebootTarget`, etc.).
  - `deepSleepInProgress` latch.
  - The `requestSilentRestart()` helper.

The `deepSleepInProgress` guard is checked ONCE inside
`requestSilentRestart()`. The `silentRestart*()` wrappers do NOT
re-check it — that was previously 4-times-repeated dead code
because the wrapper-level `if (deepSleepInProgress) return;` was
short-circuited by the same check inside `requestSilentRestart()`.

`silentRestart*()` and `requestSilentRestart()` MUST stay together.
The "Definitions for SilentRestart.h" comment is on the namespace
block, not on each individual declaration.

### 13.6 Local `setupStartMs` not file-scope `t1`

The `t1` / `t2` globals at the top of `main.cpp` were removed. `t2`
was assigned in `verifyPowerButtonDuration` but never read (true
dead code). `t1` is now a local `setupStartMs` inside `setup()`
that only the boot-time `LOG_INF` reads. The `BOOT-TIME` log line is
the ONLY consumer.

New code MUST NOT add file-scope `unsigned long` "calibration"
variables. Use local statics or struct-typed state at namespace
scope instead.

### 13.7 `Activity::onGoHome()` / `onSelectBook()` are stable

The 22 call sites across 13 activities (`OpdsBookBrowser`,
`RecentBooks`, `HomeActivity`, `FileBrowser`, `ScreenSaverActivity`,
`LibraryActivity`, `ReadingStatsDetail`, …) all use
`Activity::onGoHome()` and `Activity::onSelectBook(path)` to
forward to `ActivityManager`. These convenience forwarders are
intentionally kept on the `Activity` base class (NOT marked for
removal) for call-site brevity. New code MUST keep using them
rather than calling `activityManager.goHome()` / `goToReader()`
directly.
