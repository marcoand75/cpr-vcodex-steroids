# UPSTREAM-ALIGNMENT-REMAINING-PLAN.md

Plan for porting the remaining upstream CPR-vCodex 1.5.0.20–1.5.0.22
changes from commit `5db401f5` → `2209cd51` that were NOT included in
Phase 1–8. These items were deferred because they require either device
testing, deep integration with the rendering pipeline, or Steroids-specific
preservation of existing behavior.

---

## 1. SdCardFont — Fragmentation-Resistant Bitmap Storage (835 line diff in cpp, 67 in h)

### Current State
Steroids `SdCardFont` uses a single contiguous `miniBitmap` allocation
(`uint8_t* miniBitmap` with `miniBitmapCap` tracking). On the ESP32-C3
with no PSRAM, large contiguous allocations (20–40 KB for a 2-bpp glyph
page) can fail when the heap is fragmented after extended reading sessions.

### Upstream Changes (commit 28af4189 within 1.5.0.20)
Upstream replaces the single `miniBitmap` pointer with a fixed array of
4 KiB chunks:

```cpp
// SdCardFont.h
static constexpr uint32_t MINI_BM_CHUNK_SHIFT = 12;  // 4096 bytes
static constexpr uint32_t MINI_BM_CHUNK_SIZE = 1u << MINI_BM_CHUNK_SHIFT;
static constexpr uint32_t MINI_BM_MAX_CHUNKS = 24;  // 96 KB max

struct PerStyle {
  uint8_t* miniBitmapChunks[MINI_BM_MAX_CHUNKS] = {};
  uint32_t miniBitmapChunkCount = 0;
  // ... removed: miniBitmap, miniBitmapCap
};
```

Key API additions:
- `TextGetter` callback type: `const char* (*)(const void* ctx, uint32_t index)`
  — allows prewarming from non-contiguous text sources (e.g. reader line array)
  without copying to a single buffer
- `prewarm(TextGetter, ctx, textCount, ...)` overload
- `miniGlyphBitmap(ctx, dataOffset)` — resolves a glyph from a chunk
- `onCoverageQuery(ctx, codepoint)` — static coverage callback for EpdFontData
- `prewarmStyle(..., bool loadKernLig)` — new parameter
- `buildAdvanceTable` takes `std::deque` instead of `std::vector`

### Files Affected
| File | Lines Changed |
|---|---|
| `lib/EpdFont/SdCardFont.cpp` | +481 / -354 |
| `lib/EpdFont/SdCardFont.h` | +56 / -11 |
| `lib/EpdFont/EpdFont.h/.cpp` | +2 / +12 |
| `lib/EpdFont/EpdFontData.h` | +4 |
| `lib/EpdFont/EpdFontFamily.h/.cpp` | +4 / +4 |
| `lib/EpdFont/FontDecompressor.h/.cpp` | +10 / +74 |
| `lib/EpdFont/SdCardFontManager.h/.cpp` | +3 / (changes) |
| `lib/EpdFont/SdCardFontRegistry.h/.cpp` | +4 / (changes) |
| `lib/GfxRenderer/GfxRenderer.h/.cpp` | +65 / +302 |
| `lib/GfxRenderer/FontCacheManager.h/.cpp` | +5 / +25 |
| `lib/GfxRenderer/Bitmap.h/.cpp` | changes |
| `lib/GfxRenderer/BitmapHelpers.h/.cpp` | +218 / +115 |

**Total: 37 files, +1100 / -27082 lines** (includes removing 12 built-in
Lexend font headers — ~24,000 lines)

### Risk Assessment
- **HIGH**: Deeply coupled to GfxRenderer's text measurement and rendering
  pipeline. The `TextGetter` callback pattern changes how text is iterated
  in `drawText` / `measureText` / `wrappedText`.
- **HIGH**: BitmapHelpers changes affect dithering/grayscale conversion
  which Steroids has heavily customized (imageDithering, gamma LUT,
  threshold tuning).
- **HIGH**: Removing built-in Lexend fonts breaks Steroids'
  `fontIds.h` / font family selection.
- **LOW**: The chunk-based allocation itself is a strict improvement and
  could be isolated.

### Recommended Approach
1. **Port ONLY the `SdCardFont` chunking changes** — not the GfxRenderer
   `TextGetter` callback or BitmapHelpers changes.
2. Apply `SdCardFont.h` + `SdCardFont.cpp` changes from upstream commit
   `28af4189`, keeping Steroids' existing `prewarm(const char*, ...)`
   single-buffer API (call the new chunked internals).
3. Skip the `TextGetter` callback overload and `buildAdvanceTable(deque)`
   change — Steroids uses `std::vector` and can keep it.
4. **Do NOT** port `onCoverageQuery` — Steroids' EpdFontData glyph miss
   handler differs.
