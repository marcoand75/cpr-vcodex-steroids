#pragma once

#include <cstdint>

// =============================================================================
// ImageRenderConfig - Centralized runtime image rendering configuration
//
// This header replaces the compile-time constexpr approach previously used in
// DitheringConfig.h. All image rendering parameters are now adjustable at
// runtime through the device settings UI.
//
// Each parameter has a hardcoded default that matches the historical behaviour
// and acts as a failover when settings are not yet loaded or are invalid.
//
// Supported parameters:
//   - Dithering on/off
//   - Gamma LUT on/off (and gamma value)
//   - 4-level grayscale thresholds for quantizeSimple()
//   - Dithering algorithm selection (Atkinson / Floyd-Steinberg)
// =============================================================================

// ---------------------------------------------------------------------------
// Runtime state — writeable globals populated once settings are loaded.
// ---------------------------------------------------------------------------
extern bool   g_imageRenderDitheringEnabled;   // Enable error-diffusion dithering
extern bool   g_imageRenderLutEnabled;         // Enable gamma LUT before quantization
extern float  g_imageRenderGamma;              // Gamma correction value (default 1.5)
extern uint8_t g_imageRenderThresholdBlack;    // Gray level below which = black (level 0)
extern uint8_t g_imageRenderThresholdDark;     // Gray level below which = dark gray (level 1)
extern uint8_t g_imageRenderThresholdLight;    // Gray level below which = light gray (level 2)
                                               // >= light threshold = white (level 3)
extern bool   g_imageRenderUseAtkinson;        // true = Atkinson, false = Floyd-Steinberg

// ---------------------------------------------------------------------------
// Failover default values (historical calibrated values for the X4 display).
// ---------------------------------------------------------------------------
constexpr bool   kDefaultDitheringEnabled = true;
constexpr bool   kDefaultLutEnabled       = true;
constexpr float  kDefaultGamma            = 1.5f;
constexpr uint8_t kDefaultThresholdBlack  = 50;
constexpr uint8_t kDefaultThresholdDark   = 120;
constexpr uint8_t kDefaultThresholdLight  = 200;
constexpr bool   kDefaultUseAtkinson      = true;  // Atkinson preferred, FS fallback

// ---------------------------------------------------------------------------
// Initialisation (lazy-safe: default values are set here in the .cpp).
// ---------------------------------------------------------------------------
void imageRenderConfigInit();          // Reset all values to failover defaults
void imageRenderConfigApplySettings(); // Pull values from CrossPointSettings (SETTINGS)
