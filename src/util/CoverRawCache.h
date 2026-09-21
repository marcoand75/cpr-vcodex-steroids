#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

struct RawCoverHeader {
  uint32_t width;
  uint32_t height;
  uint32_t rowBytes;   // (width + 3) / 4, bytes per 2bpp-packed row
  uint8_t  topDown;    // 0 = bottom-up BMP, 1 = top-down BMP
  uint8_t  reserved[3];
};

namespace CoverRawCache {

// Directory where pre-decoded cover pixel data is stored.
constexpr const char* kDir = "/.crosspoint/cover-raw";

// Build the raw cache path for a given BMP path and target decode size.
// The path is size-specific: the same BMP used at different cover sizes
// produces different .raw files.
std::string getRawPath(const std::string& bmpPath, int width, int height);

// Return true if a valid .raw file exists for the given path.
bool hasValidRaw(const std::string& rawPath);

// Generate a .raw file from an existing BMP cover.
// The BMP is fully decoded through the same pipeline used at render time
// (parseHeaders + readNextRow), so the resulting pixel data matches what
// drawBitmap() would produce.
//
// Returns true on success, false on I/O error or OOM.
// Does NOT modify the source BMP.
bool generate(const std::string& bmpPath, const std::string& rawPath);

// Load an entire .raw file into a heap-allocated buffer.
// The caller owns the returned buffer and must free() it.
// outHeader is filled with the stored metadata.
// Returns nullptr on OOM, I/O error, or size mismatch.
uint8_t* load(const std::string& rawPath, RawCoverHeader* outHeader, size_t* outSize);

// Remove the .raw file corresponding to a BMP path at the given size.
// Safe to call even if the file does not exist.
void remove(const std::string& bmpPath, int width, int height);

}  // namespace CoverRawCache
