# CrossInk v1.5.0-rc-3 Screen/Model Integration Plan

> Target: `cpr-vcodex-steroids`  
> Source: `https://github.com/uxjulia/CrossInk/tree/v1.5.0-rc-3`  
> Date: 2026-08-08  
> Status: Analysis complete — implementation **not yet started**

---

## Executive Summary

CrossInk v1.5.0-rc-3 has a mature multi-device abstraction layer (`freeink-sdk`) supporting Xteink X3, X3/Uc8279, X4, X4Pro, and Sticky devices. cpr-vcodex-steroids already contains significant cherry-picked fragments from CrossInk (EInkDisplay driver, HalGPIO fingerprinting, HalDisplay wrapper), but these are not fully wired up: the build flags that activate X3 detection are missing, there is no SPI mutex, no async refresh, no BoardConfig profiles, no frontlight, and no UC8279 panel variant detection.

This plan covers a 4-phase aggressive integration to bring cpr-vcodex-steroids to full multi-model parity with CrossInk v1.5.0-rc-3.

---

## Phase 1: Activate Existing X3 Code + SPI Safety

### 1.1 Build Flags (`platformio.ini`)

Add to `[env:default]`, `[env:gh_release]`, `[env:gh_release_rc]`, and `[env:slim]`:

```ini
build_flags =
    -DFREEINK_DEVICE_X4=1
    -DFREEINK_DEVICE_X3=1
    -DFREEINK_FIRMWARE_DEVICE_TYPE=\"x3-x4\"
```

**Impact**: Activates `#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3` blocks throughout HalGPIO, EInkDisplay, and HalDisplay — code that is already present but compiled out. This alone enables X3 I2C fingerprinting, display controller selection, and baseline X3 display operation.

### 1.2 SPI Bus Mutex (`lib/hal/HalSpiBus.h` + `.cpp`) — NEW

CrossInk wraps all SPI operations in a recursive mutex to prevent concurrent access from async refresh, touch, SD card, and display.

**Files to create:**
- `lib/hal/HalSpiBus.h`
- `lib/hal/HalSpiBus.cpp`

**Key pattern:**
```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class HalSpiBus {
public:
    static void begin();
    static SemaphoreHandle_t mutex();

    class Lock {
    public:
        Lock();
        ~Lock();
    private:
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };
};
```

**Implementation:**
```cpp
#include "HalSpiBus.h"
static SemaphoreHandle_t s_spiMutex = nullptr;

void HalSpiBus::begin() {
    s_spiMutex = xSemaphoreCreateRecursiveMutex();
}

SemaphoreHandle_t HalSpiBus::mutex() { return s_spiMutex; }

HalSpiBus::Lock::Lock()  { xSemaphoreTakeRecursive(s_spiMutex, portMAX_DELAY); }
HalSpiBus::Lock::~Lock() { xSemaphoreGiveRecursive(s_spiMutex); }
```

**Integration points:**
- Call `HalSpiBus::begin()` early in `main.cpp` (after GPIO init, before display init)
- Wrap `HalDisplay::displayBuffer()` and `displayBufferAsync()` with `HalSpiBus::Lock spiLock;`

---

## Phase 2: Display Performance Features

### 2.1 Async Refresh (`HalDisplay.h/cpp`)

CrossInk supports non-blocking panel refresh on X4 hardware. X3 falls back to synchronous.

**Add to `lib/hal/HalDisplay.h`:**
```cpp
bool supportsAsyncRefresh() const;
bool supportsAsyncGrayscaleBase() const;
void displayBufferAsync(RefreshMode mode);
void displayBufferAsyncNoShadow(RefreshMode mode);
void waitRefreshComplete();
```

**Add to `lib/hal/HalDisplay.cpp`:**
```cpp
bool HalDisplay::supportsAsyncRefresh() const {
    return !gpio.deviceIsX3();
}

bool HalDisplay::supportsAsyncGrayscaleBase() const {
    return !gpio.deviceIsX3();
}

void HalDisplay::displayBufferAsync(RefreshMode mode) {
    if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
        einkDisplay.requestResync(1);
    }
    HalSpiBus::Lock spiLock;
    einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode));
}

void HalDisplay::waitRefreshComplete() {
    einkDisplay.waitRefreshComplete();
}
```

**Prerequisite**: `EInkDisplay` must expose `displayBufferAsyncNoShadow()` and `waitRefreshComplete()`. These exist in CrossInk's FreeInkDisplay but need verification against cpr's `open-x4-sdk` version.

### 2.2 FrameBuffer Lending (`HalDisplay.h/cpp`)

Zero-copy heap borrowing for memory-hungry phases (chapter builds, image decoding).

```cpp
uint8_t* lendFrameBufferStorage();
void returnFrameBufferStorage(uint8_t* buffer);
```

**Implementation:**
```cpp
uint8_t* HalDisplay::lendFrameBufferStorage() {
    return einkDisplay.lendFrameBufferStorage();
}
void HalDisplay::returnFrameBufferStorage(uint8_t* buffer) {
    einkDisplay.returnFrameBufferStorage(buffer);
}
```

