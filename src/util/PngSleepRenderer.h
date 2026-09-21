#pragma once

#include <string>

class GfxRenderer;

namespace PngSleepRenderer {

bool drawTransparentPng(const std::string& path, const GfxRenderer& renderer, int targetX, int targetY, int targetWidth,
                        int targetHeight, const char* storagePrefix = "SLP");

/// Free the lazy-allocated PNG decoder (sizeof(PNG) ~38 KB) so the heap
/// returns to its pre-screensaver state.  Call when the screensaver activity
/// exits, before the reader renders its next page.
void releaseDecoder();

}  // namespace PngSleepRenderer
