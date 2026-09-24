#include "LibraryIndex.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HiddenBooksStore.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <ZipFile.h>
#include <esp_task_wdt.h>
#include <util/CoverCachePaths.h>
#include <util/LibraryPerfLog.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CrossPointSettings.h"
#include "EpubParser.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "UserCollectionsStore.h"
#include "components/LibraryIndexCache.h"
#include "components/UITheme.h"

namespace LibraryIndex {

static void invalidateBookLookup();
static bool buildBookLookup();
static bool findRecordOffset(uint32_t bookId, uint32_t& offset);

void removeBookFromAllCollections(uint32_t bookId);

// =========================================================================
// Constants
// =========================================================================
namespace {
constexpr const char* kLibDir = "/.crosspoint/LIBRARY";
constexpr const char* kDatFile = "/.crosspoint/LIBRARY/library.dat";
constexpr const char* kScanFile = "/.crosspoint/LIBRARY/scan_state.dat";
constexpr const char* kIdxTitle = "/.crosspoint/LIBRARY/idx_title.bin";
constexpr const char* kIdxAuthor = "/.crosspoint/LIBRARY/idx_author.bin";
constexpr const char* kIdxCollections = "/.crosspoint/LIBRARY/idx_collections.bin";
constexpr const char* kIdxMetadataSeries = "/.crosspoint/LIBRARY/idx_metadata_series.bin";
constexpr const char* kIdxFolderCollections = "/.crosspoint/LIBRARY/idx_folder_collections.bin";
constexpr const char* kIdxUserCollections = "/.crosspoint/LIBRARY/idx_user_collections.bin";
constexpr const char* kIdxMixed = "/.crosspoint/LIBRARY/idx_mixed.bin";
constexpr const char* kSeriesDat = "/.crosspoint/LIBRARY/series.dat";
constexpr const char* kTmpDir = "/.crosspoint/LIBRARY/tmp";

int kProgressInterval = 10;

// ---- Fixed-length record sizes ----
constexpr size_t kRecordSize = sizeof(Record);  // 256
constexpr size_t kScanRecSize = 16;             // path_hash(4)+mtime(4)+size(4)+id(4)
constexpr size_t kIndexRecSize = 28;            // key(20)+id(4)+offset(4)

// ---- External merge-sort chunk size (records per chunk) ----
// 4 KB / 256 = 16 records.  Adjust via build flag if needed.
#ifndef LIBIDX_CHUNK_RECS
#define LIBIDX_CHUNK_RECS 16
#endif
constexpr int kChunkRecs = LIBIDX_CHUNK_RECS;  // records per temp chunk

// ---- Search: records per I/O block during full-text scan ----
#ifndef LIBIDX_SEARCH_BLOCK_RECS
#define LIBIDX_SEARCH_BLOCK_RECS 64
#endif
constexpr int kSearchBlockRecs = LIBIDX_SEARCH_BLOCK_RECS;

// ---- Index record (on-disk) ----
// (defined in LibraryIndex.h as LibraryIndex::IndexRec)

// ---- Series record (on-disk, one per book) ----
struct __attribute__((packed)) SeriesRec {
  uint32_t bookId;
  char seriesName[80];  // collection/series name
  float seriesIndex;    // position in series
  uint8_t flags;        // bit0 = folderFallback
  uint8_t reserved[3];  // padding to 92 bytes
};
static_assert(sizeof(SeriesRec) == 92, "SeriesRec must be 92 bytes");

// ---- Scan state record (on-disk) ----
struct __attribute__((packed)) ScanRec {
  uint32_t pathHash;
  uint32_t mtime;
  uint32_t fileSize;
  uint32_t bookId;
};
static_assert(sizeof(ScanRec) == 16, "ScanRec must be 16 bytes");

// =========================================================================
// Lightweight vector-read buffer for library.dat
// Keeps only a few 4KB blocks cached to avoid per-record SD seeks while
// staying well under the 32KB RAM budget.
// The buffer lives in static storage to avoid stack overflow on loopTask.
// =========================================================================
namespace {
constexpr size_t kDatBlockSize = 4096;                                              // must be multiple of record size
constexpr size_t kDatBlockRecs = kDatBlockSize / static_cast<size_t>(kRecordSize);  // 16
constexpr size_t kDatCacheBlocks = 4;  // 4 blocks = 16KB total; stays under 32KB RAM budget
static_assert(kDatBlockSize % kRecordSize == 0, "block size must align with record size");

struct DatCacheBlock {
  uint8_t data[kDatBlockSize];
  size_t blockIndex;
  bool valid;
};

struct LibraryDatVectorBuffer {
  DatCacheBlock blocks[kDatCacheBlocks];
  HalFile file;
  size_t m_fileSize;
  bool opened;
  size_t nextSlot;

  LibraryDatVectorBuffer() : m_fileSize(0), opened(false), nextSlot(0) {
    for (size_t i = 0; i < kDatCacheBlocks; ++i) blocks[i].valid = false;
  }

  bool open() {
    if (opened && file) return true;
    file = Storage.open(kDatFile);
    if (!file) return false;
    m_fileSize = static_cast<size_t>(file.size());
    opened = true;
    return true;
  }

  void close() {
    if (file) file.close();
    file = HalFile();
    opened = false;
    nextSlot = 0;
    for (size_t i = 0; i < kDatCacheBlocks; ++i) blocks[i].valid = false;
  }

  Record* getRecord(uint32_t offset) {
    if (!opened || !file) {
      if (!open()) return nullptr;
    }
    if (offset + kRecordSize > m_fileSize) return nullptr;

    const size_t blockIndex = static_cast<size_t>(offset) / kDatBlockSize;
    const size_t blockOffset = static_cast<size_t>(offset) % kDatBlockSize;

    for (size_t i = 0; i < kDatCacheBlocks; ++i) {
      if (blocks[i].valid && blocks[i].blockIndex == blockIndex) {
        return reinterpret_cast<Record*>(blocks[i].data + blockOffset);
      }
    }

    size_t slot = nextSlot;
    for (size_t i = 0; i < kDatCacheBlocks; ++i) {
      if (!blocks[i].valid) {
        slot = i;
        break;
      }
    }
    nextSlot = (nextSlot + 1) % kDatCacheBlocks;

    blocks[slot].blockIndex = blockIndex;
    blocks[slot].valid = true;

    const size_t blockStart = blockIndex * kDatBlockSize;
    size_t toRead = kDatBlockSize;
    if (blockStart + toRead > m_fileSize) toRead = m_fileSize - blockStart;

    if (!file.seek(blockStart)) {
      blocks[slot].valid = false;
      return nullptr;
    }
    const int read = file.read(blocks[slot].data, toRead);
    if (read != static_cast<int>(toRead)) {
      blocks[slot].valid = false;
      return nullptr;
    }

    return reinterpret_cast<Record*>(blocks[slot].data + blockOffset);
  }

  bool isOpen() const { return opened && file; }
  size_t fileSize() const { return m_fileSize; }
};

static LibraryDatVectorBuffer g_datBuffer;

}  // namespace

// =========================================================================
// Helpers
// =========================================================================

void emitProgress(GfxRenderer& r, const Rect& popup, int done, int total) {
  const int denom = total > 0 ? total : 1;
  int pct = (done * 100) / denom;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  UITheme::getInstance().getTheme().fillPopupProgress(r, popup, pct);
  r.displayBuffer();  // flush to e-ink for cold scan progress
}

void emitProgressIdle(GfxRenderer&, const Rect&, int, int) {
  // No-op for incremental scan — avoids displayBuffer calls.
}

// Normalise string for sort key: lowercase, strip accents/diacritics, truncate to 20.
void makeSortKey(const char* src, char* dst) {
  size_t w = 0;
  for (size_t i = 0; src[i] && w < 20; ++i) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    // Accent folding (ISO-8859-1 Latin-1 supplementary)
    switch (c) {
      case 0xC0:
      case 0xC1:
      case 0xC2:
      case 0xC3:
      case 0xC4:
      case 0xC5:
        dst[w++] = 'a';
        break;
      case 0xC8:
      case 0xC9:
      case 0xCA:
      case 0xCB:
        dst[w++] = 'e';
        break;
      case 0xCC:
      case 0xCD:
      case 0xCE:
      case 0xCF:
        dst[w++] = 'i';
        break;
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        dst[w++] = 'o';
        break;
      case 0xD9:
      case 0xDA:
      case 0xDB:
      case 0xDC:
        dst[w++] = 'u';
        break;
      case 0xE0:
      case 0xE1:
      case 0xE2:
      case 0xE3:
      case 0xE4:
      case 0xE5:
        dst[w++] = 'a';
        break;
      case 0xE8:
      case 0xE9:
      case 0xEA:
      case 0xEB:
        dst[w++] = 'e';
        break;
      case 0xEC:
      case 0xED:
      case 0xEE:
      case 0xEF:
        dst[w++] = 'i';
        break;
      case 0xF2:
      case 0xF3:
      case 0xF4:
      case 0xF5:
      case 0xF6:
        dst[w++] = 'o';
        break;
      case 0xF9:
      case 0xFA:
      case 0xFB:
      case 0xFC:
        dst[w++] = 'u';
        break;
      case 0xD1:
      case 0xF1:
        dst[w++] = 'n';
        break;
      case 0xC7:
      case 0xE7:
        dst[w++] = 'c';
        break;
      default:
        dst[w++] = static_cast<char>(std::tolower(c));
        break;
    }
  }
  while (w < 20) dst[w++] = '\0';
}

// Normalise string for title sort key with natural/alphanumeric ordering:
// lowercase, strip accents/diacritics, zero-pad digit runs to 4 digits so
// "Lightlark 2" sorts before "Lightlark 10".  Truncates to 20 bytes.
// Used only for the title index; author index keeps the plain makeSortKey().
void makeTitleSortKey(const char* src, char* dst) {
  size_t w = 0;
  for (size_t i = 0; src[i] && w < 20; ++i) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    if (c >= '0' && c <= '9') {
      size_t numStart = i;
      while (src[i] >= '0' && src[i] <= '9') ++i;
      const size_t numLen = i - numStart;
      // Zero-pad short numbers to 4 digits.
      if (numLen < 4) {
        const size_t pad = static_cast<size_t>(4 - numLen);
        const size_t space = (w + pad > 20) ? (20 - w) : pad;
        for (size_t p = 0; p < space && w < 20; ++p) dst[w++] = '0';
      }
      // Copy original digits.
      for (size_t d = 0; d < numLen && w < 20; ++d) {
        dst[w++] = src[numStart + d];
      }
      // Compensate for the outer for-loop increment: we already consumed
      // the digit run, so back off by one position.
      if (src[i]) --i;
    } else {
      // Accent folding (ISO-8859-1 Latin-1 supplementary)
      switch (c) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC4:
        case 0xC5:
          dst[w++] = 'a';
          break;
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCB:
          dst[w++] = 'e';
          break;
        case 0xCC:
        case 0xCD:
        case 0xCE:
        case 0xCF:
          dst[w++] = 'i';
          break;
        case 0xD2:
        case 0xD3:
        case 0xD4:
        case 0xD5:
        case 0xD6:
          dst[w++] = 'o';
          break;
        case 0xD9:
        case 0xDA:
        case 0xDB:
        case 0xDC:
          dst[w++] = 'u';
          break;
        case 0xE0:
        case 0xE1:
        case 0xE2:
        case 0xE3:
        case 0xE4:
        case 0xE5:
          dst[w++] = 'a';
          break;
        case 0xE8:
        case 0xE9:
        case 0xEA:
        case 0xEB:
          dst[w++] = 'e';
          break;
        case 0xEC:
        case 0xED:
        case 0xEE:
        case 0xEF:
          dst[w++] = 'i';
          break;
        case 0xF2:
        case 0xF3:
        case 0xF4:
        case 0xF5:
        case 0xF6:
          dst[w++] = 'o';
          break;
        case 0xF9:
        case 0xFA:
        case 0xFB:
        case 0xFC:
          dst[w++] = 'u';
          break;
        case 0xD1:
        case 0xF1:
          dst[w++] = 'n';
          break;
        case 0xC7:
        case 0xE7:
          dst[w++] = 'c';
          break;
        default:
          dst[w++] = static_cast<char>(std::tolower(c));
          break;
      }
    }
  }
  while (w < 20) dst[w++] = '\0';
}

// Compare two sort keys (memcmp-like)
int cmpSortKey(const char* a, const char* b) { return std::strncmp(a, b, 20); }

// Case-insensitive string comparison for sort keys (used in query sorting)
static int cmpSortKeyCI(const char* a, const char* b) {
  for (int i = 0; i < 20; ++i) {
    unsigned char ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
    unsigned char cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == 0) return 0;
  }
  return 0;
}

// Check if needle (lowercase, accent-folded) is a substring of haystack.
// Both must be null-terminated.  Accent-folds haystack on the fly.
bool substringMatch(const char* haystack, const char* needle) {
  if (!needle || !needle[0]) return true;
  if (!haystack) return false;
  const size_t nlen = std::strlen(needle);
  size_t hs = 0;
  char buf[256];
  size_t bw = 0;
  // Accent-fold haystack into buf
  for (size_t i = 0; haystack[i] && bw < sizeof(buf) - 1; ++i) {
    unsigned char c = static_cast<unsigned char>(haystack[i]);
    switch (c) {
      case 0xC0:
      case 0xC1:
      case 0xC2:
      case 0xC3:
      case 0xC4:
      case 0xC5:
        buf[bw++] = 'a';
        break;
      case 0xC8:
      case 0xC9:
      case 0xCA:
      case 0xCB:
        buf[bw++] = 'e';
        break;
      case 0xCC:
      case 0xCD:
      case 0xCE:
      case 0xCF:
        buf[bw++] = 'i';
        break;
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        buf[bw++] = 'o';
        break;
      case 0xD9:
      case 0xDA:
      case 0xDB:
      case 0xDC:
        buf[bw++] = 'u';
        break;
      case 0xE0:
      case 0xE1:
      case 0xE2:
      case 0xE3:
      case 0xE4:
      case 0xE5:
        buf[bw++] = 'a';
        break;
      case 0xE8:
      case 0xE9:
      case 0xEA:
      case 0xEB:
        buf[bw++] = 'e';
        break;
      case 0xEC:
      case 0xED:
      case 0xEE:
      case 0xEF:
        buf[bw++] = 'i';
        break;
      case 0xF2:
      case 0xF3:
      case 0xF4:
      case 0xF5:
      case 0xF6:
        buf[bw++] = 'o';
        break;
      case 0xF9:
      case 0xFA:
      case 0xFB:
      case 0xFC:
        buf[bw++] = 'u';
        break;
      case 0xD1:
      case 0xF1:
        buf[bw++] = 'n';
        break;
      case 0xC7:
      case 0xE7:
        buf[bw++] = 'c';
        break;
      default:
        buf[bw++] = static_cast<char>(std::tolower(c));
        break;
    }
  }
  buf[bw] = '\0';
  return std::strstr(buf, needle) != nullptr;
}

