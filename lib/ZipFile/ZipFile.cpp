#include "ZipFile.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

struct ZipInflateCtx {
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
constexpr size_t ONE_SHOT_DEFLATE_MAX_COMPRESSED_BYTES = 32768;

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

size_t zipFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipInflateCtx*>(vctx);
  if (ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const size_t bytesRead = ctx->file->read(ctx->readBuf, toRead);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
}

size_t zipStreamFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipStreamInflateCtx*>(vctx);
  if (!ctx->file || ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const size_t bytesRead = ctx->file->read(ctx->readBuf, toRead);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
}
}  // namespace

ZipFileStreamReader::~ZipFileStreamReader() { abort(); }

bool ZipFileStreamReader::begin(const std::string& zipPathIn, const char* filename, const size_t chunkSizeIn) {
  abort();
  if (!filename || filename[0] == '\0' || chunkSizeIn == 0) {
    return false;
  }

  ZipFile zip(zipPathIn);
  const ScopedOpenClose zipOpen{zip};
  if (!zipOpen) return false;

  ZipFile::FileStatSlim fileStat = {};
  if (!zip.loadFileStatSlim(filename, &fileStat)) return false;

  const long offset = zip.getDataOffset(fileStat);
  if (offset < 0) return false;

  zipPath = zipPathIn;
  method = fileStat.method;
  dataOffset = static_cast<uint32_t>(offset);
  compressedSize = fileStat.compressedSize;
  uncompressedSize = fileStat.uncompressedSize;
  chunkSize = chunkSizeIn;
  totalProduced = 0;
  compressedConsumed = 0;

  readBuffer = static_cast<uint8_t*>(malloc(chunkSize));
  if (!readBuffer) {
    LOG_ERR("ZIP", "Failed to allocate cooperative read buffer (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap(), chunkSize);
    abort();
    return false;
  }
  outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
  if (!outputBuffer) {
    LOG_ERR("ZIP", "Failed to allocate cooperative output buffer (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap(), chunkSize);
    abort();
    return false;
  }

  if (method == ZIP_METHOD_DEFLATED) {
    inflateCtx.fileRemaining = compressedSize;
    inflateCtx.readBuf = readBuffer;
    inflateCtx.readBufSize = chunkSize;
    if (!inflateCtx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init cooperative inflate reader (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), chunkSize);
      abort();
      return false;
    }
    inflateCtx.reader.setFill(zipStreamFillCallback, &inflateCtx);
  } else if (method != ZIP_METHOD_STORED) {
    LOG_ERR("ZIP", "Unsupported compression method");
    abort();
    return false;
  }

  active = true;
  return true;
}

ZipStreamStatus ZipFileStreamReader::pump(Print& out, const size_t maxOutputBytes) {
  if (!active) return ZipStreamStatus::Error;

  HalFile zipFile;
  if (!Storage.openFileForRead("ZIP", zipPath, zipFile)) {
    return ZipStreamStatus::Error;
  }

  if (method == ZIP_METHOD_STORED) {
    if (!zipFile.seek(dataOffset + totalProduced)) {
      zipFile.close();
      return ZipStreamStatus::Error;
    }
    const size_t remaining = static_cast<size_t>(uncompressedSize) - totalProduced;
    const size_t budget = maxOutputBytes > 0 ? std::min(remaining, maxOutputBytes) : remaining;
    const size_t toRead = std::min(chunkSize, budget);
    if (toRead == 0) {
      zipFile.close();
      active = false;
      return ZipStreamStatus::Done;
    }
    const size_t dataRead = zipFile.read(outputBuffer, toRead);
    zipFile.close();
    if (dataRead == 0 || out.write(outputBuffer, dataRead) != dataRead) {
      return ZipStreamStatus::Error;
    }
    totalProduced += dataRead;
    if (totalProduced == static_cast<size_t>(uncompressedSize)) {
      active = false;
      return ZipStreamStatus::Done;
    }
    return ZipStreamStatus::More;
  }

  if (!zipFile.seek(dataOffset + compressedConsumed)) {
    zipFile.close();
    return ZipStreamStatus::Error;
  }

  inflateCtx.file = &zipFile;
  size_t pumped = 0;
  bool success = false;
  ZipStreamStatus result = ZipStreamStatus::More;

  while (true) {
    size_t produced = 0;
    const size_t beforeRemaining = inflateCtx.fileRemaining;
    const size_t outputLimit =
        maxOutputBytes > 0 ? std::min(chunkSize, maxOutputBytes - pumped) : static_cast<size_t>(chunkSize);
    if (outputLimit == 0) {
      success = true;
      break;
    }

    const InflateStream::Status status = inflateCtx.reader.readAtMost(outputBuffer, outputLimit, &produced);
    compressedConsumed += beforeRemaining - inflateCtx.fileRemaining;
    inflateCtx.file = nullptr;

    totalProduced += produced;
    pumped += produced;
    if (totalProduced > static_cast<size_t>(uncompressedSize)) {
      LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
              static_cast<size_t>(uncompressedSize));
      result = ZipStreamStatus::Error;
      break;
    }

    if (produced > 0 && out.write(outputBuffer, produced) != produced) {
      result = ZipStreamStatus::Error;
      break;
    }

    if (status == InflateStream::Status::Done) {
      if (totalProduced != static_cast<size_t>(uncompressedSize)) {
        LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(uncompressedSize),
                totalProduced);
        result = ZipStreamStatus::Error;
      } else {
        active = false;
        result = ZipStreamStatus::Done;
      }
      success = result != ZipStreamStatus::Error;
      break;
    }

    if (status == InflateStream::Status::Error) {
      LOG_ERR("ZIP", "Decompression failed");
      result = ZipStreamStatus::Error;
      break;
    }

    if (maxOutputBytes > 0 && pumped >= maxOutputBytes) {
      success = true;
      result = ZipStreamStatus::More;
      break;
    }

    inflateCtx.file = &zipFile;
  }

  inflateCtx.file = nullptr;
  zipFile.close();
  if (!success && result == ZipStreamStatus::Error) {
    return ZipStreamStatus::Error;
  }
  return result;
}

