#include "BitmapHelpers.h"

#include <cstdint>
#include <cstring>  // Added for memset
#include <cmath>

#include "Bitmap.h"
#include "DitheringConfig.h"

// Gamma lookup table. Populated once by initGammaLUT() (called at boot);
// adjustPixel() also lazily initializes it so a forgotten call cannot produce
// a fully-black image.
uint8_t gammaLUT[256];
static bool gammaLUTReady = false;
static float lastGammaValue = -1.0f;  // Track last gamma so we can detect changes

void initGammaLUT() {
  initGammaLUT(g_imageRenderGamma);
}

void initGammaLUT(float gamma) {
  if (gamma <= 0.0f) gamma = g_imageRenderGamma;
  for (int i = 0; i < 256; ++i) {
    const float norm = static_cast<float>(i) / 255.0f;
    const float correct = std::pow(norm, 1.0f / gamma);
    gammaLUT[i] = static_cast<uint8_t>(correct * 255.0f + 0.5f);
  }
  lastGammaValue = gamma;
  gammaLUTReady = true;
}

// Apply the gamma-corrected luminance. The LUT maps the input 8-bit luminance
// so that midtones get a perceptually correct weight before 4-level quantization.
int adjustPixel(int gray) {
  // If the LUT is disabled via settings, return the input unchanged.
  if (!g_imageRenderLutEnabled) return gray;

  // If gamma changed since last init, rebuild the LUT.
  if (!gammaLUTReady || lastGammaValue != g_imageRenderGamma)
    initGammaLUT(g_imageRenderGamma);

  if (gray < 0) gray = 0;
  if (gray > 255) gray = 255;
  return gammaLUT[static_cast<uint8_t>(gray)];
}

// Simple quantization without dithering – divide into 4 levels.
// Thresholds now come from runtime settings (ImageRenderConfig globals).
uint8_t quantizeSimple(int gray) {
  if (gray < g_imageRenderThresholdBlack) {
    return 0;
  } else if (gray < g_imageRenderThresholdDark) {
    return 1;
  } else if (gray < g_imageRenderThresholdLight) {
    return 2;
  } else {
    return 3;
  }
}

// Reconstruct the 8-bit gray value that a 2-bit level represents on the panel.
uint8_t unquantize(uint8_t level) {
  switch (level) {
    case 0: return GRAY_LEVEL_0;
    case 1: return GRAY_LEVEL_1;
    case 2: return GRAY_LEVEL_2;
    default: return GRAY_LEVEL_3;
  }
}

// Main quantization function (2-bit, 4 levels).
uint8_t quantize(int gray, int /*x*/, int /*y*/) {
  return quantizeSimple(gray);
}

// 1-bit noise dithering for fast home screen rendering
// Uses hash-based noise for consistent dithering that works well at small sizes
uint8_t quantize1bit(int gray, int x, int y) {
  gray = adjustPixel(gray);

  // Generate noise threshold using integer hash (no regular pattern to alias)
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);  // 0-255

  // Simple threshold with noise: gray >= (128 + noise offset) -> white
  // The noise adds variation around the 128 midpoint
  const int adjustedThreshold = 128 + ((threshold - 128) / 2);  // Range: 64-192
  return (gray >= adjustedThreshold) ? 1 : 0;
}

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder) {
  if (!bmpHeader) return;

  // Zero out the memory to ensure no garbage data if called on uninitialized stack memory
  std::memset(bmpHeader, 0, sizeof(BmpHeader));

  uint32_t rowSize = (width + 31) / 32 * 4;
  uint32_t imageSize = rowSize * height;
  uint32_t fileSize = sizeof(BmpHeader) + imageSize;

  bmpHeader->fileHeader.bfType = 0x4D42;
  bmpHeader->fileHeader.bfSize = fileSize;
  bmpHeader->fileHeader.bfReserved1 = 0;
  bmpHeader->fileHeader.bfReserved2 = 0;
  bmpHeader->fileHeader.bfOffBits = sizeof(BmpHeader);

  bmpHeader->infoHeader.biSize = sizeof(bmpHeader->infoHeader);
  bmpHeader->infoHeader.biWidth = width;
  bmpHeader->infoHeader.biHeight = (rowOrder == BmpRowOrder::TopDown) ? -height : height;
  bmpHeader->infoHeader.biPlanes = 1;
  bmpHeader->infoHeader.biBitCount = 1;
  bmpHeader->infoHeader.biCompression = 0;
  bmpHeader->infoHeader.biSizeImage = imageSize;
  bmpHeader->infoHeader.biXPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biYPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biClrUsed = 2;
  bmpHeader->infoHeader.biClrImportant = 2;

  // Color 0 (black)
  bmpHeader->colors[0].rgbBlue = 0;
  bmpHeader->colors[0].rgbGreen = 0;
  bmpHeader->colors[0].rgbRed = 0;
  bmpHeader->colors[0].rgbReserved = 0;

  // Color 1 (white)
  bmpHeader->colors[1].rgbBlue = 255;
  bmpHeader->colors[1].rgbGreen = 255;
  bmpHeader->colors[1].rgbRed = 255;
  bmpHeader->colors[1].rgbReserved = 0;
}