5. **Do NOT** port BitmapHelpers/GfxRenderer rendering changes — too deeply
   intertwined with Steroids' image pipeline customizations.
6. **Do NOT** remove built-in Lexend fonts.
7. Test on actual X4 device with fragmented heap scenario.

### Estimated Effort
- 2–3 days for SdCardFont-only port + device testing
- 1–2 weeks for full rendering pipeline integration (not recommended)

### Dependencies
- None (SdCardFont is self-contained within the font cache layer)

---

## 2. HAL Crash Detection — PANIC_CAPTURE_MAGIC

### Current State
Steroids `HalSystem.cpp` already has:
- `__wrap_panic_abort()` — copies panic message to RTC memory
- `__wrap_panic_print_backtrace()` — captures stack frames to RTC memory
- `isRebootFromPanic()` — checks `ESP_RST_PANIC` / `ESP_RST_CPU_LOCKUP`
- `checkPanic()` — dumps panic info to `/crash_report.txt`
- `clearPanic()` — clears RTC memory

### Upstream Changes (1.5.0.22)
Adds a `panicCaptureMarker` (RTC_NOINIT_ATTR `volatile uint32_t`) set to
`PANIC_CAPTURE_MAGIC = 0x50414E49u` by the panic wrappers. `isRebootFromPanic()`
is extended to treat **watchdog resets** (`ESP_RST_INT_WDT`,
`ESP_RST_TASK_WDT`, `ESP_RST_WDT`) as panic reboots **only when** the
marker is set — distinguishing a watchdog-killed infinite loop (which
captured the marker) from a clean watchdog reset during normal operation.

Additional changes:
- `checkPanic()` validates `written == panicInfo.size()` before clearing the marker
- `clearPanic()` zeroes the marker
- `begin()` comment updated

### Files Affected
- `lib/hal/HalSystem.cpp` (+25 / -6 lines)

### Risk Assessment
- **LOW**: The `RTC_NOINIT_ATTR` attribute is already used correctly.
- **MEDIUM**: Watchdog reset detection is device-dependent — the ESP32-C3
  can generate watchdog resets during legitimate long-running operations
  (e.g. font decompression). If the marker isn't set in those cases (because
  no panic wrapper fired), the behavior is identical to current. Only
  watchdog resets that follow a captured panic message will be treated as
  crash reboots — which is the desired semantics.
- **MEDIUM**: Requires device testing to verify no false positives on
  legitimate watchdog events.

### Recommended Approach
1. Apply the full `HalSystem.cpp` diff from upstream 1.5.0.22
2. Add `#define PANIC_CAPTURE_MAGIC 0x50414E49u` after `MAX_PANIC_STACK_DEPTH`
3. Add `RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;` near other
   RTC_NOINIT declarations
4. Set `panicCaptureMarker = PANIC_CAPTURE_MAGIC;` in both `__wrap_panic_abort`
   and `__wrap_panic_print_backtrace`
5. Extend `isRebootFromPanic()` to check watchdog resets + marker
6. Update `checkPanic()` to validate write completeness before clearing
7. Update `clearPanic()` to zero the marker
8. Build + deploy to test device, trigger a watchdog reset to verify

### Estimated Effort
- 1–2 hours (small diff, but needs device testing)

### Dependencies
- None

---

## 3. Web Server — Serial Number

### Current State
Steroids `CrossPointWebServer.cpp` does NOT have `#include <esp_efuse.h>`
or `#include <esp_efuse_table.h>`, and `handleStatus()` does not report a
serial number.

### Upstream Changes (1.5.0.22)
Adds three lines to `handleStatus()`:

```cpp
char serialNumber[33] = {};
bool validSerial = false;
if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serialNumber, 256) == ESP_OK) {
  validSerial = serialNumber[0] != '\0' && serialNumber[0] != static_cast<char>(0xFF);
  for (size_t index = 0; validSerial && index < 32 && serialNumber[index] != '\0'; ++index) {
    validSerial = std::isprint(static_cast<unsigned char>(serialNumber[index])) != 0;
  }
}
doc["serial"] = validSerial ? serialNumber : "Not found";
```

Requires new includes:
```cpp
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <cctype>  // for std::isprint
```

### Files Affected
- `src/network/CrossPointWebServer.cpp` (+7 includes, +10 body lines)

### Risk Assessment
- **VERY LOW**: EFUSE read is a read-only operation; cannot brick the device.
- **LOW**: `ESP_EFUSE_USER_DATA` may be empty on devices that haven't been
  programmed — the code handles this gracefully with `"Not found"`.
- **LOW**: The serial number is read from the factory-programmed eFuse
  on ESP32-C3, which stores the MAC address at this field. It should always
  be populated on genuine Espressif modules.

### Recommended Approach
1. Add `#include <esp_efuse.h>`, `#include <esp_efuse_table.h>`, and
   `#include <cctype>` to `CrossPointWebServer.cpp`
