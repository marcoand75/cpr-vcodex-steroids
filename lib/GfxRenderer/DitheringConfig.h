#pragma once

#include <cstdint>

// =============================================================================
// DitheringConfig.h – Backward-compatibility re-exports for the image pipeline.
//
// This file previously held compile-time constexpr flags (USE_ATKINSON,
// USE_FLOYD_STEINBERG, GAMMA_VALUE). After the steroids image-rendering
// settings refactor those flags are now runtime-configurable globals
// declared in ImageRenderConfig.h.
//
// Keeping this header ensures all existing #include "DitheringConfig.h"
// sites continue to compile. New code should #include "ImageRenderConfig.h"
// directly.
// =============================================================================

#include "ImageRenderConfig.h"

// ---------------------------------------------------------------------------
// Legacy names – still used throughout the rendering pipeline.
// These are now aliases to the runtime globals so behaviour follows the
// current settings without changing every call site.
// ---------------------------------------------------------------------------

// GRAY_LEVEL_x remain compile-time constants (panel-native gray levels).
constexpr uint8_t GRAY_LEVEL_0 = 0;
constexpr uint8_t GRAY_LEVEL_1 = 85;
constexpr uint8_t GRAY_LEVEL_2 = 170;
constexpr uint8_t GRAY_LEVEL_3 = 255;

// 8-bit -> gamma-corrected lookup table (rebuilt when gamma changes).
extern uint8_t gammaLUT[256];

// Fill gammaLUT once at startup (out = 255 * (in/255)^(1/gamma)).
// Now accepts an optional gamma parameter; if <= 0 the current runtime
// gamma (g_imageRenderGamma) is used.
void initGammaLUT();
void initGammaLUT(float gamma);
