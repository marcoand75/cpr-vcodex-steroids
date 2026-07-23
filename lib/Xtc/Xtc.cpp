/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <ArenaManager.h>
#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser.reset(new xtc::XtcParser());

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  // Already generated
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    return false;
  }

  if (parser->getPageCount() == 0) {
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    return false;
  }
  if (pageInfo.width == 0 || pageInfo.height == 0) {
    return false;
  }

  const uint8_t bitDepth = parser->getBitDepth();

  // Create BMP file
  FsFile coverBmp;
  if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    return false;
  }

  // Write 1-bit BMP header (top-down row order)
  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, pageInfo.width, pageInfo.height, BmpRowOrder::TopDown);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(bmpHeader));

  const uint32_t bmpRowSize = ((pageInfo.width + 31) / 32) * 4;  // BMP row with 4-byte padding
  const size_t srcRowBytes = (pageInfo.width + 7) / 8;            // XTG row without padding

  if (bitDepth == 2) {
    // XTH 2-bit: must convert from column-major two-plane to row-major 1-bit BMP.
    // Use the global arena for the full-page temporary buffer to avoid heap
    // fragmentation. If the arena is unavailable or the allocation fails, fall
    // back to heap so cover generation still works.
    size_t bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
    ArenaManager::LockGuard arenaLock;
    uint8_t* pageBuffer = static_cast<uint8_t*>(ArenaManager::instance().allocate(bitmapSize, alignof(uint8_t)));
    bool pageBufferFromArena = pageBuffer != nullptr;
    if (!pageBufferFromArena) {
      pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
    }
    if (!pageBuffer) {
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      return false;
    }
    size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
    if (bytesRead == 0) {
      if (!pageBufferFromArena) free(pageBuffer);
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      return false;
    }

    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageInfo.height + 7) / 8;
    const size_t dstRowSize = (pageInfo.width + 7) / 8;

    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    bool rowBufferFromHeap = true;
    if (ArenaManager::instance().valid()) {
      rowBuffer = static_cast<uint8_t*>(ArenaManager::instance().allocate(dstRowSize, alignof(uint8_t)));
      rowBufferFromHeap = false;
    }
    if (!rowBuffer) {
      if (!pageBufferFromArena) free(pageBuffer);
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      return false;
    }

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);
      for (uint16_t x = 0; x < pageInfo.width; x++) {
        const size_t colIndex = pageInfo.width - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);
        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;
        if (pixelValue >= 1) {
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }
      coverBmp.write(rowBuffer, dstRowSize);
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t padSize = bmpRowSize - dstRowSize;
      if (padSize > 0) coverBmp.write(padding, padSize);
    }

    if (rowBufferFromHeap) {
      free(rowBuffer);
    }
    if (!pageBufferFromArena) {
      free(pageBuffer);
    }
  } else {
    // 1-bit XTG: data is row-major, compatible with 1-bit BMP row order.
    // Use streaming to avoid a 48 KB contiguous heap allocation.
    size_t totalWritten = 0;
    const size_t expectedTotal = static_cast<size_t>(pageInfo.height) * srcRowBytes;

    auto err = const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(
        0,
        [&](const uint8_t* chunk, size_t chunkSize, size_t /*offset*/) {
          // Process in source-row-sized units so BMP padding can be inserted.
          size_t consumed = 0;
          while (consumed < chunkSize) {
            size_t remaining = chunkSize - consumed;
            if (remaining >= srcRowBytes) {
              coverBmp.write(chunk + consumed, srcRowBytes);
              // Pad to 4-byte boundary
              size_t padSize = bmpRowSize - srcRowBytes;
              if (padSize > 0) {
                uint8_t pad[4] = {0, 0, 0, 0};
                coverBmp.write(pad, padSize);
              }
              consumed += srcRowBytes;
              totalWritten += srcRowBytes;
            } else {
              // Partial row at end of chunk — should not happen for row-major data
              // where chunks align to rows, but handle gracefully.
              coverBmp.write(chunk + consumed, remaining);
              size_t padSize = bmpRowSize - remaining;
              if (padSize > 0) {
                uint8_t pad[4] = {0, 0, 0, 0};
                coverBmp.write(pad, padSize);
              }
              totalWritten += remaining;
              consumed = chunkSize;
            }
          }
        },
        srcRowBytes * 128  // chunk size = 128 rows at a time = ~7.5 KB
    );

    if (err != xtc::XtcError::OK || totalWritten < expectedTotal) {
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      return false;
    }

    // Handle any remaining partial rows (shouldn't happen but be safe)
    size_t remainingRows = expectedTotal - totalWritten;
    while (remainingRows > 0) {
      size_t thisRow = (remainingRows >= srcRowBytes) ? srcRowBytes : remainingRows;
      uint8_t padFill[60] = {0xFF};  // white row
      coverBmp.write(padFill, thisRow);
      if (thisRow == srcRowBytes) {
        size_t padSize = bmpRowSize - srcRowBytes;
        if (padSize > 0) {
          uint8_t pad[4] = {0, 0, 0, 0};
          coverBmp.write(pad, padSize);
        }
      }
      remainingRows -= thisRow;
    }
  }

  coverBmp.close();

  const bool ok = Storage.exists(getCoverBmpPath().c_str());
  if (!ok) Storage.remove(getCoverBmpPath().c_str());
  LOG_DBG("XTC", "Generated cover BMP: %s %s", getCoverBmpPath().c_str(), ok ? "ok" : "FAILED");
  return ok;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }
std::string Xtc::getThumbBmpPath(int width, int height) const {
  return cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + ".bmp";
}

bool Xtc::generateThumbBmp(int height) const {
  return generateThumbBmpToPath(static_cast<int>(height * 0.6f), height, getThumbBmpPath(height));
}

bool Xtc::generateThumbBmp(int width, int height) const {
  return generateThumbBmpToPath(width, height, getThumbBmpPath(width, height));
}

bool Xtc::generateThumbBmpToPath(int width, int height, const std::string& thumbPath) const {
  if (width <= 0 || height <= 0) {
    return false;
  }

  // Validate existing file: reuse if correct dimensions, remove if stale/wrong.
  const bool thumbExists = Storage.exists(thumbPath.c_str());
  if (thumbExists) {
    FsFile chkFile;
    bool dimensionsMatch = false;
    if (Storage.openFileForRead("XTC", thumbPath, chkFile)) {
      Bitmap chk(chkFile);
      dimensionsMatch = (chk.parseHeaders() == BmpReaderError::Ok &&
                          chk.getWidth() == width && chk.getHeight() == height);
      chkFile.close();
    }
    if (dimensionsMatch) {
      return true;
    }
    Storage.remove(thumbPath.c_str());
  }

  // Ensure the full-size cover BMP exists first. This is cheaper than loading
  // the raw XTC page again and avoids a 48-94 KB contiguous heap allocation.
  if (!generateCoverBmp()) {
    return false;
  }

  // Read cover BMP dimensions (must exist after generateCoverBmp).
  FsFile coverFile;
  if (!Storage.openFileForRead("XTC", getCoverBmpPath(), coverFile)) {
    return false;
  }
  Bitmap coverBmpReader(coverFile);
  if (coverBmpReader.parseHeaders() != BmpReaderError::Ok) {
    coverFile.close();
    return false;
  }
  const int srcW = static_cast<int>(coverBmpReader.getWidth());
  const int srcH = static_cast<int>(coverBmpReader.getHeight());
  if (srcW == 0 || srcH == 0) {
    coverFile.close();
    return false;
  }

  const uint16_t thumbWidth  = static_cast<uint16_t>(width);
  const uint16_t thumbHeight = static_cast<uint16_t>(height);

  // Scale factor: fill the target slot (max of scaleX, scaleY = "cover" crop).
  const float scaleX = static_cast<float>(thumbWidth)  / srcW;
  const float scaleY = static_cast<float>(thumbHeight) / srcH;
  const float scale  = (scaleX > scaleY) ? scaleX : scaleY;

  // If source is smaller than or equal to target, just copy the cover BMP.
  if (scale >= 1.0f) {
    coverFile.close();
    FsFile src, dst;
    if (!Storage.openFileForRead("XTC", getCoverBmpPath(), src)) return false;
    if (!Storage.openFileForWrite("XTC", thumbPath, dst)) { src.close(); return false; }
    uint8_t buf[512];
    while (src.available()) {
      size_t n = src.read(buf, sizeof(buf));
      dst.write(buf, n);
    }
    dst.close();
    src.close();
    return Storage.exists(thumbPath.c_str());
  }

  // Fixed-point scale inverse (16.16).
  const uint32_t scaleInv_fp = static_cast<uint32_t>(65536.0f / scale);

  // Crop offsets so the visible region is centred.
  const uint64_t srcWidth_fp      = static_cast<uint64_t>(srcW) << 16;
  const uint64_t srcHeight_fp     = static_cast<uint64_t>(srcH) << 16;
  const uint64_t visibleWidth_fp  = static_cast<uint64_t>(thumbWidth)  * scaleInv_fp;
  const uint64_t visibleHeight_fp = static_cast<uint64_t>(thumbHeight) * scaleInv_fp;
  const uint32_t cropX_fp = static_cast<uint32_t>(srcWidth_fp  > visibleWidth_fp  ? (srcWidth_fp  - visibleWidth_fp)  / 2 : 0);
  const uint32_t cropY_fp = static_cast<uint32_t>(srcHeight_fp > visibleHeight_fp ? (srcHeight_fp - visibleHeight_fp) / 2 : 0);

  // Open output BMP.
  FsFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", thumbPath, thumbBmp)) {
    coverFile.close();
    return false;
  }

  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(BmpHeader));

  const size_t srcRowBytes = (srcW + 7) / 8;   // 60 for 480 px wide
  const size_t srcBmpRowSize = (srcW + 31) / 32 * 4;  // BMP row size with padding (60 for 480px)
  const uint32_t rowSize   = (thumbWidth + 31) / 32 * 4;

  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(rowSize));
  bool rowBufferFromHeap = true;
  if (ArenaManager::instance().valid()) {
    rowBuffer = static_cast<uint8_t*>(ArenaManager::instance().allocate(rowSize, alignof(uint8_t)));
    rowBufferFromHeap = false;
  }
  if (!rowBuffer) {
    thumbBmp.close();
    Storage.remove(thumbPath.c_str());
    coverFile.close();
    return false;
  }

  // After Bitmap::parseHeaders(), the file pointer is at the start of pixel data.
  const size_t bmpDataOffset = coverFile.position();

  // Buffer to hold all source rows needed for one destination row.
  // Worst case: scale factor 0.45 means ~2.2 src rows per dst row. Allocate for 4 rows.
  const size_t maxSrcRowsPerDstRow = 4;
  const size_t srcLinesBufSize = maxSrcRowsPerDstRow * srcRowBytes;
  uint8_t* srcLinesBuf = static_cast<uint8_t*>(malloc(srcLinesBufSize));
  bool srcLinesBufFromHeap = true;
  if (ArenaManager::instance().valid()) {
    srcLinesBuf = static_cast<uint8_t*>(ArenaManager::instance().allocate(srcLinesBufSize, alignof(uint8_t)));
    srcLinesBufFromHeap = false;
  }
  if (!srcLinesBuf) {
    if (rowBufferFromHeap) free(rowBuffer);
    thumbBmp.close();
    Storage.remove(thumbPath.c_str());
    coverFile.close();
    return false;
  }

  for (uint16_t dstY = 0; dstY < thumbHeight; dstY++) {
    memset(rowBuffer, 0xFF, rowSize);

    uint32_t srcYStart = (cropY_fp + static_cast<uint32_t>(dstY)     * scaleInv_fp) >> 16;
    uint32_t srcYEnd   = (cropY_fp + static_cast<uint32_t>(dstY + 1) * scaleInv_fp) >> 16;
    if (srcYStart >= static_cast<uint32_t>(srcH)) srcYStart = srcH - 1;
    if (srcYEnd   >  static_cast<uint32_t>(srcH)) srcYEnd   = srcH;
    if (srcYEnd   <= srcYStart)                   srcYEnd   = srcYStart + 1;
    if (srcYEnd   >  static_cast<uint32_t>(srcH)) srcYEnd   = srcH;

    const uint32_t numSrcRows = srcYEnd - srcYStart;
      if (numSrcRows > maxSrcRowsPerDstRow) {
        // Should not happen for 230x360 thumb from 480x800 source, but be safe.
        if (srcLinesBufFromHeap) free(srcLinesBuf);
        if (rowBufferFromHeap) free(rowBuffer);
        thumbBmp.close();
        Storage.remove(thumbPath.c_str());
        coverFile.close();
        return false;
      }

    // Pre-load all source rows for this destination row with one seek+read each.
    for (uint32_t r = 0; r < numSrcRows; r++) {
      const uint32_t srcY = srcYStart + r;
      const size_t rowFileOffset = bmpDataOffset + static_cast<size_t>(srcY) * srcBmpRowSize;
      coverFile.seek(rowFileOffset);
      coverFile.read(srcLinesBuf + r * srcRowBytes, srcRowBytes);
    }

    for (uint16_t dstX = 0; dstX < thumbWidth; dstX++) {
      uint32_t srcXStart = (cropX_fp + static_cast<uint32_t>(dstX)     * scaleInv_fp) >> 16;
      uint32_t srcXEnd   = (cropX_fp + static_cast<uint32_t>(dstX + 1) * scaleInv_fp) >> 16;
      if (srcXStart >= static_cast<uint32_t>(srcW)) srcXStart = srcW - 1;
      if (srcXEnd   >  static_cast<uint32_t>(srcW)) srcXEnd   = srcW;
      if (srcXEnd   <= srcXStart)                   srcXEnd   = srcXStart + 1;
      if (srcXEnd   >  static_cast<uint32_t>(srcW)) srcXEnd   = srcW;

      uint32_t graySum = 0, totalCount = 0;
      for (uint32_t r = 0; r < numSrcRows; r++) {
        const uint8_t* srcLine = srcLinesBuf + r * srcRowBytes;
        for (uint32_t srcX = srcXStart; srcX < srcXEnd && srcX < static_cast<uint32_t>(srcW); srcX++) {
          const size_t byteIdx = srcX / 8;
          const size_t bitIdx = 7 - (srcX % 8);
          const uint8_t grayValue = ((srcLine[byteIdx] >> bitIdx) & 1) ? 255 : 0;
          graySum += grayValue;
          totalCount++;
        }
      }

      const uint8_t avgGray = (totalCount > 0) ? static_cast<uint8_t>(graySum / totalCount) : 255;
      uint32_t h = static_cast<uint32_t>(dstX) * 374761393u + static_cast<uint32_t>(dstY) * 668265263u;
      h = (h ^ (h >> 13)) * 1274126177u;
      const int adjustedThreshold = 128 + ((static_cast<int>(h >> 24) - 128) / 2);
      if (avgGray < adjustedThreshold) {
        const size_t byteIndex = dstX / 8;
        if (byteIndex < rowSize)
          rowBuffer[byteIndex] &= ~(1u << (7 - (dstX % 8)));
      }
    }

    thumbBmp.write(rowBuffer, rowSize);
  }

  if (srcLinesBufFromHeap) {
    free(srcLinesBuf);
  }
  if (rowBufferFromHeap) {
    free(rowBuffer);
  }
  thumbBmp.close();
  coverFile.close();

  return Storage.exists(thumbPath.c_str());
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