2. Insert the serial-number block before `serializeJson(doc, json)` in
   `handleStatus()`
3. Verify on device that `/api/status` returns a 12-character hex serial
4. This is a trivial, self-contained change

### Estimated Effort
- 30 minutes (trivial change, no device testing strictly required
  but recommended)

### Dependencies
- None

---

## 4. FirmwareFlasher — runningPartitionChipId()

### Current State
Steroids `FirmwareFlasher.h` already has `BAD_CHIP` in the Result enum
(ported as part of Phase 2 for the `WRONG_DEVICE_ERROR` mapping). However:

1. `validateImageFile()` in Steroids' `FirmwareFlasher.cpp` does **NOT**
   perform chip_id validation — it only checks magic, segments, checksum,
   SHA256, and size.
2. The `runningPartitionChipId()` function does NOT exist in Steroids.
3. `resultName()` in Steroids does NOT handle `BAD_CHIP` (returns "?").

### Upstream Changes (1.5.0.22)
Adds `runningPartitionChipId()` — reads `chip_id` from offset 12 of the
running ESP image header via `esp_partition_read`. Caches the result in a
function-local static.

Adds chip_id validation to `validateImageFile()`:
```cpp
uint16_t imageChip = 0xFFFF;
std::memcpy(&imageChip, header + 12, sizeof(imageChip));
const uint16_t deviceChip = runningPartitionChipId();
if (deviceChip != 0xFFFF && imageChip != deviceChip) {
  LOG_ERR("FLASH", "validate: wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
  file.close();
  return Result::BAD_CHIP;
}
```

Also adds `BAD_CHIP` to `resultName()` and declares `runningPartitionChipId()`
in the header.

### Files Affected
| File | Change |
|---|---|
| `src/network/FirmwareFlasher.h` | +5 lines (`runningPartitionChipId` declaration; `BAD_CHIP` already present) |
| `src/network/FirmwareFlasher.cpp` | +29 lines (function + `resultName` case + validation in `validateImageFile`) |

### Risk Assessment
- **LOW**: The `esp_partition_read` API is already used elsewhere in the
  codebase. Reading from the running partition is safe.
- **MEDIUM**: On the X4 e-reader, the image verification path differs
  from stock ESP-IDF. The `chip_id` field at offset 12 may have a
  different meaning. The upstream code specifically notes this:
  *"The running slot is authoritative for this device even on X4 units
  whose image verification path differs from stock ESP-IDF."*
  However, Steroids should verify this works on X4 specifically.
- **MEDIUM**: The `BAD_CHIP` validation is defense-in-depth. If it
  incorrectly rejects valid firmware on the X4, it would block OTA
  updates entirely. Must be tested with both X3 and X4 images.
- **LOW**: `runningPartitionChipId()` returns 0xFFFF on failure, which
  causes the validation to be skipped (no false rejection).

### Recommended Approach
1. Add `runningPartitionChipId()` declaration to `FirmwareFlasher.h`
   (after `resultName()` declaration, inside `namespace firmware_flash`)
2. Add `BAD_CHIP` case to `resultName()` in `FirmwareFlasher.cpp`
3. Add chip_id validation block to `validateImageFile()` in
   `FirmwareFlasher.cpp` — insert after the magic check, before segment
   count validation
4. Test with X3 firmware on X3 device, X4 firmware on X4 device, and
   cross-device (should be rejected)
5. **CRITICAL**: Test cross-device rejection does NOT produce false
   positives on the X4 e-reader

### Estimated Effort
- 1 hour for the code changes
- 1–2 hours for device testing (X3 + X4)

### Dependencies
- Requires `esp_ota_ops.h` for `esp_ota_get_running_partition()`
- Requires `esp_partition.h` for `esp_partition_read()`
- Both are already included transitively in `FirmwareFlasher.cpp`

---

## Summary

| # | Item | Complexity | Risk | Status |
|---|---|---|---|---|
| 1 | SdCardFont fragmentation-resistant bitmap storage | HIGH | HIGH | **DEFER** — 37 files, 27K+ line diff |
| 2 | HAL PANIC_CAPTURE_MAGIC crash detection | LOW | MEDIUM | **TRIVIAL** — 31 lines, needs device test |
| 3 | Web Server serial number | TRIVIAL | LOW | **TRIVIAL** — 10 lines, self-contained |
| 4 | FirmwareFlasher runningPartitionChipId() | LOW | MEDIUM | **TRIVIAL** — 34 lines, needs X4 test |

### Recommended Next Steps
1. **Item 3** (Web Server serial): Port immediately — zero risk, useful for diagnostics
2. **Item 4** (FirmwareFlasher): Port with `BAD_CHIP` validation — defense-in-depth against wrong-device firmware
3. **Item 2** (HAL crash detection): Port after device testing on X4
4. **Item 1** (SdCardFont): Create separate task — requires rendering pipeline analysis
