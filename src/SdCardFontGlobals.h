#pragma once

#include "SdCardFontSystem.h"

class GfxRenderer;

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;

// Ensure the correct SD card font family is loaded for current settings.
// Defined in main.cpp; call before entering the reader or after settings change.
extern void ensureSdFontLoaded();

// Reader-facing helper: ensure SD fonts are loaded only when the loaded
// family/size differs from current settings. Uses the font system generation
// counter to skip redundant work and reduce heap churn.
extern void onReaderResume();