void ZipFileStreamReader::abort() {
  active = false;
  inflateCtx.reader.deinit();
  inflateCtx.file = nullptr;
  inflateCtx.fileRemaining = 0;
  inflateCtx.readBuf = nullptr;
  inflateCtx.readBufSize = 0;
  if (readBuffer) {
    free(readBuffer);
    readBuffer = nullptr;
  }
  if (outputBuffer) {
    free(outputBuffer);
    outputBuffer = nullptr;
  }
  zipPath.clear();
  method = 0;
  dataOffset = 0;
  compressedSize = 0;
  uncompressedSize = 0;
  chunkSize = 0;
  totalProduced = 0;
  compressedConsumed = 0;
}

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;  // End of list

    FileStatSlim fileStat = {};

    file.seekCur(6);
    file.read(&fileStat.method, 2);
    file.seekCur(8);
    file.read(&fileStat.compressedSize, 4);
    file.read(&fileStat.uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat.localHeaderOffset, 4);

    if (nameLen < sizeof(itemName)) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';
      fileStatSlimCache.emplace(itemName, fileStat);
    } else {
      // Skip over oversized entry names to avoid writing past fixed buffer.
      file.seekCur(nameLen);
    }

    // Skip the rest of this entry (extra field + comment)
    file.seekCur(m + k);
  }

  // Set cursor to start of central directory for sequential access
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  // Phase 1: Try scanning from cursor position first
  uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  bool wrapped = false;
  bool found = false;

  file.seek(startPos);

  uint32_t sig;
  char itemName[256];

  while (true) {
    uint32_t entryStart = file.position();

    if (file.read(&sig, 4) != 4 || sig != 0x02014b50) {
      // End of central directory
      if (!wrapped && lastCentralDirPosValid && startPos != zipDetails.centralDirOffset) {
        // Wrap around to beginning
        file.seek(zipDetails.centralDirOffset);
        wrapped = true;
        continue;
      }
      break;
    }

    // If we've wrapped and reached our start position, stop
    if (wrapped && entryStart >= startPos) {
      break;
    }

    file.seekCur(6);
    file.read(&fileStat->method, 2);
    file.seekCur(8);
    file.read(&fileStat->compressedSize, 4);
    file.read(&fileStat->uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat->localHeaderOffset, 4);

    if (nameLen < 256) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';

      if (strcmp(itemName, filename) == 0) {
        // Found it! Update cursor to next entry
        file.seekCur(m + k);
        lastCentralDirPos = file.position();
        lastCentralDirPosValid = true;
        found = true;
        break;
      }
    } else {
      // Name too long, skip it
      file.seekCur(nameLen);
    }

    // Skip extra field + comment
    file.seekCur(m + k);
  }

  return found;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* ZIP local file header signature */) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  file.seek(fileSize - scanRange);
  file.read(buffer, scanRange);

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    if (*reinterpret_cast<uint32_t*>(&buffer[i]) == signature) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    free(buffer);
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  zipDetails.totalEntries = *reinterpret_cast<uint16_t*>(&buffer[foundOffset + 10]);
  zipDetails.centralDirOffset = *reinterpret_cast<uint32_t*>(&buffer[foundOffset + 16]);
  zipDetails.isSet = true;

  free(buffer);
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(const SizeTarget* targets, const size_t targetCount, uint32_t* sizes,
                                   const size_t sizeCount) {
  if (targets == nullptr || sizes == nullptr || targetCount == 0) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  file.seek(zipDetails.centralDirOffset);

  int matched = 0;
  const auto expectedMatches = static_cast<int>(targetCount);
  const SizeTarget* const targetEnd = targets + targetCount;
  uint32_t sig;
  char itemName[256];

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;

    file.seekCur(6);
    uint16_t method;
    file.read(&method, 2);
    file.seekCur(8);
    uint32_t compressedSize, uncompressedSize;
    file.read(&compressedSize, 4);
    file.read(&uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    uint32_t localHeaderOffset;
    file.read(&localHeaderOffset, 4);

    if (nameLen < 256) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';

      uint64_t hash = fnvHash64(itemName, nameLen);
      SizeTarget key = {hash, nameLen, 0};

      auto it = std::lower_bound(targets, targetEnd, key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targetEnd && it->hash == hash && it->len == nameLen) {
        if (it->index < sizeCount) {
          sizes[it->index] = uncompressedSize;
          matched++;
        }
        ++it;
      }

      if (matched >= expectedMatches) {
        break;
      }
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(m + k);
  }

  return matched;
}