### 2.3 Grayscale Base Display Fix

CrossInk's `displayGrayscaleBase()` handles X3 FAST_REFRESH fallback (cpr currently only handles HALF_REFRESH):

```cpp
void HalDisplay::displayGrayscaleBase(RefreshMode mode) {
    if (gpio.deviceIsX3()) {
        if (mode == RefreshMode::HALF_REFRESH || mode == RefreshMode::FAST_REFRESH) {
            einkDisplay.requestResync(1);
        }
    }
    HalSpiBus::Lock spiLock;
    einkDisplay.displayGrayscaleBase(convertRefreshMode(mode));
}
```

---

## Phase 3: Multi-Device Hardware Abstraction

### 3.1 BoardConfig Library

CrossInk's `freeink-sdk/libs/hardware/BoardConfig/` provides a unified pin mapping for X3, X3Uc8279, X4, X4Pro, and Sticky boards. cpr-vcodex-steroids currently hardcodes X4 pins.

**Decision point**: Either:
- **Option A**: Migrate `open-x4-sdk` → `freeink-sdk` (high reward, gets all libraries)
- **Option B**: Build a minimal `BoardConfig` equivalent within `lib/hal/` (lower risk, more manual work)

**If Option B, create `lib/hal/HalBoardConfig.h`:**

```cpp
#pragma once
#include <cstdint>

namespace HalBoardConfig {
    enum class Board : uint8_t {
        Unknown = 0,
        XteinkX3 = 1,
        XteinkX3Uc8279 = 2,
        XteinkX4 = 3,
        XteinkX4Pro = 4,
        Sticky = 5,
    };

    struct PinMap {
        int8_t epdSclk, epdMosi, epdMiso, epdCs, epdBusy, epdDc, epdRst;
        int8_t sdClk, sdCmd, sdD0, sdD1, sdD2, sdD3;
        int8_t i2cSda, i2cScl;
        int8_t batAdc;
        // ... button pins, touch pins, frontlight pins
    };

    extern Board activeBoard;
    extern const PinMap* activePins;

    void selectDevice(Board board);
    const PinMap& pinsFor(Board board);
}
```

### 3.2 UC8279 Panel Controller Detection (`lib/hal/HalPanelDetect.h/cpp`) — NEW

CrossInk bit-bangs the UC8279 controller's VER/FLG registers to distinguish UC8253 from UC8279 panels on X3 devices. Newer production X3 units ship UC8279d.

**Files to create:**
- `lib/hal/HalPanelDetect.h`
- `lib/hal/HalPanelDetect.cpp`

**Key function signatures:**
```cpp
namespace HalPanelDetect {
    bool detectX3DisplayIsUc8279();
    void applyXteinkDisplayController();  // X4Pro UC8179 resolution
}
```

**Integration in `HalGPIO::begin()`:**
```cpp
if (deviceIsX3()) {
    const bool x3IsUc8279 = HalPanelDetect::detectX3DisplayIsUc8279();
    HalBoardConfig::selectDevice(
        x3IsUc8279 ? HalBoardConfig::Board::XteinkX3Uc8279
                   : HalBoardConfig::Board::XteinkX3
    );
} else {
    HalBoardConfig::selectDevice(HalBoardConfig::Board::XteinkX4);
}
```

### 3.3 HalFrontlight (`lib/hal/HalFrontlight.h` + `.cpp`) — NEW

CrossInk wraps `FrontlightManager` from freeink-sdk. cpr-vcodex-steroids has no frontlight support.

**Files to create:**
- `lib/hal/HalFrontlight.h`
- `lib/hal/HalFrontlight.cpp`

**Key API:**
```cpp
class HalFrontlight {
public:
    void begin();
    void setBrightness(uint8_t cold, uint8_t warm);  // X4Pro dual PWM
    void setBrightness(uint8_t level);                // Single channel fallback
    uint8_t coldBrightness() const;
    uint8_t warmBrightness() const;
    bool hasFrontlight() const;
    void toggle();  // Power button double-click
};
```

**Integration in `main.cpp`:**
```cpp
// In handleX4ProFrontlightDoubleClick():
if (frontlight.hasFrontlight()) {
    frontlight.toggle();
}
```

---

## Phase 4: Touch and Input Extensions

### 4.1 Touch/Gesture API (`HalGPIO.h`)

CrossInk's HalGPIO exposes a touch/gesture API guarded by `#if CROSSINK_APP_CAP_TOUCH`. cpr-vcodex-steroids has none of this.

**Add to `lib/hal/HalGPIO.h`:**

