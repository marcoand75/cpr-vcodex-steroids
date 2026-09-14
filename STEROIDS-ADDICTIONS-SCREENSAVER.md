# CPR-vCodex Steroids — Screensaver, Sleep & Deep-Sleep

> **SCOPE:** Complete technical reference for the dual-mode e-ink screensaver system and power management.

---

## 1. Architecture Overview

Two independent screensaver modes with separate configuration:

| Mode | Trigger | Config | Behavior |
|------|---------|--------|----------|
| **General** | Auto (idle) / Manual | `Settings → Screensaver` | Folder picker, sequential/shuffle, battery protection |
| **In-Book** | Long-press power (when reading) | `Settings → Screensaver (reading)` | Separate folder, keeps reader on stack for instant return |

---

## 2. General Screensaver

### 2.1 Features
- **Folder selector** with preview (`ScreenSaverDirActivity` + `ScreenSaverPreviewActivity`)
- **Sequential / shuffle** order
- **Automatic sleep bypass** — keeps display refreshed without full sleep cycles
- **Battery-protection deep sleep** — after configurable timeout
- **Wake-on-any-button** or **single custom button**
- **Sleep screen rotation** — short power press rotates image without full wake
- **4-gray-level BMP cycling** respects refresh settings

### 2.2 Transparent PNG Compositing (Unified)
```cpp
// 1. Snapshot current framebuffer to SD before drawing
//    - Screensaver: /.crosspoint/screensaver-caller.tmp
//    - Sleep: /.crosspoint/last_reader_page.bin
// 2. On every image change: restore snapshot first → transparent areas show original
// 3. File-based framebuffer cache (replaces 48 KB in-memory vector)
// 4. Heap-friendly PNG decoder: SD font caches cleared before decoding
// 5. PngSleepRenderer::releaseDecoder() returns heap on exit
```

### 2.3 Exit & Transitions
- **No forced full refresh** on exit — underlying activity re-renders naturally
- **Immediate render notification** on exit to avoid blank gaps

---

## 3. In-Book (Reader) Screensaver

### 3.1 Features
- **Separate folder + order** (`Settings → Screensaver (reading)`)
- **Launch from reader menu:** Select → "Screensaver"; any button exits to exact page
- **Replace sleep with screensaver:** Long-press power while reading → screensaver instead of deep sleep
  - Reader activity stays on stack for instant return
  - Battery minimum checks respected (below threshold → normal deep sleep)
  - Outside reading, power button behaves normally

### 3.2 Battery Safety
- Screensaver time **NOT counted** toward reading statistics
- Reading session timer correctly reset on screensaver dismiss

---

## 4. Deep-Sleep / Power Button Handling (`main.cpp`)

### 4.1 Upstream vs Steroids Difference

| Upstream | Steroids |
|----------|----------|
| Unconditional `enterDeepSleep()` on every power press | Short/long-press state machine |
| Only `SLEEP` mode works | All `shortPwrBtn` modes reachable |

### 4.2 Steroids State Machine
```cpp
// Variables: powerBtnDownMs, powerBtnInScreensaver

// SHORT PRESS (< getPowerButtonDuration(), 400 ms for non-SLEEP):
// → triggers configured shortPwrBtn action

// LONG PRESS (≥ threshold):
// → always deep-sleeps, OR
// → starts replacement screensaver (if configured for reader + battery ok)

// ACTIVE SCREENSAVER:
// → long-press check skipped so wake button works
// → release edge suppressed so dismiss doesn't fire shortPwrBtn
```

### 4.3 Key Functions
```cpp
handlePowerButtonPressEdge()      // Record start time, remember if screensaver active
handlePowerButtonLongPressHold()  // Held ≥ duration → replacement screensaver or deepSleep
handlePowerButtonReleaseEdge()    // FORCE_REFRESH redraws; TOGGLE_STATUS_BAR/PAGE_TURN in reader
```

---

## 5. Framebuffer Cache Optimization

| Before | After |
|--------|-------|
| In-memory 48 KB vector | File-based cache on SD |
| Persistent heap pressure | Heap released after decode |
| Single buffer | Snapshot + restore per frame |

---

## 6. Key Files

| File | Role |
|------|------|
| `src/activities/apps/ScreenSaverActivity.h/cpp` | General screensaver logic |
| `src/activities/apps/ScreenSaverDirActivity.h/cpp` | Folder selector with preview |
| `src/activities/apps/ScreenSaverPreviewActivity.h/cpp` | Image preview |
| `src/activities/apps/SleepAppActivity.h/cpp` | Sleep screen, rotation |
| `src/activities/apps/SleepPreviewActivity.h/cpp` | Sleep preview |
| `src/main.cpp` | Power button state machine |
| `src/util/PngSleepRenderer.h/cpp` | PNG decoding + compositing |
| `src/util/SleepScreenCache.h/cpp` | Sleep framebuffer cache |
| `src/util/SleepImageUtils.h/cpp` | Directory/image listing |
| `src/util/TextOverlay.h/cpp` | Text overlay on images |
| `src/activities/reader/ReaderUtils.h` | `canStartReplacementScreenSaver()` |

---

## 7. Settings (CrossPointSettings)

```cpp
// General screensaver
screenSaverDirectory      // char[128]
screenSaverOrder          // 0=sequential, 1=shuffle
screenSaverInterval       // minutes
screenSaverWakeButton     // button enum
screenSaverText           // char[64]
screenSaverFontSize       // uint8_t
screenSaverTextPosition   // enum
screenSaverTextStyle      // enum
screenSaverShowPanel      // bool
screenSaverPanelColor     // 0=black, 1=white
screenSaverPanelOpacity   // uint8_t
screenSaverMinBattery     // uint8_t (0-100)
screenSaverReplaceSleep   // bool

// Reader screensaver (separate)
screenSaverReaderDir
screenSaverReaderOrder
```

---

## 8. Text Overlay (`text_overlay::`)

### Usage Pattern (Mandatory for Heap Optimization)
```cpp
if (text_overlay::shouldDraw(myText)) {
    text_overlay::OverlayConfig cfg;
    cfg.position = SETTINGS.screenSaverTextPosition;
    cfg.textStyle = SETTINGS.screenSaverTextStyle;
    cfg.drawPanel = SETTINGS.screenSaverShowPanel;
    cfg.panelColor = SETTINGS.screenSaverPanelColor;
    cfg.cachedRandomPosition = &randomCache;
    text_overlay::resolveFontFromSize(SETTINGS.screenSaverFontSize, cfg.fontId, cfg.fontStyle);
    
    fcm->prewarmCache(cfg.fontId, myText, styleMask);  // Before image decode
    text_overlay::draw(renderer, myText, cfg);         // Per render pass
}
```

**Heap savings when text empty:** Skips `FontDecompressor::restoreFontMemory()` (~40-48 KB), `prewarmCache()`, all draw calls.

---

## 9. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (all screensaver files protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` §2, §12.6, §13.9
- **Library:** `STEROIDS-ADDICTIONS-LIBRARY.md` (unrelated)

---

*Last updated: 2026-09-14*