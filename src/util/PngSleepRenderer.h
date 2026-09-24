#pragma once

#include <string>

class GfxRenderer;

namespace PngSleepRenderer {

bool drawTransparentPng(const std::string& path, const GfxRenderer& renderer, int targetX, int targetY, int targetWidth,
                        int targetHeight, const char* storagePrefix = "SLP");

// Steroids-compat shim: this branch decodes PNGs with a per-call decoder that is
// freed at the end of drawTransparentPng(), so there is no persistent decoder to
// release. Kept so the ported screensaver sources stay identical to Steroids.
inline void releaseDecoder() {}

}  // namespace PngSleepRenderer
