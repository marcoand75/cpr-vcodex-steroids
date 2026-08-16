#pragma once
#include <HalStorage.h>
#include <InflateStream.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

enum class ZipStreamStatus {
  More,
  Done,
  Error,
};

struct ZipStreamInflateCtx {
  InflateStream reader;
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

class ZipFileStreamReader {
 public:
  ZipFileStreamReader() = default;
  ~ZipFileStreamReader();
  ZipFileStreamReader(const ZipFileStreamReader&) = delete;
  ZipFileStreamReader& operator=(const ZipFileStreamReader&) = delete;

  bool begin(const std::string& zipPath, const char* filename, size_t chunkSize);
  ZipStreamStatus pump(Print& out, size_t maxOutputBytes = 0);
  void abort();
  size_t produced() const { return totalProduced; }

 private:
  std::string zipPath;
  uint16_t method = 0;
  uint32_t dataOffset = 0;
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;
  size_t chunkSize = 0;
  size_t totalProduced = 0;
  size_t compressedConsumed = 0;
  uint8_t* readBuffer = nullptr;
  uint8_t* outputBuffer = nullptr;
  ZipStreamInflateCtx inflateCtx;
  bool active = false;
};

class ZipFile {
  friend class ZipFileStreamReader;

 public:
  struct FileStatSlim {
    uint16_t method;             // Compression method
    uint32_t compressedSize;     // Compressed size
    uint32_t uncompressedSize;   // Uncompressed size
    uint32_t localHeaderOffset;  // Offset of local file header
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint16_t totalEntries;
    bool isSet;
  };

  // Target for batch uncompressed size lookup (sorted by hash, then len)
  struct SizeTarget {
    uint64_t hash;   // FNV-1a 64-bit hash of normalized path
    uint16_t len;    // Length of path for collision reduction
    uint16_t index;  // Caller's index (e.g. spine index)
  };

  // Target and result for batch central-directory identity lookup. The full
  // path check prevents a hash collision from treating unrelated files as
  // byte-identical.
  struct EntryTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
    const char* path;
  };

  struct EntryIdentity {
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    bool found = false;
  };

  // FNV-1a 64-bit hash computed from char buffer (no std::string allocation)
  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  HalFile file;
  ZipDetails zipDetails = {0, 0, false};
  std::unordered_map<std::string, FileStatSlim> fileStatSlimCache;

  // Cursor for sequential central-dir scanning optimization
  uint32_t lastCentralDirPos = 0;
  bool lastCentralDirPosValid = false;

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  ~ZipFile() = default;
  // Zip file can be opened and closed by hand in order to allow for quick calculation of inflated file size
  // It is NOT recommended to pre-open it for any kind of inflation due to memory constraints
  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  bool getInflatedFileSize(const char* filename, size_t* size);
  // Batch lookup: scan ZIP central dir once and fill sizes for matching targets.
  // targets must be sorted by (hash, len). sizes[target.index] receives uncompressedSize.
  // Returns number of targets matched.
  int fillUncompressedSizes(const SizeTarget* targets, size_t targetCount, uint32_t* sizes, size_t sizeCount);
  // Batch lookup for duplicate detection. Targets must be sorted by (hash,
  // len); identities[target.index] receives the ZIP entry's CRC and sizes.
  int fillEntryIdentities(const EntryTarget* targets, size_t targetCount, EntryIdentity* identities,
                          size_t identityCount);
  // Due to the memory required to run each of these, it is recommended to not preopen the zip file for multiple
  // These functions will open and close the zip as needed
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize, bool allowEarlyStop = false);
  std::unique_ptr<ZipFileStreamReader> openFileStream(const char* filename, size_t chunkSize);

  template <typename F>
  bool enumerateFilePaths(F&& callback) {
    if (!fileStatSlimCache.empty()) {
      for (const auto& entry : fileStatSlimCache) {
        callback(std::string_view{entry.first});
      }
      return true;
    }

    const bool wasOpen = isOpen();
    if (!wasOpen && !open()) {
      return false;
    }

    if (!loadZipDetails()) {
      if (!wasOpen) {
        close();
      }
      return false;
    }

    file.seek(zipDetails.centralDirOffset);

    uint32_t sig;
    char itemName[256];

    while (file.available()) {
      file.read(&sig, 4);
      if (sig != 0x02014b50) {
        break;
      }

      file.seekCur(24);
      uint16_t nameLen, m, k;
      file.read(&nameLen, 2);
      file.read(&m, 2);
      file.read(&k, 2);
      file.seekCur(12);

      if (nameLen < sizeof(itemName)) {
        file.read(itemName, nameLen);
        itemName[nameLen] = '\0';
        callback(std::string_view{itemName, nameLen});
      } else {
        file.seekCur(nameLen);
      }

      file.seekCur(m + k);
    }

    if (!wasOpen) {
      close();
    }
    return true;
  }
};