// =========================================================================
// Storage helpers (file I/O at fixed record granularity)
// =========================================================================

// Read one Record by its zero-based position in library.dat.
// Returns true on success.  RAM: 256 bytes stack.
bool readRecord(uint32_t pos, Record& rec) {
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const uint32_t offset = pos * kRecordSize;
  if (!f.seek(offset)) {
    f.close();
    return false;
  }
  const bool ok = (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize));
  f.close();
  return ok;
}

// Find a Record by its bookId in library.dat.
// Linear scan; acceptable because it is used only for the first book of a
// series while building mixed query results.
// Returns true on success.  RAM: 256 bytes stack.
bool readRecordByBookId(uint32_t bookId, Record& rec) {
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const int totalRecs = static_cast<int>(f.size() / kRecordSize);
  for (int rp = 0; rp < totalRecs; ++rp) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.id == bookId && !rec.tombstone()) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Find a Record by its absolute path in library.dat.
// Linear scan; acceptable for one-off lookups during delete/rename.
// Returns true on success.  RAM: 256 bytes stack.
static bool readRecordByPath(const char* path, Record& rec) {
  if (!path || !*path) return false;
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const int totalRecs = static_cast<int>(f.size() / kRecordSize);
  for (int rp = 0; rp < totalRecs; ++rp) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (!rec.tombstone() && strcmp(rec.path, path) == 0) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Append one Record to library.dat.  Returns the new record position (index).
// Returns UINT32_MAX on error.
uint32_t appendRecord(const Record& rec) {
  Storage.mkdir(kLibDir);
  // Open for write (O_WRONLY) so we can append. Use O_CREAT on first access.
  HalFile f = Storage.open(kDatFile, O_WRONLY);
  if (!f) {
    HalFile tmp = Storage.open(kDatFile, O_CREAT | O_WRONLY);
    if (tmp) tmp.close();
    f = Storage.open(kDatFile, O_WRONLY);
  }
  if (!f) return UINT32_MAX;
  const size_t currentSize = f.size();
  if (!f.seek(currentSize)) {
    f.close();
    return UINT32_MAX;
  }
  const uint32_t pos = static_cast<uint32_t>(currentSize / kRecordSize);
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) {
    f.close();
    return UINT32_MAX;
  }
  f.close();
  return pos;
}

// Append a series record to series.dat (used during scan)
static uint32_t appendSeriesRec(const SeriesRec& rec) {
  Storage.mkdir(kLibDir);
  HalFile f = Storage.open(kSeriesDat, O_WRONLY);
  if (!f) {
    HalFile tmp = Storage.open(kSeriesDat, O_CREAT | O_WRONLY);
    if (tmp) tmp.close();
    f = Storage.open(kSeriesDat, O_WRONLY);
  }
  if (!f) return UINT32_MAX;
  const size_t currentSize = f.size();
  if (!f.seek(currentSize)) {
    f.close();
    return UINT32_MAX;
  }
  const uint32_t pos = static_cast<uint32_t>(currentSize / sizeof(SeriesRec));
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) {
    f.close();
    return UINT32_MAX;
  }
  f.close();
  return pos;
}

// Writes one IndexRec at the given position in the file.
bool writeIndexRec(HalFile& f, const IndexRec& rec) {
  return f.write(reinterpret_cast<const uint8_t*>(&rec), kIndexRecSize) == static_cast<int>(kIndexRecSize);
}

// Reads one IndexRec from file (caller manages seek).
bool readIndexRec(HalFile& f, IndexRec& rec) {
  return f.read(reinterpret_cast<uint8_t*>(&rec), kIndexRecSize) == static_cast<int>(kIndexRecSize);
}

// =========================================================================
// Exists
// =========================================================================

// ---- Close anonymous namespace: all helpers above are internal, all
//      public API below is exported with external linkage ----
}  // anonymous namespace

bool exists() {
  HalFile f = Storage.open(kDatFile);
  if (!f) {
    LOG_DBG("LIBIDX", "exists: library.dat missing");
    return false;
  }
  const size_t sz = f.size();
  f.close();
  if (sz < kRecordSize) {
    LOG_DBG("LIBIDX", "exists: library.dat too small %u", (unsigned)sz);
    return false;
  }
  LOG_DBG("LIBIDX", "exists: library.dat size=%u records=%d", (unsigned)sz, (int)(sz / kRecordSize));
  return true;
}

// =========================================================================
// thumbPathFor (delegates to the shared upstream-compatible cache paths)
// =========================================================================

std::string thumbPathFor(const std::string& bookPath, int coverW, int coverH) {
  // Cover cache locations live in the steroids CoverCachePaths utility, which
  // mirrors the upstream Epub/Xtc/Txt constructor naming so library covers and
  // home-screen covers share the same files.
  return cover_cache_paths::thumbPathForBookPath(bookPath, coverW, coverH);
}

// =========================================================================
// Metadata extraction (adapted from LibraryCache)
// =========================================================================

bool extractMetadata(const char* path, char* title, size_t titleCap, char* author, size_t authorCap) {
  if (!path || path[0] != '/') return false;
  HalFile stat = Storage.open(path);
  if (!stat || stat.isDirectory() || stat.size() == 0) {
    if (stat) stat.close();
    return false;
  }
  stat.close();

  if (FsHelpers::hasEpubExtension(std::string_view{path})) {
    std::string t, a;
    EpubParser::extractMetadata(path, "/.crosspoint", t, a);
    std::strncpy(title, t.c_str(), titleCap - 1);
    title[titleCap - 1] = '\0';
    std::strncpy(author, a.c_str(), authorCap - 1);
    author[authorCap - 1] = '\0';
  } else if (FsHelpers::hasXtcExtension(std::string_view{path})) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      std::strncpy(title, xtc.getTitle().c_str(), titleCap - 1);
      title[titleCap - 1] = '\0';
      std::strncpy(author, xtc.getAuthor().c_str(), authorCap - 1);
      author[authorCap - 1] = '\0';
    }
  } else if (FsHelpers::hasTxtExtension(std::string_view{path}) ||
             FsHelpers::hasMarkdownExtension(std::string_view{path})) {
    Txt txt(path, "/.crosspoint");
    if (txt.load()) {
      std::strncpy(title, txt.getTitle().c_str(), titleCap - 1);
      title[titleCap - 1] = '\0';
      author[0] = '\0';
    }
  }

  if (title[0] == '\0') {
    const char* slash = std::strrchr(path, '/');
    const char* dot = std::strrchr(path, '.');
    const char* start = slash ? slash + 1 : path;
    const size_t len = (dot && dot > start) ? static_cast<size_t>(dot - start) : std::strlen(start);
    size_t cp = (len < titleCap - 1) ? len : titleCap - 1;
    std::memcpy(title, start, cp);
    title[cp] = '\0';
  }
  return true;
}

// =========================================================================
// Scan — full SD walk + metadata + library.dat + scan_state.dat
// =========================================================================
//
// RAM note (post-optimization): the previous implementation collected every
// candidate book path into a std::vector<std::string> before processing.
// That vector alone cost ~110-150 B/book (vector slot + heap string) and
// lived in RAM for the *entire* duration of the scan, on top of the
// prevScan/newScan ScanRec vectors (16 B/book each). For large libraries
// this was by far the dominant RAM cost of scan().
//
// The version below eliminates that vector entirely. The directory walk
// (enumerateBooks) now takes a callback and invokes it once per discovered
// book file, so no path list is ever materialized in RAM. To still show
// an accurate progress bar we do a cheap first pass that only *counts*
// matching files (no strings stored, negligible RAM), then a second pass
// that streams each path straight into the existing per-file processing
// logic. Net effect: the only vectors alive during scan are prevScan and
// newScan (16 B/book each = 32 B/book total), a ~4-5x RAM reduction for
// large libraries versus the previous approach.
// =========================================================================

namespace {

// Directory walker used for scan passes.
// `onFile` is invoked once per matching book path with the file size from
// the directory entry (no extra Storage.open() needed).
using FileVisitor = std::function<void(const char* path, size_t fileSize)>;

static void walkDirs(const char* rootDir, const FileVisitor& onFile, bool yieldBetweenDirs = true) {
  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);
  std::vector<uint8_t> depth;
  depth.push_back(0);
  constexpr int kMaxDepth = 8;
  int dirCount = 0;

  while (!worklist.empty()) {
    std::string folder = std::move(worklist.back());
    worklist.pop_back();
    uint8_t fd = depth.back();
    depth.pop_back();
    if (yieldBetweenDirs && (++dirCount & 0x7) == 0) {
      yield();
      esp_task_wdt_reset();
    }
    HalFile rootFile = Storage.open(folder.c_str());
    if (!rootFile || !rootFile.isDirectory()) {
      if (rootFile) rootFile.close();
      continue;
    }
    rootFile.rewindDirectory();
    char name[500];
    for (HalFile file = rootFile.openNextFile(); file; file = rootFile.openNextFile()) {
      file.getName(name, sizeof(name));
      bool isDir = file.isDirectory();
      size_t fsz = file.size();  // capture before close
      file.close();
      if (name[0] == '.') continue;
      std::string lower = name;
      for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower == "system volume information" || lower == "my clippings.txt" || lower == "my lookups.txt") continue;
      if (isDir && (lower == "crosspoint" || lower == "library" || lower.compare(0, 5, "sleep") == 0 ||
                    lower == "font" || lower == "fonts" || lower == "dictionaries" || lower == "exports"))
        continue;
      std::string child = folder;
      if (child.back() != '/') child.push_back('/');
      child.append(name);
      if (isDir) {
        if (fd + 1 >= kMaxDepth) continue;
        worklist.push_back(std::move(child));
        depth.push_back(static_cast<uint8_t>(fd + 1));
        continue;
      }
      const std::string_view fn{name};
      if (FsHelpers::hasEpubExtension(fn) || FsHelpers::hasXtcExtension(fn) || FsHelpers::hasTxtExtension(fn) ||
          FsHelpers::hasMarkdownExtension(fn)) {
        if (std::strcmp(name, "if_found.txt") != 0 && std::strcmp(name, "crash_report.txt") != 0) {
          onFile(child.c_str(), fsz);
        }
      }
    }
    rootFile.close();
  }
}

static uint32_t hashPath(const char* p) {
  uint32_t h = 5381;
  while (*p) {
    h = ((h << 5) + h) + static_cast<unsigned char>(*p);
    ++p;
  }
  return h;
}

}  // namespace

// Update the path of an existing record by bookId.
// Used by rename/move operations.  The record must already exist.
bool updateRecordPath(uint32_t bookId, const char* newPath) {
  invalidateBookLookup();
  if (bookId == 0 || !newPath || !*newPath) return false;
  HalFile f = Storage.open(kDatFile, O_WRONLY);
  if (!f) return false;
  const int totalRecs = static_cast<int>(f.size() / kRecordSize);
  for (int rp = 0; rp < totalRecs; ++rp) {
    Record rec;
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.id == bookId && !rec.tombstone()) {
      std::strncpy(rec.path, newPath, sizeof(rec.path) - 1);
      rec.path[sizeof(rec.path) - 1] = '\0';
      f.seek(static_cast<uint32_t>(rp) * kRecordSize);
      f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize);
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Remove a book from all user collections by path.
// Looks up the bookId internally; safe to call before the record is tombstoned.
void removeBookFromAllCollectionsByPath(const char* path) {
  if (!path || !*path) return;
  Record rec;
  HalFile f = Storage.open(kDatFile);
  if (!f) return;
  const int totalRecs = static_cast<int>(f.size() / kRecordSize);
  for (int rp = 0; rp < totalRecs; ++rp) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (!rec.tombstone() && strcmp(rec.path, path) == 0) {
      f.close();
      removeBookFromAllCollections(rec.id);
      return;
    }
  }
  f.close();
}

bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir, int* outAdded, int* outRemoved) {
  LOG_DBG("LIB", "Scan: start root=%s", rootDir ? rootDir : "/");
  invalidateBookLookup();
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kLibDir);
  Storage.mkdir(kTmpDir);

  // ---- Phase 1: load existing scan_state.dat into RAM map ----
  struct ScanEntry {
    uint32_t hash;
    uint32_t mtime;
    uint32_t size;
    uint32_t id;
  };
  std::vector<ScanEntry> prevScan;
  {
    HalFile sf = Storage.open(kScanFile);
    if (sf) {
      const size_t fsz = sf.size();
      prevScan.reserve(fsz / kScanRecSize + 1);
      ScanRec r;
      while (sf.read(reinterpret_cast<uint8_t*>(&r), kScanRecSize) == static_cast<int>(kScanRecSize)) {
        prevScan.push_back({r.pathHash, r.mtime, r.fileSize, r.bookId});
      }
      sf.close();
    }
  }
  LOG_DBG("LIB", "Scan: loaded %u previous scan entries", prevScan.size());

  // Build hash→index map for O(1) lookup
  // Sort prevScan by hash for O(log n) binary search — zero extra RAM,
  // avoids ~96 KB std::unordered_map overhead at 3000 books.
  std::sort(prevScan.begin(), prevScan.end(), [](const ScanEntry& a, const ScanEntry& b) { return a.hash < b.hash; });
  LOG_DBG("LIB", "Scan: loaded %u previous scan entries", prevScan.size());

  // ---- Phase 2: single streaming pass
  int total = 0;

  // Choose progress emitter: show popup only for cold scan (non-empty popup)
  // Incremental scan from LibraryActivity passes an empty Rect{}.
  const bool incremental = (popupRect.x == 0 && popupRect.y == 0);
  auto doEmit = incremental ? emitProgressIdle : emitProgress;

  if (!incremental) {
    walkDirs(rootDir, [&total](const char*, size_t) { ++total; }, false);
    LOG_DBG("LIB", "Scan: %d candidate files found", total);
    LOG_DBG("LIB", "Scan: starting walk with total=%d popupRect=%d,%d", total, popupRect.x, popupRect.y);
    emitProgress(renderer, popupRect, 0, total);
    // Dynamic progress interval: ~10 refreshes total regardless of library size
    if (total > 10) kProgressInterval = std::max(1, total / 10);
  }

  struct ScanState {
    int interval;
  } state = {kProgressInterval};

  std::vector<ScanRec> newScan;
  // For incremental scan (empty popupRect), total is 0 so reserve(0) is a no-op.
  // Use prevScan.size() as a hint to avoid incremental vector growth that
  // can fail with std::bad_alloc on a fragmented heap.
  newScan.reserve(total > 0 ? total : prevScan.size());

  // Open library.dat for append (create fresh if no existing scan)
  {
    HalFile datFile = Storage.open(kDatFile);
    if (!datFile || prevScan.empty()) {
      if (datFile) datFile.close();
      HalFile tmp = Storage.open(kDatFile, O_CREAT | O_WRONLY | O_TRUNC);
      if (tmp) tmp.close();
      datFile = Storage.open(kDatFile);
    }
    if (!datFile) {
      LOG_ERR("LIB", "Scan: cannot open library.dat");
      return false;
    }
    datFile.close();
  }

  uint32_t nextId = 1;
  // Find max id from existing records
  {
    HalFile datFile = Storage.open(kDatFile);
    if (datFile) {
      const size_t existing = datFile.size() / kRecordSize;
      datFile.close();
      Record last;
      if (existing > 0 && readRecord(static_cast<uint32_t>(existing - 1), last)) {
        nextId = last.id + 1;
      }
    }
  }

  int added = 0, skipped = 0, removed = 0, pi = 0;

  auto processFile = [&](const char* p, size_t fsz) {
    yield();
    esp_task_wdt_reset();
    if (pi % state.interval == 0) doEmit(renderer, popupRect, pi, total);
    ++pi;

    // File size from directory entry — no extra Storage.open() needed
    if (fsz == 0) {
      ++skipped;
      return;
    }
    // mtime: use file size as proxy (ESP32 VFS doesn't expose mtime reliably via Arduino)
    const uint32_t mtime = (uint32_t)fsz;

    const uint32_t ph = hashPath(p);
    // Binary search in sorted prevScan for O(log n) lookup (zero extra RAM)
    const ScanEntry* prev = nullptr;
    {
      auto lo = prevScan.begin();
      auto hi = prevScan.end();
      ScanEntry key{ph, 0, 0, 0};
      auto it = std::lower_bound(lo, hi, key, [](const ScanEntry& a, const ScanEntry& b) { return a.hash < b.hash; });
      if (it != hi && it->hash == ph) prev = &(*it);
    }

    if (prev && prev->mtime == mtime && prev->size == (uint32_t)fsz) {
      // Unchanged — keep existing record
      newScan.push_back({ph, mtime, (uint32_t)fsz, prev->id});
      ++skipped;
      return;
    }

    // New or changed — extract metadata and append
    Record rec = {};
    rec.id = prev ? prev->id : nextId++;
    rec.file_size = (uint32_t)fsz;
    rec.mtime = mtime;
    std::strncpy(rec.path, p, sizeof(rec.path) - 1);
    rec.path[sizeof(rec.path) - 1] = '\0';

    char title[65] = {}, author[49] = {}, series[81] = {};
    float seriesIndex = 0.0f;
    bool seriesFromFolder = false;
    // For EPUBs, extract series/collection info via the fast ZIP parser.
    // Other formats (XTC, TXT) don't have series metadata.
    if (FsHelpers::hasEpubExtension(std::string_view{p})) {
      std::string epTitle, epAuthor, epSeries;
      float epSeriesIdx = 0.0f;
      EpubParser::extractMetadata(p, "/.crosspoint", epTitle, epAuthor, &epSeries, &epSeriesIdx);
      std::strncpy(title, epTitle.c_str(), sizeof(title) - 1);
      title[sizeof(title) - 1] = '\0';
      std::strncpy(author, epAuthor.c_str(), sizeof(author) - 1);
      author[sizeof(author) - 1] = '\0';
      std::strncpy(series, epSeries.c_str(), sizeof(series) - 1);
      series[sizeof(series) - 1] = '\0';
      seriesIndex = epSeriesIdx;
    } else {
      extractMetadata(p, title, sizeof(title), author, sizeof(author));
    }

    // Folder fallback: if no series metadata, use parent folder name (if not root)
    if (series[0] == '\0' && SETTINGS.libraryFolderCollections) {
      // Extract parent folder name from the path.
      // For "/books/Mystery/Book1.epub" this yields "Mystery".
      const char* lastSlash = strrchr(p, '/');
      if (lastSlash && lastSlash > p) {
        const char* prevSlash = lastSlash;
        while (prevSlash > p && *(prevSlash - 1) != '/') --prevSlash;
        if (prevSlash > p && prevSlash < lastSlash) {
          const size_t folderLen = static_cast<size_t>(lastSlash - prevSlash);
          if (folderLen < sizeof(series)) {
            memcpy(series, prevSlash, folderLen);
            series[folderLen] = '\0';
            seriesFromFolder = true;
          }
        }
      }
    }
    std::strncpy(rec.title, title, sizeof(rec.title) - 1);
    rec.title[sizeof(rec.title) - 1] = '\0';
    std::strncpy(rec.author, author, sizeof(rec.author) - 1);
    rec.author[sizeof(rec.author) - 1] = '\0';

    // Restore flags from previous record
    if (prev) {
      Record old;
      if (readRecord(prev->id > 0 ? (prev->id - 1) : 0, old) && old.id == prev->id) {
        rec.flags = old.flags;
        rec.setTombstone(false);
      }
    }

    uint32_t pos = appendRecord(rec);
    if (pos == UINT32_MAX) {
      LOG_ERR("LIB", "Scan: append failed for %s", p);
      return;
    }
    newScan.push_back({ph, mtime, (uint32_t)fsz, rec.id});

    // Write series entry if book has series/collection metadata.
    // Metadata-derived series are gated by libraryMetadataSeries; folder-fallback
    // series are gated by libraryFolderCollections.
    if (series[0] != '\0') {
      if (!seriesFromFolder && !SETTINGS.libraryMetadataSeries) {
        // metadata series disabled: skip
      } else if (seriesFromFolder && !SETTINGS.libraryFolderCollections) {
        // folder collections disabled: skip
      } else {
        SeriesRec sr = {};
        sr.bookId = rec.id;
        std::strncpy(sr.seriesName, series, sizeof(sr.seriesName) - 1);
        sr.seriesName[sizeof(sr.seriesName) - 1] = '\0';
        sr.seriesIndex = seriesIndex;
        sr.flags = seriesFromFolder ? 1 : 0;
        appendSeriesRec(sr);
      }
    }

    ++added;
  };

  walkDirs(rootDir, processFile, !incremental);  // only yield when showing progress

  // ---- Phase 4: mark removed files (present in old scan but not new) ----
  for (auto& old : prevScan) {
    bool found = false;
    for (auto& ns : newScan) {
      if (ns.bookId == old.id) {
        found = true;
        break;
      }
    }
    if (!found) {
      Record rec;
      for (uint32_t rp = 0;; ++rp) {
        if (!readRecord(rp, rec)) break;
        if (rec.id == old.id && !rec.tombstone()) {
          rec.setTombstone(true);
          HalFile f = Storage.open(kDatFile, O_WRONLY);
          if (f) {
            f.seek(rp * kRecordSize);
            f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize);
            f.close();
          }
          ++removed;
          // Clean up user-collection memberships for the removed book.
          USER_COLLECTIONS.ensureLoaded();
          removeBookFromAllCollections(old.id);
          break;
        }
      }
    }
  }

  // ---- Phase 5: write new scan_state.dat ----
  HalFile sf = Storage.open(kScanFile, O_CREAT | O_WRONLY | O_TRUNC);
  if (sf) {
    for (auto& ns : newScan) {
      ScanRec sr = {ns.pathHash, ns.mtime, ns.fileSize, ns.bookId};
      sf.write(reinterpret_cast<const uint8_t*>(&sr), kScanRecSize);
    }
    sf.close();
  }

  emitProgress(renderer, popupRect, total, total);
  if (outAdded) *outAdded = added;
  if (outRemoved) *outRemoved = removed;
  LOG_DBG("LIB", "Scan: final added=%d skipped=%d removed=%d newScan=%u", added, skipped, removed, newScan.size());
  LOG_DBG("LIB", "Scan: done totalBooks=%d", added + skipped);
  LOG_DBG("LIB", "Scan: final datFile records check...");
  {
    HalFile datCheck = Storage.open(kDatFile);
    if (datCheck) {
      const int datCount = static_cast<int>(datCheck.size() / kRecordSize);
      datCheck.close();
      LOG_DBG("LIB", "Scan: library.dat records=%d", datCount);
    } else {
      LOG_ERR("LIB", "Scan: library.dat missing after scan!");
    }
  }
  return true;
}

// =========================================================================
// Build indices
// =========================================================================

// =========================================================================
// Build indices — external merge-sort
// RAM: 4 KB chunk buffer + index file buffer
// =========================================================================

// Compare two records by sort key for qsort
static int cmpByTitle(const void* a, const void* b) {
  auto* ra = static_cast<const IndexRec*>(a);
  auto* rb = static_cast<const IndexRec*>(b);
  return cmpSortKey(ra->sortKey, rb->sortKey);
}
static int cmpByAuthor(const void* a, const void* b) {
  auto* ra = static_cast<const IndexRec*>(a);
  auto* rb = static_cast<const IndexRec*>(b);
  int c = cmpSortKey(ra->sortKey, rb->sortKey);
  if (c != 0) return c;
  // Secondary sort by title within same author
  // (No title key stored; we'd need the record for that, but for index purposes
  //  author sort key alone is sufficient; tie-breaking is done at query time.)
  return 0;
}

// =========================================================================
// Merge-sort helpers (must be at namespace scope, not inside function)
// =========================================================================

struct ChunkReader {
  HalFile file;
  IndexRec cur;
  bool eof = false;
  bool open(const char* path) {
    file = Storage.open(path);
    if (!file) return false;
    eof = !readIndexRec(file, cur);
    return true;
  }
  bool advance() {
    if (eof) return false;
    eof = !readIndexRec(file, cur);
    return !eof;
  }
  void close() {
    if (file) file.close();
  }
};

static bool buildIndexFile(const char* outPath, int (*cmp)(const void*, const void*), bool useAuthorKey) {
  LOG_DBG("LIB", "IdxBuild: start %s", outPath);
  // Phase 1: read library.dat in chunks, sort each chunk, write chunk_*.tmp
  HalFile dat = Storage.open(kDatFile);
  if (!dat) {
    LOG_ERR("LIB", "IdxBuild: cannot open library.dat");
    return false;
  }
  const int totalRecs = static_cast<int>(dat.size() / kRecordSize);
  if (totalRecs == 0) {
    dat.close();
    LOG_ERR("LIB", "IdxBuild: library.dat empty");
    return false;
  }
  LOG_DBG("LIB", "IdxBuild: totalRecs=%d", totalRecs);
  dat.close();

  int chunkCount = 0;
  {
    HalFile df = Storage.open(kDatFile);
    Record rec;
    std::vector<IndexRec> chunk;
    chunk.reserve(kChunkRecs);
    for (int rp = 0; rp < totalRecs; ++rp) {
      if (!readRecord(static_cast<uint32_t>(rp), rec)) continue;
      if (rec.tombstone()) continue;
      IndexRec ir;
      if (useAuthorKey) {
        char key[65];
        std::strncpy(key, rec.author, 64);
        key[64] = '\0';
        if (key[0] == '\0') {
          key[0] = 'z';
          key[1] = 'z';
          key[2] = 'z';
          key[3] = '\0';
        }
        makeSortKey(key, ir.sortKey);
      } else {
        makeTitleSortKey(rec.title, ir.sortKey);
      }
      ir.bookId = rec.id;
      ir.recordOffset = static_cast<uint32_t>(rp * kRecordSize);
      chunk.push_back(ir);
      if (static_cast<int>(chunk.size()) >= kChunkRecs || rp == totalRecs - 1) {
        std::qsort(chunk.data(), chunk.size(), sizeof(IndexRec), cmp);
        char tmpPath[96];
        std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, chunkCount++);
        HalFile tf = Storage.open(tmpPath, O_CREAT | O_WRONLY | O_TRUNC);
        if (tf) {
          tf.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size() * kIndexRecSize);
          tf.close();
        }
        chunk.clear();
      }
    }
    df.close();
  }
  LOG_DBG("LIB", "IdxBuild: %d chunks written for %s", chunkCount, outPath);

  if (chunkCount == 0) return false;

  // Phase 2: k-way merge
  HalFile outF = Storage.open(outPath, O_CREAT | O_WRONLY | O_TRUNC);
  if (!outF) return false;

  std::vector<ChunkReader> readers(chunkCount);

   LOG_DBG("LIB", "IdxBuild: merge start chunkCount=%d out=%s", chunkCount, outPath);
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!readers[i].open(tmpPath)) {
      LOG_ERR("LIB", "IdxBuild: cannot open chunk %s", tmpPath);
    }
  }

  while (true) {
    // Find smallest
    int best = -1;
    for (int i = 0; i < chunkCount; ++i) {
      if (readers[i].eof) continue;
      if (best < 0 || cmp(&readers[i].cur, &readers[best].cur) < 0) best = i;
    }
    if (best < 0) break;
    writeIndexRec(outF, readers[best].cur);
    readers[best].advance();
  }

  outF.close();
  for (int i = 0; i < chunkCount; ++i) readers[i].close();

  // Phase 3: delete temp chunks
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    Storage.remove(tmpPath);
  }

  LOG_DBG("LIB", "IdxBuild: merge complete for %s", outPath);
  LOG_DBG("LIB", "IdxBuild: done %s", outPath);
  return true;
}

bool buildIndices() {
  LOG_DBG("LIB", "BuildIndices: start");
  invalidateBookLookup();
  const unsigned long t0 = millis();

  if (!buildIndexFile(kIdxTitle, cmpByTitle, false)) {
    LOG_ERR("LIB", "BuildIndices: title index failed");
    return false;
  }
  LOG_DBG("LIB", "BuildIndices: title index OK");
  if (!buildIndexFile(kIdxAuthor, cmpByAuthor, true)) {
    LOG_ERR("LIB", "BuildIndices: author index failed");
    return false;
  }
  LOG_DBG("LIB", "BuildIndices: author index OK");
  if (!buildMixedIndex(SortMode::TITLE_ASC)) {
    LOG_ERR("LIB", "BuildIndices: mixed index failed");
    return false;
  }
  LOG_DBG("LIB", "BuildIndices: mixed index OK");

  LOG_DBG("LIB", "BuildIndices: done in %lu ms", millis() - t0);
  return true;
}

