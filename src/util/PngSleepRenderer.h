#pragma once

#include <string>

class GfxRenderer;

namespace PngSleepRenderer {

bool drawTransparentPng(const std::string& path, const GfxRenderer& renderer, int targetX, int targetY, int targetWidth,
                        int targetHeight, const char* storagePrefix = "SLP");

}  // namespace PngSleepRenderer