int ZipFile::fillEntryIdentities(const EntryTarget* targets, const size_t targetCount, EntryIdentity* identities,
                                 const size_t identityCount) {
  if (targets == nullptr || identities == nullptr || targetCount == 0) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  file.seek(zipDetails.centralDirOffset);

  int matched = 0;
  const auto expectedMatches = static_cast<int>(targetCount);
  const EntryTarget* const targetEnd = targets + targetCount;
  uint32_t sig;
  char itemName[256];

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;

    // Skip versions, flags, compression method, and modification time/date.
    file.seekCur(12);
    EntryIdentity identity;
    file.read(&identity.crc32, 4);
    file.read(&identity.compressedSize, 4);
    file.read(&identity.uncompressedSize, 4);
    uint16_t nameLen, extraLen, commentLen;
    file.read(&nameLen, 2);
    file.read(&extraLen, 2);
    file.read(&commentLen, 2);
    file.seekCur(12);

    if (nameLen < sizeof(itemName)) {
      file.read(itemName, nameLen);
      itemName[nameLen] = '\0';

      const uint64_t hash = fnvHash64(itemName, nameLen);
      const EntryTarget key = {hash, nameLen, 0, nullptr};
      auto it = std::lower_bound(targets, targetEnd, key, [](const EntryTarget& a, const EntryTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targetEnd && it->hash == hash && it->len == nameLen) {
        if (it->index < identityCount && it->path != nullptr && memcmp(it->path, itemName, nameLen) == 0 &&
            it->path[nameLen] == '\0') {
          identity.found = true;
          identities[it->index] = identity;
          ++matched;
        }
        ++it;
      }

      if (matched >= expectedMatches) {
        break;
      }
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(extraLen + commentLen);
  }

  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    bool inflated = false;
    if (deflatedDataSize <= ONE_SHOT_DEFLATE_MAX_COMPRESSED_BYTES) {
      auto* compressedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
      if (compressedData) {
        const size_t compressedRead = file.read(compressedData, deflatedDataSize);
        if (compressedRead != deflatedDataSize) {
          LOG_ERR("ZIP", "Failed to read compressed data");
          free(compressedData);
          free(data);
          return nullptr;
        }

        InflateStream inflate;
        if (!inflate.init(false)) {
          LOG_ERR("ZIP", "Failed to init one-shot inflate stream");
          free(compressedData);
          free(data);
          return nullptr;
        }
        inflate.setSource(compressedData, deflatedDataSize);
        if (!inflate.read(data, inflatedDataSize)) {
          LOG_ERR("ZIP", "Failed to inflate file");
          free(compressedData);
          free(data);
          return nullptr;
        }
        free(compressedData);
        inflated = true;
      } else {
        LOG_DBG("ZIP", "Falling back to streaming inflate; compressed buffer alloc failed (%zu bytes)",
                static_cast<size_t>(deflatedDataSize));
      }
    }

    if (!inflated) {
      file.seek(fileOffset);
      auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
      if (!fileReadBuffer) {
        LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
        free(data);
        return nullptr;
      }

      ZipInflateCtx ctx;
      ctx.file = &file;
      ctx.fileRemaining = deflatedDataSize;
      ctx.readBuf = fileReadBuffer;
      ctx.readBufSize = 1024;

      // One-shot mode: `data` holds the entire output, so back-references
      // resolve inside it and no 32KB window is allocated.
      InflateStream inflate;
      if (!inflate.init(false)) {
        LOG_ERR("ZIP", "Failed to init inflate stream");
        free(fileReadBuffer);
        free(data);
        return nullptr;
      }
      inflate.setFill(zipFillCallback, &ctx);

      if (!inflate.read(data, inflatedDataSize)) {
        LOG_ERR("ZIP", "Failed to inflate file");
        free(fileReadBuffer);
        free(data);
        return nullptr;
      }
      free(fileReadBuffer);
    }

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const size_t dataRead = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (dataRead == 0) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;

    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer (free=%u, maxAlloc=%u, chunk=%zu)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap(), chunkSize);
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), chunkSize);
      free(fileReadBuffer);
      return false;
    }

    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    InflateStream inflate;
    if (!inflate.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate stream (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), chunkSize);
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    inflate.setFill(zipFillCallback, &ctx);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStream::Status status = inflate.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          if (allowEarlyStop) {
            success = true;
          } else {
            LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          }
          break;
        }
      }

      if (status == InflateStream::Status::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        success = true;
        break;
      }

      if (status == InflateStream::Status::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
      // InflateStream::Status::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // inflate destructor frees the decompressor state + window
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}

std::unique_ptr<ZipFileStreamReader> ZipFile::openFileStream(const char* filename, const size_t chunkSize) {
  auto reader = makeUniqueNoThrow<ZipFileStreamReader>();
  if (!reader) {
    LOG_ERR("ZIP", "Failed to allocate cooperative stream reader");
    return nullptr;
  }
  if (!reader->begin(filePath, filename, chunkSize)) {
    return nullptr;
  }
  return reader;
}