// ---- Collections index builder ----
// Builds idx_collections.bin from series.dat — one entry per unique collection,
// sorted alphabetically.  The index stores: collection name (key), first record
// offset in series.dat (where books of this collection start).
// The series.dat is sorted by collection name during the Build process.

static void recordToBookRef(const Record& rec, BookRef& ref);  // fwd decl

bool buildCollectionsIndex() {
  LOG_DBG("LIB", "BuildCollIdx: start");
  invalidateBookLookup();
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) {
    LOG_ERR("LIB", "BuildCollIdx: cannot open %s", kSeriesDat);
    return false;
  }

  // Handle old 88-byte format: if size is not divisible by 92, delete and rebuild
  const size_t seriesFileSize = sf.size();
  sf.close();
  if (seriesFileSize > 0 && seriesFileSize % sizeof(SeriesRec) != 0) {
    LOG_DBG("LIB", "BuildCollIdx: old series.dat format detected, removing for rebuild");
    Storage.remove(kSeriesDat);
    return false;
  }

  const int totalSeries = static_cast<int>(seriesFileSize / sizeof(SeriesRec));
  LOG_DBG("LIB", "BuildCollIdx: totalSeries=%d", totalSeries);

  // Build bookId -> path map from library.dat for folder-fallback scoping
  std::unordered_map<uint32_t, std::string> bookIdToPath;
  {
    HalFile datF = Storage.open(kDatFile);
    if (datF) {
      Record rec;
      int datRecords = 0;
      while (datF.read(reinterpret_cast<uint8_t*>(&rec), sizeof(Record)) == static_cast<int>(sizeof(Record))) {
        if (!rec.tombstone()) {
          bookIdToPath[rec.id] = rec.path;
        }
        ++datRecords;
      }
      datF.close();
      LOG_DBG("LIB", "BuildCollIdx: bookIdToPath built datRecords=%d mapSize=%d", datRecords, (int)bookIdToPath.size());
    } else {
      LOG_ERR("LIB", "BuildCollIdx: cannot open library.dat for bookId map");
    }
  }

  // Read all series records, validate against library.dat (skip tombstoned books)
  std::vector<SeriesRec> series;
  series.reserve(totalSeries);
  {
    HalFile f = Storage.open(kSeriesDat);
    if (!f) {
      LOG_ERR("LIB", "BuildCollIdx: cannot reopen %s", kSeriesDat);
      return false;
    }
    SeriesRec sr;
    int rawSeries = 0;
    while (f.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == static_cast<int>(sizeof(SeriesRec))) {
      ++rawSeries;
      // Skip tombstoned books
      if (sr.bookId == 0) continue;
      // Verify the book still exists and isn't tombstoned in library.dat
      auto pathIt = bookIdToPath.find(sr.bookId);
      if (pathIt == bookIdToPath.end()) continue;  // book deleted
      series.push_back(sr);
    }
    f.close();
    LOG_DBG("LIB", "BuildCollIdx: series read raw=%d valid=%d", rawSeries, (int)series.size());
  }

  // Sort by normalized series name + series index
  std::sort(series.begin(), series.end(), [](const SeriesRec& a, const SeriesRec& b) {
    int c = cmpSortKey(a.seriesName, b.seriesName);
    if (c != 0) return c < 0;
    return a.seriesIndex < b.seriesIndex;
  });
  LOG_DBG("LIB", "BuildCollIdx: series sorted count=%d", (int)series.size());

  // Write sorted series.dat
  {
    HalFile f = Storage.open(kSeriesDat, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f) {
      LOG_ERR("LIB", "BuildCollIdx: cannot open %s for write", kSeriesDat);
      return false;
    }
    for (const auto& sr : series) {
      f.write(reinterpret_cast<const uint8_t*>(&sr), sizeof(SeriesRec));
    }
    f.close();
  }
  LOG_DBG("LIB", "BuildCollIdx: sorted series.dat written");

  // Build collections index: group metadata-derived series globally by normalized key,
  // group folder-fallback series by exact parent path match.
  std::vector<CollectionIndexRec> collections;
  {
    size_t i = 0;
    while (i < series.size()) {
      // Determine grouping scope based on flags
      const bool isFolderFallback = (series[i].flags & 1) != 0;
      if (isFolderFallback && !SETTINGS.libraryFolderCollections) {
        ++i;
        continue;
      }
      if (!isFolderFallback && !SETTINGS.libraryMetadataSeries) {
        ++i;
        continue;
      }
      const char* groupKey = series[i].seriesName;
      std::string folderPath;
      if (isFolderFallback) {
        auto pathIt = bookIdToPath.find(series[i].bookId);
        if (pathIt != bookIdToPath.end()) {
          // Simple parent path extraction
          const char* p = pathIt->second.c_str();
          const char* lastSlash = strrchr(p, '/');
          if (lastSlash && lastSlash > p) {
            folderPath.assign(p, static_cast<size_t>(lastSlash - p));
          }
          groupKey = folderPath.c_str();
        }
      }

      // Find all entries in this group
      CollectionIndexRec ci;
      std::strncpy(ci.collectionName, series[i].seriesName, sizeof(ci.collectionName) - 1);
      ci.collectionName[sizeof(ci.collectionName) - 1] = '\0';
      ci.firstSeriesOffset = static_cast<uint32_t>(i * sizeof(SeriesRec));
      ci.bookCount = 0;
      ci.flags = 0;  // auto series

      size_t j = i;
      while (j < series.size()) {
        const bool jIsFolder = (series[j].flags & 1) != 0;
        if (isFolderFallback != jIsFolder) break;
        if (isFolderFallback) {
          auto pathIt = bookIdToPath.find(series[j].bookId);
          if (pathIt == bookIdToPath.end()) break;
          const char* p = pathIt->second.c_str();
          const char* lastSlash = strrchr(p, '/');
          std::string jParentPath;
          if (lastSlash && lastSlash > p) {
            jParentPath.assign(p, static_cast<size_t>(lastSlash - p));
          }
          if (folderPath != jParentPath) break;
        } else {
          // Metadata-derived: group by normalized name
          if (cmpSortKey(series[j].seriesName, series[i].seriesName) != 0) break;
        }
        ++ci.bookCount;
        ++j;
      }
      collections.push_back(ci);
      i = j;
    }
  }
  LOG_DBG("LIB", "BuildCollIdx: collections built count=%d", (int)collections.size());

  // Load user collections and merge
  std::vector<CollectionIndexRec> userCollections;
  {
    // Try to load user_collections.json
    const std::string jsonPath = "/.crosspoint/user_collections.json";
    if (Storage.exists(jsonPath.c_str())) {
      const String json = Storage.readFile(jsonPath.c_str());
      if (!json.isEmpty()) {
        // Parse JSON manually (avoid heavy JSON library)
        // Expected format: {"version":1,"collections":[{"id":"c_001","name":"...","createdAt":...}],...}
        const char* p = json.c_str();
        const char* collectionsStart = strstr(p, "\"collections\"");
        if (collectionsStart) {
          const char* arrStart = strchr(collectionsStart, '[');
          if (arrStart) {
            const char* scan = arrStart + 1;
            while (*scan && *scan != ']') {
              // Find "id":"..."
              const char* idKey = strstr(scan, "\"id\"");
              if (!idKey) break;
              const char* colon = strchr(idKey, ':');
              if (!colon) break;
              const char* valStart = strchr(colon, '"');
              if (!valStart) break;
              const char* valEnd = strchr(valStart + 1, '"');
              if (!valEnd) break;

              CollectionIndexRec ci;
              const size_t copyLen =
                  std::min<size_t>(sizeof(ci.collectionName) - 1, static_cast<size_t>(valEnd - valStart - 1));
              std::strncpy(ci.collectionName, valStart + 1, copyLen);
              ci.collectionName[copyLen] = '\0';
              ci.firstSeriesOffset = 0;
              ci.bookCount = 0;
              ci.flags = 1;  // user-defined collection
              userCollections.push_back(ci);

              scan = valEnd + 1;
            }
          }
        }

        // Count members per collection by matching collectionId in members array
        const char* membersStr = strstr(p, "\"members\"");
        if (membersStr) {
          const char* arrStart = strchr(membersStr, '[');
          if (arrStart) {
            const char* scan = arrStart + 1;
            while (*scan && *scan != ']') {
              const char* cidKey = strstr(scan, "\"collectionId\"");
              if (!cidKey) break;
              const char* colon = strchr(cidKey, ':');
              if (!colon) break;
              const char* valStart = strchr(colon + 1, '"');
              if (!valStart) break;
              const char* valEnd = strchr(valStart + 1, '"');
              if (!valEnd) break;

              char collId[32] = {};
              const size_t idLen = std::min<size_t>(sizeof(collId) - 1, static_cast<size_t>(valEnd - valStart - 1));
              std::strncpy(collId, valStart + 1, idLen);
              collId[idLen] = '\0';

              for (auto& uc : userCollections) {
                if (strcmp(uc.collectionName, collId) == 0) {
                  uc.bookCount++;
                  break;
                }
              }

              scan = valEnd + 1;
            }
          }
        }
      }
    }
  }

  // Sort user collections by display name so they interleave correctly
  // with metadata series in the merged index.
  USER_COLLECTIONS.ensureLoaded();
  auto sortKeyFor = [&](const CollectionIndexRec& ci) -> const char* {
    static thread_local char key[20];
    if (ci.flags & 1) {
      const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
      if (uc) {
        makeTitleSortKey(uc->name.c_str(), key);
        return key;
      }
    }
    makeTitleSortKey(ci.collectionName, key);
    return key;
  };
  std::sort(userCollections.begin(), userCollections.end(),
            [&](const CollectionIndexRec& a, const CollectionIndexRec& b) {
              return cmpSortKey(sortKeyFor(a), sortKeyFor(b)) < 0;
            });
  LOG_DBG("LIB", "BuildCollIdx: userCollections sorted count=%d", (int)userCollections.size());

  // Merge auto collections and user collections into a single sorted index
  std::vector<CollectionIndexRec> merged;
  merged.reserve(collections.size() + userCollections.size());
  size_t autoIdx = 0, userIdx = 0;
  while (autoIdx < collections.size() || userIdx < userCollections.size()) {
    if (autoIdx >= collections.size()) {
      merged.push_back(userCollections[userIdx++]);
    } else if (userIdx >= userCollections.size()) {
      merged.push_back(collections[autoIdx++]);
    } else {
      char keyAuto[20];
      makeTitleSortKey(collections[autoIdx].collectionName, keyAuto);
      const char* keyUser = sortKeyFor(userCollections[userIdx]);
      int c = cmpSortKey(keyAuto, keyUser);
      if (c < 0) {
        merged.push_back(collections[autoIdx++]);
      } else if (c > 0) {
        merged.push_back(userCollections[userIdx++]);
      } else {
        // Same name: auto series first, then user collection
        merged.push_back(collections[autoIdx++]);
        merged.push_back(userCollections[userIdx++]);
      }
    }
  }
  LOG_DBG("LIB", "BuildCollIdx: merged count=%d", (int)merged.size());

  // Classify auto collections into metadata vs folder by reading series.dat flags
  std::vector<CollectionIndexRec> metadataSeries;
  std::vector<CollectionIndexRec> folderCollections;
  for (const auto& ci : collections) {
    LOG_DBG("LIB", "BuildCollIdx: classify name=%s bookCount=%d flags=%d firstOffset=%u", ci.collectionName, (int)ci.bookCount, (int)ci.flags, (unsigned)ci.firstSeriesOffset);
    HalFile sf = Storage.open(kSeriesDat);
    if (sf) {
      const bool seekOk = sf.seek(ci.firstSeriesOffset);
      SeriesRec sr;
      const bool readOk = sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == sizeof(SeriesRec);
      sf.close();
      if (seekOk && readOk) {
        if ((sr.flags & 1) != 0) {
          folderCollections.push_back(ci);
        } else {
          metadataSeries.push_back(ci);
        }
      } else {
        metadataSeries.push_back(ci);
      }
    } else {
      metadataSeries.push_back(ci);
    }
  }
  LOG_DBG("LIB", "BuildCollIdx: classified metadata=%d folder=%d", (int)metadataSeries.size(), (int)folderCollections.size());

  // Write metadata series index
  LOG_DBG("LIB", "BuildCollIdx: writing metadata series idx entries=%d", (int)metadataSeries.size());
  if (!metadataSeries.empty() && SETTINGS.libraryMetadataSeries) {
    HalFile outF = Storage.open(kIdxMetadataSeries, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : metadataSeries) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
      LOG_DBG("LIB", "BuildCollIdx: wrote %s entries=%d", kIdxMetadataSeries, (int)metadataSeries.size());
    } else {
      LOG_ERR("LIB", "BuildCollIdx: cannot open %s for write", kIdxMetadataSeries);
    }
  } else {
    Storage.remove(kIdxMetadataSeries);
    LOG_DBG("LIB", "BuildCollIdx: removed %s", kIdxMetadataSeries);
  }

  // Write folder collections index
  LOG_DBG("LIB", "BuildCollIdx: writing folder collections idx entries=%d", (int)folderCollections.size());
  if (!folderCollections.empty() && SETTINGS.libraryFolderCollections) {
    HalFile outF = Storage.open(kIdxFolderCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : folderCollections) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
      LOG_DBG("LIB", "BuildCollIdx: wrote %s entries=%d", kIdxFolderCollections, (int)folderCollections.size());
    } else {
      LOG_ERR("LIB", "BuildCollIdx: cannot open %s for write", kIdxFolderCollections);
    }
  } else {
    Storage.remove(kIdxFolderCollections);
    LOG_DBG("LIB", "BuildCollIdx: removed %s", kIdxFolderCollections);
  }

  // Write user collections index
  LOG_DBG("LIB", "BuildCollIdx: writing user collections idx entries=%d", (int)userCollections.size());
  if (!userCollections.empty()) {
    HalFile outF = Storage.open(kIdxUserCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : userCollections) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
      LOG_DBG("LIB", "BuildCollIdx: wrote %s entries=%d", kIdxUserCollections, (int)userCollections.size());
    } else {
      LOG_ERR("LIB", "BuildCollIdx: cannot open %s for write", kIdxUserCollections);
    }
  } else {
    Storage.remove(kIdxUserCollections);
    LOG_DBG("LIB", "BuildCollIdx: removed %s", kIdxUserCollections);
  }

  // Also write legacy idx_collections.bin for backward compatibility
  // This contains all active collections (auto + user) for code paths that still use it
  LOG_DBG("LIB", "BuildCollIdx: writing legacy collections idx entries=%d", (int)merged.size());
  if (!merged.empty()) {
    HalFile outF = Storage.open(kIdxCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : merged) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
      LOG_DBG("LIB", "BuildCollIdx: wrote %s entries=%d", kIdxCollections, (int)merged.size());
    } else {
      LOG_ERR("LIB", "BuildCollIdx: cannot open %s for write", kIdxCollections);
    }
  } else {
    Storage.remove(kIdxCollections);
    LOG_DBG("LIB", "BuildCollIdx: removed %s", kIdxCollections);
  }

  LOG_DBG("LIB", "BuildCollIdx: %d metadata + %d folder + %d user = %d total entries", metadataSeries.size(),
          folderCollections.size(), userCollections.size(), merged.size());
  LOG_DBG("LIB", "BuildCollIdx: metadataSeriesIdx=%d folderIdx=%d userIdx=%d legacyIdx=%d",
          !metadataSeries.empty() && SETTINGS.libraryMetadataSeries ? 1 : 0,
          !folderCollections.empty() && SETTINGS.libraryFolderCollections ? 1 : 0,
          !userCollections.empty() ? 1 : 0,
          !merged.empty() ? 1 : 0);
  IndexCacheManager::invalidateCollections();
  LOG_DBG("LIB", "BuildCollIdx: done");
  return true;
}