```cpp
// Touch/gesture (stubs for non-touch devices)
bool hasTouch() const { return false; }
bool hasHomeKey() const { return false; }
bool wasHomeKeyPressed() const { return false; }
bool wasHomeKeyTapped() const { return false; }
bool wasHomeKeyLongPressed() const { return false; }
bool wasTouchTap(int16_t& x, int16_t& y) const { return false; }
bool wasTouchDown(int16_t& x, int16_t& y) const { return false; }
bool wasTouchReleased() const { return false; }
bool isTouchTapCandidate(int16_t& x, int16_t& y) const { return false; }
bool wasSwipe(int16_t& dx, int16_t& dy) const { return false; }
bool isTouchHeldAt(int16_t& x, int16_t& y) const { return false; }
uint32_t lastTouchHeldMs() const { return 0; }
bool wasTouchActivity() const { return false; }
```

### 4.2 Board Mode Detection

```cpp
bool isXteinkDevice() const;   // true for X3/X4/X4Pro
bool hasEdgeSideButtons() const; // X3 has edge buttons, X4 has side buttons
```

---

## Required `main.cpp` Changes

### Boot Flow Additions

```cpp
// After HalGPIO::begin():
HalSpiBus::begin();
HalPanelDetect::applyXteinkDisplayController(); // X4Pro panel resolution

// After display init:
HalFrontlight frontlight;
frontlight.begin();

// In input loop:
handleX4ProFrontlightDoubleClick(frontlight);
```

### Recovery Mode

CrossInk's Power+Side button recovery mode (already in cpr, verify completeness).

### BootResume Enum

CrossInk's `BootResume` enum (Splash, Silent, Network, QuickResume) — verify cpr already has equivalent.

---

## Files Changed Summary

| File | Action | Phase |
|------|--------|-------|
| `platformio.ini` | Add `-DFREEINK_DEVICE_X4=1 -DFREEINK_DEVICE_X3=1` flags | 1 |
| `lib/hal/HalSpiBus.h` | **CREATE** — SPI recursive mutex | 1 |
| `lib/hal/HalSpiBus.cpp` | **CREATE** — SPI mutex implementation | 1 |
| `lib/hal/HalDisplay.h` | Add async refresh, framebuffer lending, grayscale fix | 2 |
| `lib/hal/HalDisplay.cpp` | Implement async/lending/grayscale fix | 2 |
| `lib/hal/HalBoardConfig.h` | **CREATE** — Multi-board pin profiles | 3 |
| `lib/hal/HalBoardConfig.cpp` | **CREATE** — Board selection implementation | 3 |
| `lib/hal/HalPanelDetect.h` | **CREATE** — UC8279/UC8253 detection | 3 |
| `lib/hal/HalPanelDetect.cpp` | **CREATE** — Bit-bang panel detection | 3 |
| `lib/hal/HalFrontlight.h` | **CREATE** — Frontlight HAL | 3 |
| `lib/hal/HalFrontlight.cpp` | **CREATE** — Frontlight PWM control | 3 |
| `lib/hal/HalGPIO.h` | Touch stubs, board mode detection | 4 |
| `lib/hal/HalGPIO.cpp` | UC8279 detection call, BoardConfig integration | 3 |
| `src/main.cpp` | HalSpiBus init, frontlight init, boot flow additions | 1-4 |
| `open-x4-sdk/libs/display/EInkDisplay/` | Verify `displayBufferAsyncNoShadow`, `waitRefreshComplete`, `lendFrameBufferStorage` exist | 2 |

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| **open-x4-sdk vs freeink-sdk divergence** — EInkDisplay APIs may differ | HIGH | Verify each missing method exists in open-x4-sdk before porting; if not, add them to EInkDisplay first |
| **Heap pressure** — BoardConfig + FrontlightManager + Rtc libraries add flash/RAM usage | MEDIUM | ESP32-C3 has ~380KB usable RAM; use `custom_sdkconfig` optimizations from CrossInk, enable `lendFrameBufferStorage` |
| **Build flag propagation** — New flags may trigger dead code paths | MEDIUM | Incremental builds after each phase; test `pio run -e default` |
| **UC8279 bit-banging** — GPIO manipulation during boot may interfere with other init | LOW | CrossInk has proven this on real hardware |
| **SPI mutex deadlocks** — Recursive mutex misuse | LOW | Single acquisition pattern verified against CrossInk |
| **X4Pro/S3 support** — Requires ESP32-S3 toolchain and different MCU | LOW | Phase 4 only; cpr target devices remain C3-based |

---

## Build Verification Commands

After each phase:

```powershell
python -X utf8 -m platformio run -e default -j 16
```

After all phases, also verify release build:

```powershell
python -X utf8 -m platformio run -e gh_release -j 16
```

RAM budget check:

```powershell
python -X utf8 scripts/firmware_budget_report.py
```

---

## References

- CrossInk v1.5.0-rc-3: `https://github.com/uxjulia/CrossInk/tree/v1.5.0-rc-3`
- CrossInk freeink-sdk: `https://github.com/uxjulia/freeink-sdk` (submodule)
- cpr-vcodex open-x4-sdk: `open-x4-sdk/`
- Upstream skill: `.agents/skills/cpr-upstream-sync/SKILL.md`
- Firmware constraints: `agent-docs/firmware-constraints.md`
- Upstream sync guide: `agent-docs/upstream-sync.md`
