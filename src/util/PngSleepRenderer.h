#pragma once

#include <string>

class GfxRenderer;

namespace PngSleepRenderer {

bool drawTransparentPng(const std::string& path, const GfxRenderer& renderer, int targetX, int targetY, int targetWidth,
                        int targetHeight, const char* storagePrefix = "SLP");

/// Destroy the PNG decoder object and release any internal heap allocations
/// made by PNGdec during decode. The ~38 KB decoder buffer itself is static
/// storage (not heap) and remains reserved for the lifetime of the device.
/// Call when the screensaver session exits, before rendering text/fonts,
/// to ensure PNGdec's internal buffers don't fragment the heap.
void releaseDecoder();

}  // namespace PngSleepRenderer