// =========================================================================
// Mixed index builder (series tiles + standalone books together)
// =========================================================================

bool buildMixedIndex(SortMode sortMode) {
  LOG_DBG("LIB", "BuildMixedIdx: start");
  const unsigned long t0 = millis();

  // Phase 0: read series.dat and user collections, build a sorted set of book IDs that belong
  // to a series/collection. Used to skip them when emitting standalone entries.
  std::vector<uint32_t> collectionBookIds;
  {
    HalFile sf = Storage.open(kSeriesDat);
    if (sf) {
      const size_t totalSeries = sf.size() / sizeof(SeriesRec);
      collectionBookIds.reserve(totalSeries);
      SeriesRec sr;
      for (size_t i = 0; i < totalSeries; ++i) {
        if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == sizeof(SeriesRec)) {
          if (sr.bookId > 0) collectionBookIds.push_back(sr.bookId);
        }
      }
      sf.close();
    }

    // Include user collection members so they don't appear as standalone books.
    USER_COLLECTIONS.ensureLoaded();
    const auto& members = USER_COLLECTIONS.allMembers();
    collectionBookIds.reserve(collectionBookIds.size() + members.size());
    for (const auto& m : members) {
      if (m.bookId > 0) collectionBookIds.push_back(m.bookId);
    }
    std::sort(collectionBookIds.begin(), collectionBookIds.end());
  }

  LOG_DBG("LIB", "BuildMixedIdx: collection ids sorted size=%d", (int)collectionBookIds.size());
  if (!buildBookLookup()) {
    LOG_ERR("LIB", "BuildMixedIdx: book lookup build failed");
    return false;
  }
  LOG_DBG("LIB", "BuildMixedIdx: book lookup built");
  LOG_DBG("LIB", "BuildMixedIdx: book lookup built");

  int chunkCount = 0;
  {
    // Phase 1a: standalone books from library.dat
    HalFile dat = Storage.open(kDatFile);
    if (!dat) {
      LOG_ERR("LIB", "BuildMixedIdx: cannot open library.dat");
      return false;
    }
    const int totalRecs = static_cast<int>(dat.size() / kRecordSize);
    if (totalRecs == 0) {
      dat.close();
      LOG_ERR("LIB", "BuildMixedIdx: library.dat empty");
      return false;
    }
    LOG_DBG("LIB", "BuildMixedIdx: totalRecs=%d", totalRecs);

    std::vector<IndexRec> chunk;
    chunk.reserve(kChunkRecs);
    Record rec;
    for (int rp = 0; rp < totalRecs; ++rp) {
      if (!readRecord(static_cast<uint32_t>(rp), rec)) continue;
      if (rec.tombstone()) continue;
      if (std::binary_search(collectionBookIds.begin(), collectionBookIds.end(), rec.id)) continue;

      IndexRec ir;
      if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
        makeSortKey(rec.author, ir.sortKey);
      } else {
        makeTitleSortKey(rec.title, ir.sortKey);
      }
      ir.bookId = rec.id;
      ir.recordOffset = static_cast<uint32_t>(rp * kRecordSize);
      chunk.push_back(ir);

      if (static_cast<int>(chunk.size()) >= kChunkRecs || rp == totalRecs - 1) {
        if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
          std::qsort(chunk.data(), chunk.size(), sizeof(IndexRec), cmpByAuthor);
        } else {
          std::qsort(chunk.data(), chunk.size(), sizeof(IndexRec), cmpByTitle);
        }
        char tmpPath[96];
        std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, chunkCount++);
        HalFile tf = Storage.open(tmpPath, O_CREAT | O_WRONLY | O_TRUNC);
        if (tf) {
          tf.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size() * kIndexRecSize);
          tf.close();
        }
        chunk.clear();
      }
    }
    dat.close();
  }
  LOG_DBG("LIB", "BuildMixedIdx: phase1a done chunkCount=%d", chunkCount);

  {
    // Phase 1b: series tiles from separate indices based on settings
    std::vector<CollectionIndexRec> allCollections;

    // Always include user collections
    {
      HalFile uf = Storage.open(kIdxUserCollections);
      if (uf) {
        const int total = static_cast<int>(uf.size() / sizeof(CollectionIndexRec));
        LOG_DBG("LIB", "BuildMixedIdx: user collections idx count=%d", total);
        CollectionIndexRec ci;
        for (int i = 0; i < total; ++i) {
          if (uf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
            allCollections.push_back(ci);
          }
        }
        uf.close();
      }
    }

    // Include metadata series if enabled
    if (SETTINGS.libraryMetadataSeries) {
      HalFile mf = Storage.open(kIdxMetadataSeries);
      if (mf) {
        const int total = static_cast<int>(mf.size() / sizeof(CollectionIndexRec));
        CollectionIndexRec ci;
        for (int i = 0; i < total; ++i) {
          if (mf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
            allCollections.push_back(ci);
          }
        }
        mf.close();
      }
    }

    // Include folder collections if enabled
    if (SETTINGS.libraryFolderCollections) {
      HalFile ff = Storage.open(kIdxFolderCollections);
      if (ff) {
        const int total = static_cast<int>(ff.size() / sizeof(CollectionIndexRec));
        CollectionIndexRec ci;
        for (int i = 0; i < total; ++i) {
          if (ff.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
            allCollections.push_back(ci);
          }
        }
        ff.close();
      }
    }

    if (!allCollections.empty()) {
      LOG_DBG("LIB", "BuildMixedIdx: allCollections count=%d", (int)allCollections.size());
      // Build lookup from combined index so mixed index references match kIdxCollections
      std::unordered_map<std::string, int> combinedIndexMap;
      {
        HalFile cf = Storage.open(kIdxCollections);
        if (cf) {
          const int total = static_cast<int>(cf.size() / sizeof(CollectionIndexRec));
          LOG_DBG("LIB", "BuildMixedIdx: combined collections idx count=%d", total);
          CollectionIndexRec ci;
          for (int i = 0; i < total; ++i) {
            if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
              char key[82];
              std::snprintf(key, sizeof(key), "%s|%d", ci.collectionName, ci.flags);
              combinedIndexMap[key] = i;
            }
          }
          cf.close();
        }
      }

      std::vector<IndexRec> chunk;
      chunk.reserve(kChunkRecs);
      for (int idx = 0; idx < static_cast<int>(allCollections.size()); ++idx) {
        const auto& ci = allCollections[idx];
        IndexRec ir;
        if (ci.flags & 1) {
          USER_COLLECTIONS.ensureLoaded();
          const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
          if (uc) {
            if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
              makeSortKey(uc->name.c_str(), ir.sortKey);
            } else {
              makeTitleSortKey(uc->name.c_str(), ir.sortKey);
            }
          } else {
            if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
              makeSortKey(ci.collectionName, ir.sortKey);
            } else {
              makeTitleSortKey(ci.collectionName, ir.sortKey);
            }
          }
        } else {
          if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
            makeSortKey(ci.collectionName, ir.sortKey);
          } else {
            makeTitleSortKey(ci.collectionName, ir.sortKey);
          }
        }

        // Look up the index in the combined kIdxCollections
        char key[82];
        std::snprintf(key, sizeof(key), "%s|%d", ci.collectionName, ci.flags);
        auto it = combinedIndexMap.find(key);
        int combinedIdx = (it != combinedIndexMap.end()) ? it->second : idx;
        ir.bookId = 0x80000000u | static_cast<uint32_t>(combinedIdx);

        // Resolve first book path now so queryMixed() does not need to
        // scan series.dat + library.dat at runtime.
        Record firstRec;
        bool foundFirst = false;
        HalFile sf = Storage.open(kSeriesDat);
        if (sf) {
          sf.seek(ci.firstSeriesOffset);
          SeriesRec sr;
          for (uint32_t b = 0; b < ci.bookCount; ++b) {
            if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
            if (sr.bookId == 0) continue;
            if (readRecordByBookId(sr.bookId, firstRec)) {
              foundFirst = true;
              break;
            }
          }
          sf.close();
        }
        uint32_t firstOffset = 0xFFFFFFFFu;
        if (foundFirst && firstRec.id > 0) {
          uint32_t off = 0;
          if (findRecordOffset(firstRec.id, off)) {
            firstOffset = off;
          }
        }
        ir.recordOffset = firstOffset;
        chunk.push_back(ir);

        if (static_cast<int>(chunk.size()) >= kChunkRecs || idx == static_cast<int>(allCollections.size()) - 1) {
          if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
            std::qsort(chunk.data(), chunk.size(), sizeof(IndexRec), cmpByAuthor);
          } else {
            std::qsort(chunk.data(), chunk.size(), sizeof(IndexRec), cmpByTitle);
          }
          char tmpPath[96];
          std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, chunkCount++);
          HalFile tf = Storage.open(tmpPath, O_CREAT | O_WRONLY | O_TRUNC);
          if (tf) {
            tf.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size() * kIndexRecSize);
            tf.close();
          }
          chunk.clear();
        }
      }
    }
  }

  if (chunkCount == 0) {
    LOG_DBG("LIB", "BuildMixedIdx: no entries");
    return false;
  }
  LOG_DBG("LIB", "BuildMixedIdx: phase1b done chunkCount=%d", chunkCount);

  // Phase 2: k-way merge into idx_mixed.bin
  HalFile outF = Storage.open(kIdxMixed, O_CREAT | O_WRONLY | O_TRUNC);
  if (!outF) {
    LOG_ERR("LIB", "BuildMixedIdx: cannot open %s", kIdxMixed);
    return false;
  }
  LOG_DBG("LIB", "BuildMixedIdx: merge start chunkCount=%d", chunkCount);

  std::vector<ChunkReader> readers(chunkCount);
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    readers[i].open(tmpPath);
  }

  int mergedCount = 0;
  while (true) {
    int best = -1;
    for (int i = 0; i < chunkCount; ++i) {
      if (readers[i].eof) continue;
      if (best < 0) {
        best = i;
      } else if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
        if (cmpByAuthor(&readers[i].cur, &readers[best].cur) < 0) best = i;
      } else {
        if (cmpByTitle(&readers[i].cur, &readers[best].cur) < 0) best = i;
      }
    }
    if (best < 0) break;
    writeIndexRec(outF, readers[best].cur);
    readers[best].advance();
    ++mergedCount;
  }
  LOG_DBG("LIB", "BuildMixedIdx: merge done mergedCount=%d", mergedCount);

  outF.close();
  for (int i = 0; i < chunkCount; ++i) readers[i].close();

  // Phase 3: delete temp chunks
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    Storage.remove(tmpPath);
  }

  LOG_DBG("LIB", "BuildMixedIdx: done in %lu ms, %d chunks", millis() - t0, chunkCount);
  LOG_DBG("LIB", "BuildMixedIdx: final mixed index entries check...");
  {
    HalFile mf = Storage.open(kIdxMixed);
    if (mf) {
      const int mixedCount = static_cast<int>(mf.size() / kIndexRecSize);
      mf.close();
      LOG_DBG("LIB", "BuildMixedIdx: idx_mixed.bin entries=%d", mixedCount);
    } else {
      LOG_ERR("LIB", "BuildMixedIdx: idx_mixed.bin missing!");
    }
  }
  if (IndexCacheManager::loadMixedIndex()) {
    LOG_DBG("LIB", "BuildMixedIdx: RAM cache loaded");
  }
  return true;
}

// ---- Collections query ----

// Compute a disambiguation subtitle for folder-fallback series.
// Returns the parent folder basename if the series is folder-fallback,
// empty string otherwise. For user collections, always returns empty.

// ---- Book lookup cache for library.dat ----
// queryMixed() used to scan library.dat linearly for every series book.
// With ~300 records that turns into O(collections * series_len * lib_size)
// and was measured at ~9s. Build a sorted (bookId, offset) table so
// lookups become O(log n). Actual record reads are served through a
// small vector-read buffer (4 x 4KB blocks) to avoid per-record SD seeks.

struct BookIdOffset {
  uint32_t id;
  uint32_t offset;
};

static std::vector<BookIdOffset> g_bookLookup;
static size_t g_bookDatSize = 0;

static void invalidateBookLookup() {
  g_bookLookup.clear();
  g_bookDatSize = 0;
}

static bool buildBookLookup() {
  LOG_DBG("LIB", "buildBookLookup: start");
  HalFile f = Storage.open(kDatFile);
  if (!f) {
    LOG_ERR("LIB", "buildBookLookup: cannot open library.dat");
    return false;
  }
  const size_t datSize = static_cast<size_t>(f.size());
  if (datSize == g_bookDatSize && !g_bookLookup.empty()) {
    f.close();
    LOG_DBG("LIB", "buildBookLookup: cache hit size=%u", (unsigned)datSize);
    return true;
  }
  g_bookDatSize = datSize;
  const int totalRecs = static_cast<int>(datSize / kRecordSize);
  LOG_DBG("LIB", "buildBookLookup: totalRecs=%d", totalRecs);
  g_bookLookup.clear();
  g_bookLookup.reserve(totalRecs > 0 ? static_cast<size_t>(totalRecs) : 0);
  LOG_DBG("LIB", "buildBookLookup: reserved %d", (int)g_bookLookup.capacity());
  Record rec;
  for (int rp = 0; rp < totalRecs; ++rp) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize)) {
      g_bookLookup.push_back({rec.id, static_cast<uint32_t>(rp * kRecordSize)});
    }
  }
  std::sort(g_bookLookup.begin(), g_bookLookup.end(),
            [](const BookIdOffset& a, const BookIdOffset& b) { return a.id < b.id; });
  f.close();
  LOG_DBG("LIB", "buildBookLookup: done count=%d", (int)g_bookLookup.size());
  return true;
}

