#include "ImageRenderConfig.h"

// CrossPointSettings is pulled in by imageRenderConfigApplySettings() only;
// the rest of the file stays independent of the settings layer so tests can
// use it without dragging in the full device settings infrastructure.
//
// Use a relative path because lib/ directories cannot see src/ via -I flags.
#include "../../src/CrossPointSettings.h"

// =============================================================================
// Default values initialised at static-init time (before settings are loaded).
// These are the "failover" defaults matching the historic X4 calibration.
// =============================================================================
bool    g_imageRenderDitheringEnabled = kDefaultDitheringEnabled;
bool    g_imageRenderLutEnabled       = kDefaultLutEnabled;
float   g_imageRenderGamma            = kDefaultGamma;
uint8_t g_imageRenderThresholdBlack   = kDefaultThresholdBlack;
uint8_t g_imageRenderThresholdDark    = kDefaultThresholdDark;
uint8_t g_imageRenderThresholdLight   = kDefaultThresholdLight;
bool    g_imageRenderUseAtkinson      = kDefaultUseAtkinson;

// ---------------------------------------------------------------------------
// imageRenderConfigInit  –  Reset every value to the hardcoded failover defaults.
// Called early during boot before settings are parsed, so any image work
// before the settings store is ready uses safe values.
// ---------------------------------------------------------------------------
void imageRenderConfigInit() {
    g_imageRenderDitheringEnabled = kDefaultDitheringEnabled;
    g_imageRenderLutEnabled       = kDefaultLutEnabled;
    g_imageRenderGamma            = kDefaultGamma;
    g_imageRenderThresholdBlack   = kDefaultThresholdBlack;
    g_imageRenderThresholdDark    = kDefaultThresholdDark;
    g_imageRenderThresholdLight   = kDefaultThresholdLight;
    g_imageRenderUseAtkinson      = kDefaultUseAtkinson;
}

// ---------------------------------------------------------------------------
// imageRenderConfigApplySettings  –  Pull current values from CrossPointSettings
// and push them into the global runtime variables.
//
// Call this AFTER settings have been loaded (and after any settings change
// via the UI / web API) so all image consumers pick up the new values.
// Invalid ranges are clamped to safe defaults.
// ---------------------------------------------------------------------------
void imageRenderConfigApplySettings() {
    const auto& s = SETTINGS;

    g_imageRenderDitheringEnabled = (s.imageDitheringEnabled != 0);
    g_imageRenderLutEnabled       = (s.imageLutEnabled != 0);
    g_imageRenderGamma            = static_cast<float>(s.imageGamma) / 10.0f;

    // Clamp thresholds to ensure strictly increasing order and valid range.
    uint8_t tb = s.imageThresholdBlack;
    uint8_t td = s.imageThresholdDark;
    uint8_t tl = s.imageThresholdLight;

    if (tb < 1)  tb = 1;
    if (tb > 253) tb = 253;
    if (td <= tb) td = tb + 1;
    if (td > 254) td = 254;
    if (tl <= td) tl = td + 1;
    if (tl > 255) tl = 255;

    g_imageRenderThresholdBlack  = tb;
    g_imageRenderThresholdDark   = td;
    g_imageRenderThresholdLight  = tl;

    g_imageRenderUseAtkinson     = (s.imageDitheringAlgorithm == 0);
}
