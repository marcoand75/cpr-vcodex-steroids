#pragma once

#include <cstdint>
#include <cstring>
#include <new>

#include "DitheringConfig.h"

struct BmpHeader;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t unquantize(uint8_t level);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);

enum class BmpRowOrder { BottomUp, TopDown };

// Populates a 1-bit BMP header in the provided memory.
void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

// 1-bit Atkinson dithering - better quality than noise dithering for thumbnails
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = nullptr;
    errorRow1 = nullptr;
    errorRow2 = nullptr;
    // Nothrow-safe: an OOM during cover regeneration must never abort the
    // device (with -fno-exceptions a raw new would throw std::bad_alloc -> abort).
    errorRow0 = new (std::nothrow) int16_t[width + 4]();
    errorRow1 = new (std::nothrow) int16_t[width + 4]();
    errorRow2 = new (std::nothrow) int16_t[width + 4]();
    if (!errorRow0 || !errorRow1 || !errorRow2) {
      delete[] errorRow0;
      delete[] errorRow1;
      delete[] errorRow2;
      errorRow0 = nullptr; errorRow1 = nullptr; errorRow2 = nullptr;
    }
  }

  // True only when all error buffers were allocated; callers must check this
  // before using the ditherer.
  bool valid() const { return errorRow0 != nullptr && errorRow1 != nullptr && errorRow2 != nullptr; }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Apply brightness/contrast/gamma adjustments
    gray = adjustPixel(gray);

    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 2 levels (1-bit): 0 = black, 1 = white
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width) : width(width) {
    errorRow0 = nullptr; errorRow1 = nullptr; errorRow2 = nullptr;
    errorRow0 = new (std::nothrow) int16_t[width + 4]();  // Current row
    errorRow1 = new (std::nothrow) int16_t[width + 4]();  // Next row
    errorRow2 = new (std::nothrow) int16_t[width + 4]();  // Row after next
    if (!errorRow0 || !errorRow1 || !errorRow2) {
      delete[] errorRow0;
      delete[] errorRow1;
      delete[] errorRow2;
      errorRow0 = nullptr; errorRow1 = nullptr; errorRow2 = nullptr;
    }
  }

  bool valid() const { return errorRow0 != nullptr && errorRow1 != nullptr && errorRow2 != nullptr; }

  ~AtkinsonDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }
  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int grayIn, int x) {
    // Pre-clamp accumulated value (original gray + propagated error). The error
    // is computed against this value so clamped overflow isn't silently lost.
    const int16_t accumulated = static_cast<int16_t>(grayIn) + errorRow0[x + 2];

    // Clamp only for quantization / the reconstructable panel value.
    int16_t gray = accumulated;
    if (gray < 0) gray = 0;
    if (gray > 255) gray = 255;

    // Quantize to the nearest 2-bit level and reconstruct the panel gray
    // (no float: unquantize is a pure integer table lookup).
    const uint8_t qIndex = quantizeSimple(static_cast<uint8_t>(gray));
    const int16_t reconstructed = static_cast<int16_t>(unquantize(qIndex));

    // Real error: pre-clamp accumulated value minus the reconstructed panel gray.
    const int16_t error = accumulated - reconstructed;

    // Atkinson: diffuse 6/8 of the error (1/8 each) to 6 neighbors.
    const int16_t diffused = error >> 3;
    const bool lastPixel = (x + 1 >= width);
    // Horizontal neighbors only exist on this row; drop right/R+1 on the last pixel.
    if (!lastPixel) {
      errorRow0[x + 3] += diffused;  // Right
      errorRow0[x + 4] += diffused;  // Right+1
    }
    errorRow1[x + 1] += diffused;  // Bottom-left
    errorRow1[x + 2] += diffused;  // Bottom
    errorRow1[x + 3] += diffused;  // Bottom-right
    errorRow2[x + 2] += diffused;  // Two rows down

    return qIndex;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width) : width(width), rowCount(0) {
    errorCurRow = nullptr; errorNextRow = nullptr;
    errorCurRow = new (std::nothrow) int16_t[width + 2]();  // +2 for boundary handling
    errorNextRow = new (std::nothrow) int16_t[width + 2]();
    if (!errorCurRow || !errorNextRow) {
      delete[] errorCurRow;
      delete[] errorNextRow;
      errorCurRow = nullptr; errorNextRow = nullptr;
    }
  }

  bool valid() const { return errorCurRow != nullptr && errorNextRow != nullptr; }

  ~FloydSteinbergDitherer() {
    delete[] errorCurRow;
    delete[] errorNextRow;
  }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  // Process a single pixel and return quantized 2-bit value
  // x is the logical x position (0 to width-1), direction handled internally
  uint8_t processPixel(int grayIn, int x) {
    // Pre-clamp accumulated value; error is computed from it so clamped
    // overflow isn't silently lost. All arithmetic in int16_t, no float.
    const int16_t accumulated = static_cast<int16_t>(grayIn) + errorCurRow[x + 1];

    // Clamp only for quantization / the reconstructable panel value.
    int16_t gray = accumulated;
    if (gray < 0) gray = 0;
    if (gray > 255) gray = 255;

    // Quantize and reconstruct (pure integer).
    const uint8_t qIndex = quantizeSimple(static_cast<uint8_t>(gray));
    const int16_t reconstructed = static_cast<int16_t>(unquantize(qIndex));

    // Real error: pre-clamp accumulated minus the reconstructed panel gray.
    const int16_t error = accumulated - reconstructed;
    const bool lastPixel = (x + 1 >= width);

    // Serpentine: direction-aware 7/16, 3/16, 5/16, 1/16 diffusion.
    if (!isReverseRow()) {
      // Left to right.
      if (!lastPixel) errorCurRow[x + 2] += (error * 7) >> 4;  // Right (drop on last pixel)
      errorNextRow[x] += (error * 3) >> 4;                     // Bottom-left
      errorNextRow[x + 1] += (error * 5) >> 4;                 // Bottom
      errorNextRow[x + 2] += (error) >> 4;                     // Bottom-right
    } else {
      // Right to left: the "left" horizontal neighbor is x-1, and there is no
      // left neighbor when x == 0 (first pixel) — drop its 7/16 contribution.
      if (x > 0) errorCurRow[x] += (error * 7) >> 4;           // Left
      errorNextRow[x + 2] += (error * 3) >> 4;                 // Bottom-right
      errorNextRow[x + 1] += (error * 5) >> 4;                 // Bottom
      errorNextRow[x] += (error) >> 4;                         // Bottom-left
    }

    return qIndex;
  }

  // Call at the end of each row to swap buffers
  void nextRow() {
    // Swap buffers
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    // Clear the next row buffer
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount++;
  }

  // Check if current row should be processed in reverse
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  // Reset for a new image or MCU block
  void reset() {
    memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int width;
  int rowCount;
  int16_t* errorCurRow;
  int16_t* errorNextRow;
};