static bool findRecordOffset(uint32_t bookId, uint32_t& offset) {
  if (g_bookLookup.empty()) return false;
  auto it = std::lower_bound(g_bookLookup.begin(), g_bookLookup.end(), bookId,
                             [](const BookIdOffset& entry, uint32_t id) { return entry.id < id; });
  if (it == g_bookLookup.end() || it->id != bookId) return false;
  offset = it->offset;
  return true;
}

static std::string getCollectionSubtitle(const CollectionIndexRec& ci, HalFile& sf) {
  if ((ci.flags & 1) != 0) return "";  // user collection: no subtitle
  if (!sf) return "";

  // Check if any book in this series is folder-fallback
  sf.seek(ci.firstSeriesOffset);
  SeriesRec sr;
  for (uint32_t b = 0; b < ci.bookCount; ++b) {
    if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
    if (sr.bookId == 0) continue;
    if ((sr.flags & 1) != 0) {
      // Found folder-fallback entry; resolve book path
      uint32_t offset;
      if (findRecordOffset(sr.bookId, offset)) {
        const Record* recPtr = g_datBuffer.getRecord(offset);
        if (recPtr && !recPtr->tombstone()) {
          // Extract parent folder basename from path
          const char* p = recPtr->path;
          const char* lastSlash = strrchr(p, '/');
          if (lastSlash && lastSlash > p) {
            // Find the slash before the last component
            const char* prevSlash = lastSlash;
            while (prevSlash > p && *(prevSlash - 1) != '/') --prevSlash;
            if (prevSlash > p) {
              return std::string(prevSlash, static_cast<size_t>(lastSlash - prevSlash));
            }
          }
          return "";
        }
      }
      return "";
    }
  }
  return "";
}

int queryCollections(BookRef* out, int page, int pageSize, int coverWidth, int coverHeight) {
  buildBookLookup();
  // Read all active collections from separate index files based on settings
  std::vector<CollectionIndexRec> allCollections;

  // Always include user collections
  {
    HalFile uf = Storage.open(kIdxUserCollections);
    if (uf) {
      const int total = static_cast<int>(uf.size() / sizeof(CollectionIndexRec));
      CollectionIndexRec ci;
      for (int i = 0; i < total; ++i) {
        if (uf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
          allCollections.push_back(ci);
        }
      }
      uf.close();
    }
  }

  // Include metadata series if enabled
  if (SETTINGS.libraryMetadataSeries) {
    HalFile mf = Storage.open(kIdxMetadataSeries);
    if (mf) {
      const int total = static_cast<int>(mf.size() / sizeof(CollectionIndexRec));
      CollectionIndexRec ci;
      for (int i = 0; i < total; ++i) {
        if (mf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
          allCollections.push_back(ci);
        }
      }
      mf.close();
    }
  }

  // Include folder collections if enabled
  if (SETTINGS.libraryFolderCollections) {
    HalFile ff = Storage.open(kIdxFolderCollections);
    if (ff) {
      const int total = static_cast<int>(ff.size() / sizeof(CollectionIndexRec));
      CollectionIndexRec ci;
      for (int i = 0; i < total; ++i) {
        if (ff.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) == sizeof(CollectionIndexRec)) {
          allCollections.push_back(ci);
        }
      }
      ff.close();
    }
  }

  const int total = static_cast<int>(allCollections.size());
  const int start = page * pageSize;
  if (start >= total) {
    return 0;
  }

  HalFile sf = Storage.open(kSeriesDat);

  CollectionIndexRec ci;
  int count = 0;
  for (int i = start; i < total && count < pageSize; ++i) {
    ci = allCollections[i];
    BookRef& ref = out[count];
    ref.id = 0x80000000u | static_cast<uint32_t>(i);

    // Resolve display name for user collections from UserCollectionsStore.
    std::string displayName = ci.collectionName;
    if ((ci.flags & 1) != 0) {
      USER_COLLECTIONS.ensureLoaded();
      const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
      if (uc) displayName = uc->name;
    }

    // Append folder-fallback disambiguation subtitle if needed
    std::string subtitle = getCollectionSubtitle(ci, sf);
    if (!subtitle.empty()) {
      char combined[80];
      std::snprintf(combined, sizeof(combined), "%s / %s", displayName.c_str(), subtitle.c_str());
      std::strncpy(ref.title, combined, 64);
      ref.title[64] = '\0';
    } else {
      std::strncpy(ref.title, displayName.c_str(), 64);
      ref.title[64] = '\0';
    }
    // Get accurate book count for user collections from UserCollectionsStore
    int bookCount = ci.bookCount;
    if ((ci.flags & 1) != 0) {
      USER_COLLECTIONS.ensureLoaded();
      const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
      if (uc) {
        bookCount = USER_COLLECTIONS.memberCount(uc->id);
      }
    }
    snprintf(ref.author, sizeof(ref.author), "%d books", bookCount);
    ref.path[0] = '\0';
    ref.isFavorite = false;
    ref.isOpened = false;
    ref.isCompleted = false;
    ref.isHidden = false;
    ref.isCollection = (ci.flags & 1) != 0;

    // Resolve first book path that has an existing cover so the UI
    // can show a meaningful cover. If no book has a cover yet, path
    // stays empty and the UI will render the collection placeholder.
    if (coverWidth > 0 && coverHeight > 0) {
      if (ci.flags & 1) {
        // User collection: resolve from UserCollectionsStore
        USER_COLLECTIONS.ensureLoaded();
        const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
        if (uc) {
          auto members = USER_COLLECTIONS.members(uc->id);
          for (const auto& m : members) {
            uint32_t offset;
            if (findRecordOffset(m.bookId, offset)) {
              const Record* recPtr = g_datBuffer.getRecord(offset);
              if (recPtr && !recPtr->tombstone()) {
                const std::string bookPath(recPtr->path);
                const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
                if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                  std::strncpy(ref.path, recPtr->path, sizeof(ref.path) - 1);
                  ref.path[sizeof(ref.path) - 1] = '\0';
                  break;
                }
              }
            }
          }
        }
      } else if (sf) {
        // Auto series: resolve from series.dat
        sf.seek(ci.firstSeriesOffset);
        SeriesRec sr;
        for (uint32_t b = 0; b < ci.bookCount; ++b) {
          if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
          if (sr.bookId == 0) continue;
          uint32_t offset;
          if (findRecordOffset(sr.bookId, offset)) {
            const Record* recPtr = g_datBuffer.getRecord(offset);
            if (recPtr && !recPtr->tombstone()) {
              const std::string bookPath(recPtr->path);
              const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
              if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                std::strncpy(ref.path, recPtr->path, sizeof(ref.path) - 1);
                ref.path[sizeof(ref.path) - 1] = '\0';
                break;
              }
            }
          }
          if (ref.path[0] != '\0') break;
        }
      }
    }

    ++count;
  }

  if (sf) sf.close();
  return count;
}

int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx) {
  HalFile cf = Storage.open(kIdxCollections);
  if (!cf) return 0;
  const int totalColls = static_cast<int>(cf.size() / sizeof(CollectionIndexRec));
  if (collectionIdx < 0 || collectionIdx >= totalColls) {
    cf.close();
    return 0;
  }
  cf.seek(static_cast<uint32_t>(collectionIdx) * sizeof(CollectionIndexRec));
  CollectionIndexRec ci;
  if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) !=
      static_cast<int>(sizeof(CollectionIndexRec))) {
    cf.close();
    return 0;
  }
  cf.close();

  // Resolve library.dat records via the in-memory book lookup so deleted or
  // tombstoned books are filtered out BEFORE pagination (keeps page alignment).
  buildBookLookup();

  // User collections: resolve books from UserCollectionsStore, not series.dat
  if (ci.flags & 1) {
    USER_COLLECTIONS.ensureLoaded();
    // collectionName stores the user collection id
    const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
    if (uc) {
      auto members = USER_COLLECTIONS.members(uc->id);
      std::sort(members.begin(), members.end(),
                [](const CollectionMember& a, const CollectionMember& b) { return a.position < b.position; });
      // Filter to books that actually exist in library.dat so count/pagination
      // matches collectionBookCount().
      std::vector<CollectionMember> valid;
      valid.reserve(members.size());
      for (const auto& m : members) {
        uint32_t offset;
        if (findRecordOffset(m.bookId, offset)) valid.push_back(m);
      }
      const int start = page * pageSize;
      const int end = std::min(start + pageSize, static_cast<int>(valid.size()));
      int count = 0;
      for (int i = start; i < end; ++i) {
        uint32_t offset;
        if (findRecordOffset(valid[i].bookId, offset)) {
          const Record* recPtr = g_datBuffer.getRecord(offset);
          if (recPtr && !recPtr->tombstone()) {
            recordToBookRef(*recPtr, out[count++]);
          }
        }
      }
      return count;
    }
    return 0;
  }

  // Auto series: read from series.dat, resolve via book lookup (no per-book SD open)
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) return 0;
  sf.seek(ci.firstSeriesOffset);

  struct BookSlot {
    uint32_t bookId;
    uint32_t seriesIndex;
    uint32_t recordOffset;
  };
  std::vector<BookSlot> slots;
  SeriesRec sr;
  for (uint32_t i = 0; i < ci.bookCount; ++i) {
    if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) break;
    if (sr.bookId == 0) continue;
    // Find Record by bookId in library.dat using book lookup (fast, no per-book SD open)
    uint32_t offset;
    if (findRecordOffset(sr.bookId, offset)) {
      slots.push_back({sr.bookId, static_cast<uint32_t>(sr.seriesIndex), offset});
    }
  }
  sf.close();

  // Sort by seriesIndex numerically
  std::sort(slots.begin(), slots.end(),
            [](const BookSlot& a, const BookSlot& b) { return a.seriesIndex < b.seriesIndex; });

  // Paginate
  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(slots.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    const Record* recPtr = g_datBuffer.getRecord(slots[i].recordOffset);
    if (recPtr && !recPtr->tombstone()) {
      recordToBookRef(*recPtr, out[count++]);
    }
  }
  return count;
}

int totalCollections() {
  HalFile f = Storage.open(kIdxCollections);
  if (!f) return 0;
  const int count = static_cast<int>(f.size() / sizeof(CollectionIndexRec));
  f.close();
  return count;
}

int collectionBookCount(int collectionIdx) {
  HalFile cf = Storage.open(kIdxCollections);
  if (!cf) return 0;
  const int totalColls = static_cast<int>(cf.size() / sizeof(CollectionIndexRec));
  if (collectionIdx < 0 || collectionIdx >= totalColls) {
    cf.close();
    return 0;
  }
  cf.seek(static_cast<uint32_t>(collectionIdx) * sizeof(CollectionIndexRec));
  CollectionIndexRec ci;
  if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) !=
      static_cast<int>(sizeof(CollectionIndexRec))) {
    cf.close();
    return 0;
  }
  cf.close();

  if (ci.flags & 1) {
    USER_COLLECTIONS.ensureLoaded();
    const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
    if (uc) {
      // Count only members that actually exist in library.dat (not tombstoned/deleted)
      // to match queryCollectionBooks pagination.
      buildBookLookup();
      auto members = USER_COLLECTIONS.members(uc->id);
      int validCount = 0;
      for (const auto& m : members) {
        uint32_t offset;
        if (findRecordOffset(m.bookId, offset)) ++validCount;
      }
      return validCount;
    }
    return 0;
  }

  // Auto series: count only books that actually exist in library.dat (not tombstoned/deleted)
  buildBookLookup();
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) return 0;
  sf.seek(ci.firstSeriesOffset);

  SeriesRec sr;
  int validCount = 0;
  for (uint32_t i = 0; i < ci.bookCount; ++i) {
    if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) break;
    if (sr.bookId == 0) continue;
    uint32_t offset;
    if (findRecordOffset(sr.bookId, offset)) {
      ++validCount;
    }
  }
  sf.close();
  return validCount;
}

// ---- Mixed view query ----

// Forward declaration for filter matching used by queryMixed/totalMixedMatching.
static bool matchesFilter(const Record& rec, FilterMode m);

