#pragma once

#include <stdint.h>

#include <BoardConfig.h>

// ============================================================================
// Extended Xteink Detect — Completes freeink‑sdk with the UC8279 / UC8179
// controller‑fingerprint layer that CrossInk v1.5.0 carries in its own
// XteinkDetect.cpp but that is not present in the minimal upstream SDK.
//
// The functions here sit between the SDK's base I²C device fingerprint
// (freeink::detectXteinkIsX3) and the EInkDisplay driver initialisation.
// They must be called BEFORE SPI.begin() claims the EPD pins.
// ============================================================================

namespace freeink {

// ── X3 panel‑controller verdict ───────────────────────────────────────────

enum class X3DisplayVerdict : uint8_t { Uc8253Assumed, Uc8279Confirmed, Inconclusive };

// Bit‑bangs the UC8279 VER / FLG registers on the EPD pins and returns
// whether this X3 carries a UC8279d controller.  Safe to call before SPI
// owns the pins.  The caller should already know the board is an X3 via
// detectXteinkIsX3() or a local probe.
X3DisplayVerdict detectX3DisplayController(uint8_t verBytes[5] = nullptr, uint8_t* flg = nullptr);

// ── X4 factory‑replacement panel overlay ──────────────────────────────────

// Probes the display bus on the active BoardConfig profile and, if an
// UltraChip controller is detected, promotes the board's displayController
// field in BoardConfig::ACTIVE to its UltraChip sibling (SSD1677 →
// UC8179 / UC8279, UC8253 → UC8279).  Returns true when a promotion
// occurred.
bool applyXteinkDisplayController();

}  // namespace freeink
