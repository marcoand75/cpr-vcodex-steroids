#include "CoverRawCache.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>
#include <cstring>

#include "BitmapHelpers.h"

namespace {

constexpr uint32_t FNV1A_OFFSET = 2166136261UL;
constexpr uint32_t FNV1A_PRIME = 16777619UL;

uint32_t fnv1aString(uint32_t hash, const std::string& value) {
  for (const char c : value) {
    hash = (hash ^ static_cast<uint8_t>(c)) * FNV1A_PRIME;
  }
  return (hash ^ 0xFF) * FNV1A_PRIME;
}

}  // namespace

std::string CoverRawCache::getRawPath(const std::string& bmpPath, int width, int height) {
  const uint32_t h = fnv1aString(FNV1A_OFFSET, bmpPath);
  char filename[96];
  std::snprintf(filename, sizeof(filename), "%08lx_%dx%d.raw",
                static_cast<unsigned long>(h), width, height);
  return std::string(kDir) + "/" + filename;
}

bool CoverRawCache::hasValidRaw(const std::string& rawPath) {
  if (!Storage.exists(rawPath.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("CRC", rawPath, file)) {
    return false;
  }

  const long fileSize = file.size();
  file.close();

  if (fileSize < static_cast<long>(sizeof(RawCoverHeader))) {
    return false;
  }

  return true;
}

bool CoverRawCache::generate(const std::string& bmpPath, const std::string& rawPath) {
  FsFile bmpFile;
  if (!Storage.openFileForRead("CRC", bmpPath, bmpFile)) {
    return false;
  }

  Bitmap bitmap(bmpFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    bmpFile.close();
    return false;
  }

  Storage.mkdir(kDir);

  FsFile rawFile;
  if (!Storage.openFileForWrite("CRC", rawPath, rawFile)) {
    bmpFile.close();
    return false;
  }

  RawCoverHeader header;
  header.width = static_cast<uint32_t>(bitmap.getWidth());
  header.height = static_cast<uint32_t>(bitmap.getHeight());
  header.topDown = bitmap.isTopDown() ? 1 : 0;
  std::memset(header.reserved, 0, sizeof(header.reserved));

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  header.rowBytes = static_cast<uint32_t>(outputRowSize);

  if (rawFile.write(&header, sizeof(header)) != sizeof(header)) {
    rawFile.close();
    Storage.remove(rawPath.c_str());
    bmpFile.close();
    return false;
  }

  if (outputRowSize <= 0) {
    rawFile.close();
    Storage.remove(rawPath.c_str());
    bmpFile.close();
    return false;
  }

  std::vector<uint8_t> rowBuf(outputRowSize + bitmap.getRowBytes());
  uint8_t* packedRow = rowBuf.data();
  uint8_t* rawRow = packedRow + outputRowSize;

  for (int bmpY = 0; bmpY < bitmap.getHeight(); ++bmpY) {
    if (bitmap.readNextRow(packedRow, rawRow) != BmpReaderError::Ok) {
      rawFile.close();
      Storage.remove(rawPath.c_str());
      bmpFile.close();
      return false;
    }
    if (rawFile.write(packedRow, outputRowSize) != static_cast<size_t>(outputRowSize)) {
      rawFile.close();
      Storage.remove(rawPath.c_str());
      bmpFile.close();
      return false;
    }
  }

  rawFile.close();
  bmpFile.close();
  return true;
}

uint8_t* CoverRawCache::load(const std::string& rawPath, RawCoverHeader* outHeader, size_t* outSize) {
  FsFile file;
  if (!Storage.openFileForRead("CRC", rawPath, file)) {
    return nullptr;
  }

  const long fileSize = file.size();
  if (fileSize < static_cast<long>(sizeof(RawCoverHeader))) {
    file.close();
    return nullptr;
  }

  RawCoverHeader header;
  if (file.read(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    return nullptr;
  }

  const uint32_t outputRowSize = (header.width + 3) / 4;
  if (header.width == 0 || header.height == 0 || outputRowSize == 0 ||
      header.rowBytes != outputRowSize) {
    file.close();
    return nullptr;
  }

  const size_t pixelDataSize = static_cast<size_t>(outputRowSize) * static_cast<size_t>(header.height);
  const size_t expectedSize = sizeof(header) + pixelDataSize;
  if (static_cast<size_t>(fileSize) != expectedSize) {
    file.close();
    return nullptr;
  }

  uint8_t* data = static_cast<uint8_t*>(malloc(pixelDataSize));
  if (!data) {
    file.close();
    return nullptr;
  }

  size_t totalRead = 0;
  while (totalRead < pixelDataSize) {
    const int r = file.read(data + totalRead, pixelDataSize - totalRead);
    if (r <= 0) {
      free(data);
      file.close();
      return nullptr;
    }
    totalRead += static_cast<size_t>(r);
  }

  file.close();

  if (outHeader) *outHeader = header;
  if (outSize) *outSize = pixelDataSize;
  return data;
}

void CoverRawCache::remove(const std::string& bmpPath, int width, int height) {
  const std::string rawPath = getRawPath(bmpPath, width, height);
  if (Storage.exists(rawPath.c_str())) {
    Storage.remove(rawPath.c_str());
  }
}