int queryMixed(BookRef* out, int page, int pageSize, const char* searchFilter, FilterMode filterMode, int coverWidth,
               int coverHeight, SortMode sortMode) {
  unsigned long t_total = LibraryPerf::nowMs();
  buildBookLookup();

  if (!g_datBuffer.open()) return 0;

  HalFile mf;
  const IndexRec* mixedData = nullptr;
  int mixedTotal = 0;
  bool usingCache = false;

  if (IndexCacheManager::hasMixedIndex()) {
    mixedData = IndexCacheManager::mixedIndexData();
    mixedTotal = IndexCacheManager::mixedIndexTotal();
    usingCache = true;
  } else {
    mf = Storage.open(kIdxMixed);
    if (!mf) return 0;
    mixedTotal = static_cast<int>(mf.size() / kIndexRecSize);
  }

  HalFile cf;
  const LibraryIndex::CollectionIndexRec* collData = nullptr;
  int collTotal = 0;
  bool usingCollCache = false;

  if (IndexCacheManager::hasCollectionsIndex()) {
    collData = IndexCacheManager::collectionsIndexData();
    collTotal = IndexCacheManager::collectionsIndexTotal();
    usingCollCache = true;
  } else {
    cf = Storage.open(kIdxCollections);
    if (!cf) return 0;
    collTotal = static_cast<int>(cf.size() / sizeof(LibraryIndex::CollectionIndexRec));
  }
  const bool hasCollIndex = usingCollCache || !!cf;

  HalFile sf = Storage.open(kSeriesDat);

  // Collect only the requested page from the already-sorted index.
  // The index order matches the query sortMode, so filtering preserves order.
  // We materialize BookRefs only for the visible window and skip the full
  // materialization + final sort.
  const int start = page * pageSize;
  const int need = start + pageSize;
  int matchedSoFar = 0;
  int count = 0;

  unsigned long t_iter = LibraryPerf::nowMs();
  unsigned long t_coll = 0;
  unsigned long t_series = 0;
  unsigned long t_dat = 0;
  unsigned long t_user = 0;

  for (int i = 0; i < mixedTotal && count < pageSize; ++i) {
    IndexRec ir;
    if (usingCache) {
      ir = mixedData[i];
    } else {
      mf.seek(static_cast<uint32_t>(i) * kIndexRecSize);
      if (!readIndexRec(mf, ir)) break;
    }

    bool isMatch = true;
    Record rec;
    if (ir.bookId & 0x80000000u) {
      const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
      if (hasCollIndex) {
        unsigned long t1 = LibraryPerf::nowMs();
        CollectionIndexRec ci;
        if (usingCollCache) {
          ci = collData[collIdx];
          t_coll += LibraryPerf::nowMs() - t1;
        } else {
          cf.seek(static_cast<uint32_t>(collIdx) * sizeof(CollectionIndexRec));
          if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != sizeof(CollectionIndexRec)) {
            t_coll += LibraryPerf::nowMs() - t1;
            continue;
          }
          t_coll += LibraryPerf::nowMs() - t1;
        }

        if (searchFilter && searchFilter[0] != '\0') {
          char key[20];
          makeTitleSortKey(ci.collectionName, key);
          isMatch = substringMatch(key, searchFilter);
        }

        if (isMatch && filterMode != FilterMode::ALL) {
          if (ci.flags & 1) {
            unsigned long t_user_start = LibraryPerf::nowMs();
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            t_user += LibraryPerf::nowMs() - t_user_start;
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              bool anyMatch = false;
              for (const auto& m : members) {
                uint32_t offset;
                if (findRecordOffset(m.bookId, offset)) {
                  unsigned long t_dat_start = LibraryPerf::nowMs();
                  const Record* recPtr = g_datBuffer.getRecord(offset);
                  if (recPtr && !recPtr->tombstone()) {
                    t_dat += LibraryPerf::nowMs() - t_dat_start;
                    if (matchesFilter(*recPtr, filterMode)) {
                      anyMatch = true;
                      break;
                    }
                  }
                  t_dat += LibraryPerf::nowMs() - t_dat_start;
                }
              }
              isMatch = anyMatch;
            } else {
              isMatch = false;
            }
          } else if (sf) {
            unsigned long t_series_start = LibraryPerf::nowMs();
            sf.seek(ci.firstSeriesOffset);
            SeriesRec sr;
            bool anyMatch = false;
            for (uint32_t b = 0; b < ci.bookCount; ++b) {
              if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
              if (sr.bookId == 0) continue;
              uint32_t offset;
              if (findRecordOffset(sr.bookId, offset)) {
                unsigned long t_dat_start = LibraryPerf::nowMs();
                const Record* recPtr = g_datBuffer.getRecord(offset);
                if (recPtr && !recPtr->tombstone()) {
                  t_dat += LibraryPerf::nowMs() - t_dat_start;
                  if (matchesFilter(*recPtr, filterMode)) {
                    anyMatch = true;
                    break;
                  }
                }
                t_dat += LibraryPerf::nowMs() - t_dat_start;
              }
            }
            t_series += LibraryPerf::nowMs() - t_series_start;
            isMatch = anyMatch;
          }
        }
      }
    } else {
      unsigned long t_dat_start = LibraryPerf::nowMs();
      const Record* recPtr = g_datBuffer.getRecord(ir.recordOffset);
      if (recPtr) {
        rec = *recPtr;
        isMatch = matchesFilter(rec, filterMode);
        if (isMatch && searchFilter && searchFilter[0] != '\0') {
          char titleKey[20];
          makeTitleSortKey(rec.title, titleKey);
          char authorKey[20];
          makeSortKey(rec.author, authorKey);
          isMatch = substringMatch(titleKey, searchFilter) || substringMatch(authorKey, searchFilter);
        }
      } else {
        isMatch = false;
      }
      t_dat += LibraryPerf::nowMs() - t_dat_start;
    }

    if (!isMatch) continue;

    if (matchedSoFar >= start && matchedSoFar < need) {
      BookRef ref;
      if (ir.bookId & 0x80000000u) {
        const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
        if (hasCollIndex) {
          CollectionIndexRec ci;
          if (usingCollCache) {
            ci = collData[collIdx];
          } else if (cf) {
            cf.seek(static_cast<uint32_t>(collIdx) * sizeof(CollectionIndexRec));
            if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != sizeof(CollectionIndexRec)) {
              ++matchedSoFar;
              continue;
            }
          }

          ref.id = ir.bookId;

          std::string displayName = ci.collectionName;
          if ((ci.flags & 1) != 0) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) displayName = uc->name;
          }

          std::string subtitle = getCollectionSubtitle(ci, sf);
          if (!subtitle.empty()) {
            char combined[80];
            std::snprintf(combined, sizeof(combined), "%s / %s", displayName.c_str(), subtitle.c_str());
            std::strncpy(ref.title, combined, 64);
            ref.title[64] = '\0';
          } else {
            std::strncpy(ref.title, displayName.c_str(), 64);
            ref.title[64] = '\0';
          }

          int bookCount = ci.bookCount;
          if ((ci.flags & 1) != 0) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              bookCount = USER_COLLECTIONS.memberCount(uc->id);
            }
          }
          snprintf(ref.author, sizeof(ref.author), "%d books", bookCount);
          ref.path[0] = '\0';
          ref.isFavorite = false;
          ref.isOpened = false;
          ref.isCompleted = false;
          ref.isHidden = false;
          ref.isCollection = (ci.flags & 1) != 0;
        }
      } else {
        recordToBookRef(rec, ref);
      }

      out[count++] = ref;
      if (coverWidth > 0 && coverHeight > 0 && (out[count - 1].id & 0x80000000u)) {
        const int collIdx = static_cast<int>(out[count - 1].id & 0x7FFFFFFFu);
        CollectionIndexRec ci;
        if (hasCollIndex) {
          if (usingCollCache) {
            ci = collData[collIdx];
          } else if (cf) {
            cf.seek(static_cast<uint32_t>(collIdx) * sizeof(CollectionIndexRec));
            if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != sizeof(CollectionIndexRec)) {
              continue;
            }
          }
          if (ci.flags & 1) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              for (const auto& m : members) {
                uint32_t offset;
                if (findRecordOffset(m.bookId, offset)) {
                  const Record* recPtr = g_datBuffer.getRecord(offset);
                  if (recPtr && !recPtr->tombstone()) {
                    const std::string bookPath(recPtr->path);
                    const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
                    if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                      std::strncpy(out[count - 1].path, recPtr->path, sizeof(out[count - 1].path) - 1);
                      out[count - 1].path[sizeof(out[count - 1].path) - 1] = '\0';
                      break;
                    }
                  }
                }
              }
            }
          } else if (sf) {
            sf.seek(ci.firstSeriesOffset);
            SeriesRec sr;
            for (uint32_t b = 0; b < ci.bookCount; ++b) {
              if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
              if (sr.bookId == 0) continue;
              uint32_t offset;
              if (findRecordOffset(sr.bookId, offset)) {
                const Record* recPtr = g_datBuffer.getRecord(offset);
                if (recPtr && !recPtr->tombstone()) {
                  const std::string bookPath(recPtr->path);
                  const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
                  if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                    std::strncpy(out[count - 1].path, recPtr->path, sizeof(out[count - 1].path) - 1);
                    out[count - 1].path[sizeof(out[count - 1].path) - 1] = '\0';
                    break;
                  }
                }
              }
              if (out[count - 1].path[0] != '\0') break;
            }
          }
        }
      }
    }

    ++matchedSoFar;
  }

  LibraryPerf::logElapsed("queryMixed_iteration", t_iter);
  LOG_DBG("LIB-PERF", "queryMixed_materialized: count=%d matched=%d page=%d pageSize=%d", (int)count, (int)matchedSoFar,
          (int)page, (int)pageSize);

  if (cf) cf.close();
  if (sf) sf.close();
  if (!usingCache) mf.close();
  return count;
}

int totalMixed() {
  HalFile f = Storage.open(kIdxMixed);
  if (!f) return 0;
  const int total = static_cast<int>(f.size() / kIndexRecSize);
  f.close();
  return total;
}

int totalMixedMatching(const char* searchFilter, FilterMode filterMode) {
  buildBookLookup();

  if (!g_datBuffer.open()) return 0;
  HalFile sf = Storage.open(kSeriesDat);

  HalFile mf;
  const LibraryIndex::IndexRec* mixedData = nullptr;
  int mixedTotal = 0;
  bool usingCache = false;

  if (IndexCacheManager::hasMixedIndex()) {
    mixedData = IndexCacheManager::mixedIndexData();
    mixedTotal = IndexCacheManager::mixedIndexTotal();
    usingCache = true;
  } else {
    mf = Storage.open(kIdxMixed);
    if (!mf) {
      if (sf) sf.close();
      return 0;
    }
    mixedTotal = static_cast<int>(mf.size() / kIndexRecSize);
  }

  const bool hasSearch = (searchFilter && searchFilter[0] != '\0');
  if (!hasSearch && filterMode == FilterMode::ALL) {
    if (usingCache) {
      // cache read-only; nothing to close for mf
    } else {
      mf.close();
    }
    if (sf) sf.close();
    return totalMixed();
  }

  HalFile cf;
  const LibraryIndex::CollectionIndexRec* collData = nullptr;
  int collTotal = 0;
  bool usingCollCache = false;
  if (IndexCacheManager::hasCollectionsIndex()) {
    collData = IndexCacheManager::collectionsIndexData();
    collTotal = IndexCacheManager::collectionsIndexTotal();
    usingCollCache = true;
  } else {
    cf = Storage.open(kIdxCollections);
  }
  const bool hasCollIndex = usingCollCache || !!cf;

  int count = 0;
  for (int i = 0; i < mixedTotal; ++i) {
    IndexRec ir;
    if (usingCache) {
      ir = mixedData[i];
    } else {
      mf.seek(static_cast<uint32_t>(i) * kIndexRecSize);
      if (!readIndexRec(mf, ir)) break;
    }

    bool matches = false;
    if (ir.bookId & 0x80000000u) {
      const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
      if (hasCollIndex) {
        CollectionIndexRec ci;
        if (usingCollCache) {
          ci = collData[collIdx];
        } else {
          cf.seek(static_cast<uint32_t>(collIdx) * sizeof(CollectionIndexRec));
          if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != sizeof(CollectionIndexRec)) {
            continue;
          }
        }
        if (hasSearch) {
          char key[20];
          if (ci.flags & 1) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              makeTitleSortKey(uc->name.c_str(), key);
            } else {
              makeTitleSortKey(ci.collectionName, key);
            }
          } else {
            makeTitleSortKey(ci.collectionName, key);
          }
          matches = substringMatch(key, searchFilter);
        } else {
          matches = true;
        }

        if (matches && filterMode != FilterMode::ALL) {
          if (ci.flags & 1) {
            // User collection: resolve members from UserCollectionsStore
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              bool anyMatch = false;
              for (const auto& m : members) {
                uint32_t offset;
                if (findRecordOffset(m.bookId, offset)) {
                  const Record* recPtr = g_datBuffer.getRecord(offset);
                  if (recPtr && !recPtr->tombstone() && matchesFilter(*recPtr, filterMode)) {
                    anyMatch = true;
                    break;
                  }
                }
              }
              matches = anyMatch;
            } else {
              matches = false;
            }
          } else {
            if (sf) {
              sf.seek(ci.firstSeriesOffset);
              SeriesRec sr;
              bool anyMatch = false;
              for (uint32_t b = 0; b < ci.bookCount; ++b) {
                if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != sizeof(SeriesRec)) break;
                if (sr.bookId == 0) continue;
                uint32_t offset;
                if (findRecordOffset(sr.bookId, offset)) {
                  const Record* recPtr = g_datBuffer.getRecord(offset);
                  if (recPtr && !recPtr->tombstone() && matchesFilter(*recPtr, filterMode)) {
                    anyMatch = true;
                    break;
                  }
                }
              }
              matches = anyMatch;
            }
          }
        }
      }
    } else {
      bool matches = false;
      const Record* recPtr = g_datBuffer.getRecord(ir.recordOffset);
      if (recPtr) {
        matches = matchesFilter(*recPtr, filterMode);
        if (matches && hasSearch) {
          char titleKey[20];
          makeTitleSortKey(recPtr->title, titleKey);
          char authorKey[20];
          makeSortKey(recPtr->author, authorKey);
          matches = substringMatch(titleKey, searchFilter) || substringMatch(authorKey, searchFilter);
        }
      }
    }

    if (matches) ++count;
  }

  if (cf) cf.close();
  if (sf) sf.close();
  if (!usingCache) mf.close();
  return count;
}

// =========================================================================
// Sync
// =========================================================================

// =========================================================================
// Sync — incremental, falls back to scan() if no library.dat
// =========================================================================

bool sync(const char* rootDir) {
  if (!LibraryIndex::exists()) return false;
  // For incremental: we just re-scan.  The scan function compares against
  // scan_state.dat and only processes changed/new/removed files.
  // We don't need buildIndices() if nothing changed (scan returns true
  // but added=0 and removed=0).
  // Actually scan() is not stateless w.r.t. GfxRenderer.  For sync, caller
  // provides a dummy popup or we skip progress.  Let caller handle.
  return true;  // indicates cache is valid
}

// =========================================================================
// Query page
// =========================================================================

// Populate a BookRef from a Record.
// Reading status comes from the lightweight summary path (getHomeBookStatsForRender),
// which never forces a full store load; the store is only materialized when a
// progress/recency sort or a context-menu action actually needs it.
static void recordToBookRef(const Record& rec, BookRef& ref) {
  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  ref.id = rec.id;
  std::strncpy(ref.title, rec.title, 64);
  ref.title[64] = '\0';
  std::strncpy(ref.author, rec.author, 48);
  ref.author[48] = '\0';
  std::strncpy(ref.path, rec.path, 128);
  ref.path[128] = '\0';
  ref.isFavorite = FAVORITES.isFavorite(rec.path);
  const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
  ref.isOpened = s && s->totalReadingMs > 0;
  ref.isCompleted = s && s->completed;
  ref.isHidden = HIDDEN_BOOKS.isHidden(rec.path);
}

// Check if a Record matches the active filter (favourites/recent/etc.)
// Hidden books are always excluded except when explicitly showing hidden only.
// Reading-state filters (UNREAD/COMPLETED) go through the lightweight summary
// path; they never force a full store load.
static bool matchesFilter(const Record& rec, FilterMode m) {
  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  // Hidden books are excluded from all standard views.
  if (m != FilterMode::HIDDEN && HIDDEN_BOOKS.isHidden(rec.path)) {
    LOG_DBG("LIBIDX", "matchesFilter: hidden book excluded: %s", rec.path);
    return false;
  }
  switch (m) {
    case FilterMode::ALL:
      return true;
    case FilterMode::FAVOURITES:
      return FAVORITES.isFavorite(rec.path);
    case FilterMode::LATEST_READ: {
      const auto& recent = RECENT_BOOKS.getBooks();
      for (const auto& rb : recent) {
        if (rb.path == rec.path || (!rb.bookId.empty() && rb.bookId == rec.path)) return true;
      }
      return false;
    }
    case FilterMode::UNREAD: {
      READING_STATS.ensureLoaded();
      const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
      return !s || s->totalReadingMs == 0;
    }
    case FilterMode::COMPLETED: {
      READING_STATS.ensureLoaded();
      const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
      return s && s->completed;
    }
    case FilterMode::HIDDEN:
      return HIDDEN_BOOKS.isHidden(rec.path);
  }
  return true;
}

// Walk an index file sequentially, collecting records that match filter/search.
// Stops after collecting `needed` entries (0 = collect all).
static int walkIndex(const char* idxPath, bool reverse, int skip, int needed, const char* search, FilterMode filter,
                     BookRef* out) {
  HalFile f = Storage.open(idxPath);
  if (!f) {
    LOG_ERR("LIBIDX", "walkIndex: cannot open index %s", idxPath);
    return 0;
  }

  const int total = static_cast<int>(f.size() / kIndexRecSize);
  LOG_DBG("LIBIDX", "walkIndex: idx=%s total=%d reverse=%d skip=%d needed=%d filter=%d", idxPath, total, reverse ? 1 : 0, skip, needed, (int)filter);
  int collected = 0;
  int skipped = 0;
  int tombstoned = 0;
  int filtered = 0;
  int searched = 0;
  IndexRec ir;

  const int start = reverse ? (total - 1) : 0;
  const int end = reverse ? -1 : total;
  const int step = reverse ? -1 : 1;

  HalFile df = Storage.open(kDatFile);
  if (!df) {
    LOG_ERR("LIBIDX", "walkIndex: cannot open %s", kDatFile);
    f.close();
    return 0;
  }

  for (int pos = start; pos != end; pos += step) {
    const uint32_t off = static_cast<uint32_t>(pos) * kIndexRecSize;
    if (!f.seek(off)) {
      LOG_ERR("LIBIDX", "walkIndex: seek failed at pos=%d", pos);
      break;
    }
    if (!readIndexRec(f, ir)) {
      LOG_ERR("LIBIDX", "walkIndex: readIndexRec failed at pos=%d", pos);
      break;
    }

    Record rec;
    if (df && df.seek(ir.recordOffset) &&
        df.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize)) {
      if (rec.tombstone()) {
        ++tombstoned;
        continue;
      }
      if (!matchesFilter(rec, filter)) {
        ++filtered;
        continue;
      }

      if (search && search[0]) {
        if (!substringMatch(rec.title, search) && !substringMatch(rec.author, search)) {
          ++searched;
          continue;
        }
      }

      if (skipped++ < skip) continue;

      recordToBookRef(rec, out[collected]);
      ++collected;
      if (needed > 0 && collected >= needed) break;
    }
  }

  if (df) df.close();
  f.close();
  LOG_DBG("LIBIDX", "walkIndex: collected=%d skipped=%d tombstoned=%d filtered=%d searched=%d", collected, skipped, tombstoned, filtered, searched);
  return collected;
}

// Full-text search without index (scans library.dat directly).
// Full scan with optional search filter and sort
static int scanFullText(BookRef* out, int page, int pageSize, SortMode sortMode, const char* search,
                        FilterMode filter) {
  // O(n) scan of library.dat — used for full-text search AND for
  // RECENT/PROGRESS sorts which can't use alphabetical indices.
  // These sorts compare lastReadAt / progress, which only the full store
  // carries (summary.json has no lastReadAt), so materialize it just here.
  if (sortMode == SortMode::RECENT || sortMode == SortMode::PROGRESS) {
    READING_STATS.ensureLoaded();
  }

  HalFile f = Storage.open(kDatFile);
  if (!f) return 0;

  const int total = static_cast<int>(f.size() / kRecordSize);
  // Store full records so the sort comparator never re-opens the SD card.
  std::vector<Record> matchRecords;
  matchRecords.reserve(std::min(total, 64));

  Record rec;
  for (int rp = 0; rp < total; ++rp) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.tombstone()) continue;
    if (search && search[0]) {
      if (!substringMatch(rec.title, search) && !substringMatch(rec.author, search)) continue;
    }
    if (!matchesFilter(rec, filter)) continue;
    matchRecords.push_back(rec);
  }
  f.close();

  // Sort matches by sortMode — all in RAM, no SD access.
  std::sort(matchRecords.begin(), matchRecords.end(), [sortMode](const Record& ra, const Record& rb) {
    if (sortMode == SortMode::RECENT) {
      const auto* sa = READING_STATS.findBook(ra.path);
      const auto* sb = READING_STATS.findBook(rb.path);
      uint32_t ta = sa ? sa->lastReadAt : 0;
      uint32_t tb = sb ? sb->lastReadAt : 0;
      if (ta != tb) return ta > tb;  // most recent first
      int c = cmpSortKey(ra.title, rb.title);
      return c < 0;
    }
    if (sortMode == SortMode::PROGRESS) {
      const auto* sa = READING_STATS.findBook(ra.path);
      const auto* sb = READING_STATS.findBook(rb.path);
      if (sa && sb && sa->completed != sb->completed) return sb->completed;  // unread first
      uint8_t pa = sa ? sa->lastProgressPercent : 0;
      uint8_t pb = sb ? sb->lastProgressPercent : 0;
      if (pa != pb) return pa > pb;  // highest progress first
      int c = cmpSortKey(ra.title, rb.title);
      return c < 0;
    }
    if (sortMode == SortMode::TITLE_ASC || sortMode == SortMode::TITLE_DESC) {
      int c = cmpSortKey(ra.title, rb.title);
      return (sortMode == SortMode::TITLE_ASC) ? (c < 0) : (c > 0);
    }
    // AUTHOR_ASC or AUTHOR_DESC
    int c = cmpSortKey(ra.author, rb.author);
    if (c != 0) return (sortMode == SortMode::AUTHOR_ASC) ? (c < 0) : (c > 0);
    c = cmpSortKey(ra.title, rb.title);
    return c < 0;
  });

  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(matchRecords.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    if (count >= pageSize) break;
    recordToBookRef(matchRecords[i], out[count++]);
  }
  return count;
}

int queryPage(BookRef* out, int page, int pageSize, SortMode sortMode, const char* searchFilter, FilterMode filterMode,
              int coverWidth, int coverHeight) {
  if (!out || pageSize <= 0) return 0;
  if (!exists()) {
    LOG_DBG("LIBIDX", "queryPage: library.dat missing");
    return 0;
  }

  LOG_DBG("LIBIDX", "queryPage: page=%d pageSize=%d sort=%d filter=%d search=%s", page, pageSize, (int)sortMode, (int)filterMode, searchFilter ? searchFilter : "");

  if (sortMode == SortMode::COLLECTIONS) {
    return queryCollections(out, page, pageSize, coverWidth, coverHeight);
  }

  if (sortMode == SortMode::MIXED) {
    return queryMixed(out, page, pageSize, searchFilter, filterMode, coverWidth, coverHeight, sortMode);
  }

  const bool hasSearch = (searchFilter && searchFilter[0] != '\0');
  const bool needsFullScan = hasSearch || sortMode == SortMode::RECENT || sortMode == SortMode::PROGRESS;
  const bool reverse = (sortMode == SortMode::TITLE_DESC || sortMode == SortMode::AUTHOR_DESC ||
                        sortMode == SortMode::RECENT || sortMode == SortMode::PROGRESS);
  const bool byAuthor = (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC);

  if (needsFullScan) {
    return scanFullText(out, page, pageSize, sortMode, searchFilter, filterMode);
  }

  // Indexed path: walk sorted index sequentially
  const char* idxPath = byAuthor ? kIdxAuthor : kIdxTitle;
  const int skip = page * pageSize;
  const int count = walkIndex(idxPath, reverse, skip, pageSize, nullptr, filterMode, out);
  LOG_DBG("LIBIDX", "queryPage: returning count=%d page=%d pageSize=%d idx=%s", count, page, pageSize, idxPath);
  return count;
}

int totalBooks() {
  HalFile f = Storage.open(kDatFile);
  if (!f) {
    LOG_DBG("LIBIDX", "totalBooks: library.dat missing");
    return 0;
  }
  const int raw = static_cast<int>(f.size() / kRecordSize);
  f.close();
  // Count non-tombstone (rough estimate; caller can refine via totalMatching)
  LOG_DBG("LIBIDX", "totalBooks: rawRecords=%d", raw);
  return raw > 0 ? raw : 0;
}

int totalMatching(const char* searchFilter, FilterMode filterMode) {
  if (!exists()) return 0;
  const bool hasSearch = (searchFilter && searchFilter[0] != '\0');
  if (!hasSearch && filterMode == FilterMode::ALL) {
    // Fast path: count non-tombstone records, but still filter out hidden books.
    // Hidden books must be excluded from ALL views (except explicit HIDDEN filter).
    int count = 0;
    HalFile f = Storage.open(kDatFile);
    if (!f) return 0;
    Record rec;
    while (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize)) {
      if (!rec.tombstone() && !HIDDEN_BOOKS.isHidden(rec.path)) ++count;
    }
    f.close();
    return count;
  }
  // Filtered: we must scan.  Walk index with filter, count.
  int count = 0;
  HalFile f = Storage.open(kIdxTitle);
  if (!f) return 0;
  HalFile df = Storage.open(kDatFile);
  IndexRec ir;
  while (readIndexRec(f, ir)) {
    Record rec;
    if (df && df.seek(ir.recordOffset) &&
        df.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize)) {
      if (rec.tombstone()) continue;
      if (!matchesFilter(rec, filterMode)) continue;
      if (hasSearch) {
        if (!substringMatch(rec.title, searchFilter) && !substringMatch(rec.author, searchFilter)) continue;
      }
      ++count;
    }
  }
  if (df) df.close();
  f.close();
  return count;
}

void invalidate() {
  LOG_DBG("LIB", "invalidate: start");
  Storage.remove(kDatFile);
  Storage.remove(kScanFile);
  Storage.remove(kIdxTitle);
  Storage.remove(kIdxAuthor);
  // Remove only automatic indices; preserve user collections and their associations
  Storage.remove(kIdxMetadataSeries);
  Storage.remove(kIdxFolderCollections);
  Storage.remove(kIdxMixed);
  Storage.remove(kIdxCollections);
  Storage.remove(kSeriesDat);
  // Clean temp merge-sort chunks
  for (int i = 0; i < 9999; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!Storage.exists(tmpPath)) break;
    Storage.remove(tmpPath);
  }
  IndexCacheManager::releaseAll();
  LOG_DBG("LIB", "invalidate: automatic library indices deleted, user collections preserved");
}

bool init() {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kLibDir);
  Storage.mkdir(kTmpDir);
  return true;
}

// ---- User collections API (Steroids extension) ----

int totalUserCollections() {
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.totalCollections();
}

int userCollectionBookCount(const char* collectionId) {
  if (!collectionId || !*collectionId) return 0;
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.memberCount(collectionId);
}

int queryUserCollections(BookRef* out, int page, int pageSize, int coverWidth, int coverHeight) {
  if (!out || pageSize <= 0) return 0;
  USER_COLLECTIONS.ensureLoaded();

  const auto& collections = USER_COLLECTIONS.collections();
  const int total = static_cast<int>(collections.size());
  const int start = page * pageSize;
  if (start >= total) return 0;

  int count = 0;
  for (int i = start; i < total && count < pageSize; ++i) {
    BookRef& ref = out[count];
    ref.id = 0x80000000u | static_cast<uint32_t>(i);
    std::strncpy(ref.title, collections[i].name.c_str(), 64);
    ref.title[64] = '\0';
    std::snprintf(ref.author, sizeof(ref.author), "%d books", USER_COLLECTIONS.memberCount(collections[i].id));
    ref.path[0] = '\0';
    ref.isFavorite = false;
    ref.isOpened = false;
    ref.isCompleted = false;
    ref.isHidden = false;
    ref.isCollection = true;
    ++count;
  }
  return count;
}

int queryBookByBookId(BookRef* out, uint32_t bookId) {
  if (!out || bookId == 0) return 0;
  HalFile f = Storage.open(kDatFile);
  if (!f) return 0;
  const int totalRecs = static_cast<int>(f.size() / kRecordSize);
  for (int rp = 0; rp < totalRecs; ++rp) {
    Record rec;
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.id == bookId && !rec.tombstone()) {
      recordToBookRef(rec, *out);
      f.close();
      return 1;
    }
  }
  f.close();
  return 0;
}

int queryUserCollectionBooks(BookRef* out, int page, int pageSize, const char* collectionId) {
  if (!out || pageSize <= 0 || !collectionId || !*collectionId) return 0;
  USER_COLLECTIONS.ensureLoaded();

  auto members = USER_COLLECTIONS.members(collectionId);
  if (members.empty()) return 0;

  // Sort by position
  std::sort(members.begin(), members.end(),
            [](const CollectionMember& a, const CollectionMember& b) { return a.position < b.position; });

  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(members.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    Record rec;
    if (readRecordByBookId(members[i].bookId, rec)) {
      recordToBookRef(rec, out[count++]);
    }
  }
  return count;
}

bool createUserCollection(const char* name, char* outId, size_t outIdCap) {
  if (!name || !*name || !outId || outIdCap == 0) return false;
  USER_COLLECTIONS.ensureLoaded();
  std::string id;
  if (!USER_COLLECTIONS.createCollection(name, id)) return false;
  std::strncpy(outId, id.c_str(), outIdCap - 1);
  outId[outIdCap - 1] = '\0';
  return true;
}

bool renameUserCollection(const char* collectionId, const char* newName) {
  if (!collectionId || !*collectionId || !newName || !*newName) return false;
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.renameCollection(collectionId, newName);
}

bool deleteUserCollection(const char* collectionId) {
  if (!collectionId || !*collectionId) return false;
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.deleteCollection(collectionId);
}

bool addBookToCollection(const char* collectionId, uint32_t bookId) {
  if (!collectionId || !*collectionId || bookId == 0) return false;
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.addBook(collectionId, bookId, 0.0f);
}

bool removeBookFromCollection(const char* collectionId, uint32_t bookId) {
  if (!collectionId || !*collectionId || bookId == 0) return false;
  USER_COLLECTIONS.ensureLoaded();
  return USER_COLLECTIONS.removeBook(collectionId, bookId);
}

void removeBookFromAllCollections(uint32_t bookId) {
  invalidateBookLookup();
  if (bookId == 0) return;
  USER_COLLECTIONS.ensureLoaded();
  USER_COLLECTIONS.removeBookFromAll(bookId);
}

}  // namespace LibraryIndex