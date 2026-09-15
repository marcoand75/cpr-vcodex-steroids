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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "EpubParser.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "UserCollectionsStore.h"

namespace LibraryIndex {

// =========================================================================
// Constants
// =========================================================================
namespace {
constexpr const char* kLibDir   = "/.crosspoint/LIBRARY";
constexpr const char* kDatFile  = "/.crosspoint/LIBRARY/library.dat";
constexpr const char* kScanFile = "/.crosspoint/LIBRARY/scan_state.dat";
constexpr const char* kIdxTitle  = "/.crosspoint/LIBRARY/idx_title.bin";
constexpr const char* kIdxAuthor = "/.crosspoint/LIBRARY/idx_author.bin";
constexpr const char* kIdxCollections = "/.crosspoint/LIBRARY/idx_collections.bin";
constexpr const char* kIdxMetadataSeries = "/.crosspoint/LIBRARY/idx_metadata_series.bin";
constexpr const char* kIdxFolderCollections = "/.crosspoint/LIBRARY/idx_folder_collections.bin";
constexpr const char* kIdxUserCollections = "/.crosspoint/LIBRARY/idx_user_collections.bin";
constexpr const char* kIdxMixed = "/.crosspoint/LIBRARY/idx_mixed.bin";
constexpr const char* kSeriesDat = "/.crosspoint/LIBRARY/series.dat";
constexpr const char* kTmpDir   = "/.crosspoint/LIBRARY/tmp";

int kProgressInterval = 10;

// ---- Fixed-length record sizes ----
constexpr size_t kRecordSize    = sizeof(Record);        // 256
constexpr size_t kScanRecSize   = 16;                    // path_hash(4)+mtime(4)+size(4)+id(4)
constexpr size_t kIndexRecSize  = 28;                    // key(20)+id(4)+offset(4)

// ---- External merge-sort chunk size (records per chunk) ----
#ifndef LIBIDX_CHUNK_RECS
#define LIBIDX_CHUNK_RECS 16
#endif
constexpr int kChunkRecs = LIBIDX_CHUNK_RECS;

// ---- Search: records per I/O block during full-text scan ----
#ifndef LIBIDX_SEARCH_BLOCK_RECS
#define LIBIDX_SEARCH_BLOCK_RECS 64
#endif
constexpr int kSearchBlockRecs = LIBIDX_SEARCH_BLOCK_RECS;

// ---- Index record (on-disk) ----
struct __attribute__((packed)) IndexRec {
  char     sortKey[20];
  uint32_t bookId;
  uint32_t recordOffset;
};
static_assert(sizeof(IndexRec) == 28, "IndexRec must be 28 bytes");

// ---- Series record (on-disk, one per book) ----
struct __attribute__((packed)) SeriesRec {
  uint32_t bookId;
  char     seriesName[80];
  float    seriesIndex;
  uint8_t  flags;              // bit0 = folderFallback
  uint8_t  reserved[3];
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

// ---- Collection index record (on-disk) ----
struct __attribute__((packed)) CollectionIndexRec {
  char     collectionName[80];
  uint32_t firstSeriesOffset;
  uint32_t bookCount;
  uint8_t  flags;              // bit0 = user-defined collection
  uint8_t  reserved[3];
};
static_assert(sizeof(CollectionIndexRec) == 92, "CollectionIndexRec must be 92 bytes");

// =========================================================================
// Helpers
// =========================================================================

void emitProgress(GfxRenderer& r, const Rect& popup, int done, int total) {
  const int denom = total > 0 ? total : 1;
  int pct = (done * 100) / denom;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  UITheme::getInstance().getTheme().fillPopupProgress(r, popup, pct);
  r.displayBuffer();
}

void emitProgressIdle(GfxRenderer&, const Rect&, int, int) {
  // No-op for incremental scan
}

void makeSortKey(const char* src, char* dst) {
  size_t w = 0;
  for (size_t i = 0; src[i] && w < 20; ++i) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    switch (c) {
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: dst[w++] = 'a'; break;
      case 0xC8: case 0xC9: case 0xCA: case 0xCB: dst[w++] = 'e'; break;
      case 0xCC: case 0xCD: case 0xCE: case 0xCF: dst[w++] = 'i'; break;
      case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: dst[w++] = 'o'; break;
      case 0xD9: case 0xDA: case 0xDB: case 0xDC: dst[w++] = 'u'; break;
      case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: dst[w++] = 'a'; break;
      case 0xE8: case 0xE9: case 0xEA: case 0xEB: dst[w++] = 'e'; break;
      case 0xEC: case 0xED: case 0xEE: case 0xEF: dst[w++] = 'i'; break;
      case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: dst[w++] = 'o'; break;
      case 0xF9: case 0xFA: case 0xFB: case 0xFC: dst[w++] = 'u'; break;
      case 0xD1: case 0xF1: dst[w++] = 'n'; break;
      case 0xC7: case 0xE7: dst[w++] = 'c'; break;
      default: dst[w++] = static_cast<char>(std::tolower(c)); break;
    }
  }
  while (w < 20) dst[w++] = '\0';
}

void makeTitleSortKey(const char* src, char* dst) {
  size_t w = 0;
  for (size_t i = 0; src[i] && w < 20; ++i) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    if (c >= '0' && c <= '9') {
      size_t numStart = i;
      while (src[i] >= '0' && src[i] <= '9') ++i;
      const size_t numLen = i - numStart;
      if (numLen < 4) {
        const size_t pad = static_cast<size_t>(4 - numLen);
        const size_t space = (w + pad > 20) ? (20 - w) : pad;
        for (size_t p = 0; p < space && w < 20; ++p) dst[w++] = '0';
      }
      for (size_t d = 0; d < numLen && w < 20; ++d) {
        dst[w++] = src[numStart + d];
      }
      if (src[i]) --i;
    } else {
      switch (c) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: dst[w++] = 'a'; break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: dst[w++] = 'e'; break;
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: dst[w++] = 'i'; break;
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: dst[w++] = 'o'; break;
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: dst[w++] = 'u'; break;
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: dst[w++] = 'a'; break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: dst[w++] = 'e'; break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF: dst[w++] = 'i'; break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: dst[w++] = 'o'; break;
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: dst[w++] = 'u'; break;
        case 0xD1: case 0xF1: dst[w++] = 'n'; break;
        case 0xC7: case 0xE7: dst[w++] = 'c'; break;
        default: dst[w++] = static_cast<char>(std::tolower(c)); break;
      }
    }
  }
  while (w < 20) dst[w++] = '\0';
}

int cmpSortKey(const char* a, const char* b) {
  return std::strncmp(a, b, 20);
}

static int cmpSortKeyCI(const char* a, const char* b) {
  for (int i = 0; i < 20; ++i) {
    unsigned char ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
    unsigned char cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == 0) return 0;
  }
  return 0;
}

bool substringMatch(const char* haystack, const char* needle) {
  if (!needle || !needle[0]) return true;
  if (!haystack) return false;
  const size_t nlen = std::strlen(needle);
  size_t hs = 0;
  char buf[256];
  size_t bw = 0;
  for (size_t i = 0; haystack[i] && bw < sizeof(buf) - 1; ++i) {
    unsigned char c = static_cast<unsigned char>(haystack[i]);
    switch (c) {
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: buf[bw++] = 'a'; break;
      case 0xC8: case 0xC9: case 0xCA: case 0xCB: buf[bw++] = 'e'; break;
      case 0xCC: case 0xCD: case 0xCE: case 0xCF: buf[bw++] = 'i'; break;
      case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: buf[bw++] = 'o'; break;
      case 0xD9: case 0xDA: case 0xDB: case 0xDC: buf[bw++] = 'u'; break;
      case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: buf[bw++] = 'a'; break;
      case 0xE8: case 0xE9: case 0xEA: case 0xEB: buf[bw++] = 'e'; break;
      case 0xEC: case 0xED: case 0xEE: case 0xEF: buf[bw++] = 'i'; break;
      case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: buf[bw++] = 'o'; break;
      case 0xF9: case 0xFA: case 0xFB: case 0xFC: buf[bw++] = 'u'; break;
      case 0xD1: case 0xF1: buf[bw++] = 'n'; break;
      case 0xC7: case 0xE7: buf[bw++] = 'c'; break;
      default: buf[bw++] = static_cast<char>(std::tolower(c)); break;
    }
  }
  buf[bw] = '\0';
  return std::strstr(buf, needle) != nullptr;
}

// =========================================================================
// Storage helpers
// =========================================================================

bool readRecord(uint32_t pos, Record& rec) {
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const uint32_t offset = pos * kRecordSize;
  if (!f.seek(offset)) { f.close(); return false; }
  const bool ok = (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize));
  f.close();
  return ok;
}

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

uint32_t appendRecord(const Record& rec) {
  Storage.mkdir(kLibDir);
  HalFile f = Storage.open(kDatFile, O_WRONLY);
  if (!f) {
    HalFile tmp = Storage.open(kDatFile, O_CREAT | O_WRONLY);
    if (tmp) tmp.close();
    f = Storage.open(kDatFile, O_WRONLY);
  }
  if (!f) return UINT32_MAX;
  const size_t currentSize = f.size();
  if (!f.seek(currentSize)) { f.close(); return UINT32_MAX; }
  const uint32_t pos = static_cast<uint32_t>(currentSize / kRecordSize);
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) {
    f.close(); return UINT32_MAX;
  }
  f.close();
  return pos;
}

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
  if (!f.seek(currentSize)) { f.close(); return UINT32_MAX; }
  const uint32_t pos = static_cast<uint32_t>(currentSize / sizeof(SeriesRec));
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) {
    f.close(); return UINT32_MAX;
  }
  f.close();
  return pos;
}

bool writeIndexRec(HalFile& f, const IndexRec& rec) {
  return f.write(reinterpret_cast<const uint8_t*>(&rec), kIndexRecSize) == static_cast<int>(kIndexRecSize);
}

bool readIndexRec(HalFile& f, IndexRec& rec) {
  return f.read(reinterpret_cast<uint8_t*>(&rec), kIndexRecSize) == static_cast<int>(kIndexRecSize);
}

static uint32_t hashPath(const char* p) {
  uint32_t h = 5381;
  while (*p) {
    h = ((h << 5) + h) + static_cast<unsigned char>(*p);
    ++p;
  }
  return h;
}

// ---- Book lookup cache ----
struct BookIdOffset {
  uint32_t id;
  uint32_t offset;
};

static std::vector<BookIdOffset> g_bookLookup;
static std::vector<Record> g_bookCache;
static size_t g_bookDatSize = 0;

static std::vector<IndexRec> g_mixedIndexCache;
static std::vector<CollectionIndexRec> g_collectionsIndexCache;
static std::vector<SeriesRec> g_seriesCache;
static size_t g_mixedIndexSize = 0;
static size_t g_collectionsIndexSize = 0;
static size_t g_seriesDatSize = 0;

static void invalidateBookLookup() {
  g_bookLookup.clear();
  g_bookCache.clear();
  g_bookDatSize = 0;
}

static bool buildBookLookup() {
  HalFile dat = Storage.open(kDatFile);
  if (!dat) return false;
  const size_t datSize = static_cast<size_t>(dat.size());
  dat.close();
  if (datSize == g_bookDatSize && !g_bookLookup.empty()) return true;
  g_bookDatSize = datSize;
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const int totalRecs = static_cast<int>(datSize / kRecordSize);
  g_bookLookup.clear();
  g_bookCache.clear();
  g_bookLookup.reserve(totalRecs > 0 ? static_cast<size_t>(totalRecs) : 0);
  g_bookCache.reserve(totalRecs > 0 ? static_cast<size_t>(totalRecs) : 0);

  for (int rp = 0; rp < totalRecs; ++rp) {
    Record rec;
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.id == 0) continue;
    g_bookLookup.push_back({rec.id, static_cast<uint32_t>(rp * kRecordSize)});
    g_bookCache.push_back(rec);
  }
  f.close();

  std::sort(g_bookLookup.begin(), g_bookLookup.end(),
            [](const BookIdOffset& a, const BookIdOffset& b) { return a.id < b.id; });
  std::sort(g_bookCache.begin(), g_bookCache.end(),
            [](const Record& a, const Record& b) { return a.id < b.id; });
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

// Read a full Record by bookId using the bookLookup (binary search + single read)
// Uses the already-built g_bookLookup for O(log n) offset lookup, then one seek+read.
static bool readRecordByBookId(uint32_t bookId, Record& rec) {
  // Ensure book lookup is built
  if (g_bookLookup.empty()) buildBookLookup();
  uint32_t offset;
  if (!findRecordOffset(bookId, offset)) return false;
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  if (!f.seek(offset)) { f.close(); return false; }
  bool ok = (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize));
  f.close();
  return ok && !rec.tombstone();
}

}  // anonymous namespace

// =========================================================================
// Public cache API
// =========================================================================

bool loadMixedIndexCache() {
  HalFile f = Storage.open(kIdxMixed);
  if (!f) return false;
  const size_t fsize = static_cast<size_t>(f.size());
  if (fsize == g_mixedIndexSize && !g_mixedIndexCache.empty()) return true;
  g_mixedIndexSize = fsize;
  const int count = static_cast<int>(fsize / kIndexRecSize);
  g_mixedIndexCache.resize(count);
  if (count > 0) {
    f.read(reinterpret_cast<uint8_t*>(g_mixedIndexCache.data()), count * kIndexRecSize);
  }
  f.close();
  return true;
}

bool loadCollectionsIndexCache() {
  HalFile f = Storage.open(kIdxCollections);
  if (!f) return false;
  const size_t fsize = static_cast<size_t>(f.size());
  if (fsize == g_collectionsIndexSize && !g_collectionsIndexCache.empty()) return true;
  g_collectionsIndexSize = fsize;
  const int count = static_cast<int>(fsize / sizeof(CollectionIndexRec));
  g_collectionsIndexCache.resize(count);
  if (count > 0) {
    f.read(reinterpret_cast<uint8_t*>(g_collectionsIndexCache.data()), count * sizeof(CollectionIndexRec));
  }
  f.close();
  return true;
}

bool loadSeriesCache() {
  HalFile f = Storage.open(kSeriesDat);
  if (!f) return false;
  const size_t fsize = static_cast<size_t>(f.size());
  if (fsize == g_seriesDatSize && !g_seriesCache.empty()) return true;
  g_seriesDatSize = fsize;
  const int count = static_cast<int>(fsize / sizeof(SeriesRec));
  g_seriesCache.resize(count);
  if (count > 0) {
    f.read(reinterpret_cast<uint8_t*>(g_seriesCache.data()), count * sizeof(SeriesRec));
  }
  f.close();
  return true;
}

void invalidateAllCaches() {
  invalidateBookLookup();
  g_mixedIndexCache.clear();
  g_collectionsIndexCache.clear();
  g_seriesCache.clear();
  g_mixedIndexSize = 0;
  g_collectionsIndexSize = 0;
  g_seriesDatSize = 0;
}

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

// =========================================================================
// Exists / thumbPathFor / extractMetadata
// =========================================================================

bool exists() {
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const size_t sz = f.size();
  f.close();
  if (sz < kRecordSize) return false;
  return true;
}

std::string thumbPathFor(const std::string& bookPath, int coverW, int coverH) {
  char buf[96];
  if (FsHelpers::hasXtcExtension(bookPath)) {
    const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
    std::snprintf(buf, sizeof(buf), "/.crosspoint/xtc_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
    std::snprintf(buf, sizeof(buf), "/.crosspoint/txt_%llu/cover.bmp", hash);
  } else {
    const uint64_t hash = ZipFile::fnvHash64(bookPath.c_str(), bookPath.size());
    std::snprintf(buf, sizeof(buf), "/.crosspoint/epub_%llu/thumb_%dx%d_fit.bmp",
                  static_cast<unsigned long long>(hash), coverW, coverH);
  }
  return buf;
}

bool extractMetadata(const char* path, char* title, size_t titleCap, char* author, size_t authorCap) {
  if (!path || path[0] != '/') return false;
  HalFile stat = Storage.open(path);
  if (!stat || stat.isDirectory() || stat.size() == 0) { if (stat) stat.close(); return false; }
  stat.close();

  if (FsHelpers::hasEpubExtension(std::string_view{path})) {
    std::string t, a;
    EpubParser::extractMetadata(path, "/.crosspoint", t, a);
    std::strncpy(title, t.c_str(), titleCap - 1); title[titleCap - 1] = '\0';
    std::strncpy(author, a.c_str(), authorCap - 1); author[authorCap - 1] = '\0';
  } else if (FsHelpers::hasXtcExtension(std::string_view{path})) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      std::strncpy(title, xtc.getTitle().c_str(), titleCap - 1); title[titleCap - 1] = '\0';
      std::strncpy(author, xtc.getAuthor().c_str(), authorCap - 1); author[authorCap - 1] = '\0';
    }
  } else if (FsHelpers::hasTxtExtension(std::string_view{path}) || FsHelpers::hasMarkdownExtension(std::string_view{path})) {
    Txt txt(path, "/.crosspoint");
    if (txt.load()) {
      std::strncpy(title, txt.getTitle().c_str(), titleCap - 1); title[titleCap - 1] = '\0';
      author[0] = '\0';
    }
  }

  if (title[0] == '\0') {
    const char* slash = std::strrchr(path, '/');
    const char* dot   = std::strrchr(path, '.');
    const char* start = slash ? slash + 1 : path;
    const size_t len  = (dot && dot > start) ? static_cast<size_t>(dot - start) : std::strlen(start);
    size_t cp = (len < titleCap - 1) ? len : titleCap - 1;
    std::memcpy(title, start, cp);
    title[cp] = '\0';
  }
  return true;
}

// =========================================================================
// Directory walker
// =========================================================================

namespace {

using FileVisitor = std::function<void(const char* path, size_t fileSize)>;

static void walkDirs(const char* rootDir, const FileVisitor& onFile, bool yieldBetweenDirs = true) {
  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<std::string> worklist; worklist.reserve(16); worklist.emplace_back(root);
  std::vector<uint8_t> depth; depth.push_back(0);
  constexpr int kMaxDepth = 8;
  int dirCount = 0;

  while (!worklist.empty()) {
    std::string folder = std::move(worklist.back()); worklist.pop_back();
    uint8_t fd = depth.back(); depth.pop_back();
    if (yieldBetweenDirs && (++dirCount & 0x7) == 0) { yield(); esp_task_wdt_reset(); }
    HalFile rootFile = Storage.open(folder.c_str());
    if (!rootFile || !rootFile.isDirectory()) { if (rootFile) rootFile.close(); continue; }
    rootFile.rewindDirectory();
    char name[500];
    for (HalFile file = rootFile.openNextFile(); file; file = rootFile.openNextFile()) {
      file.getName(name, sizeof(name));
      bool isDir = file.isDirectory();
      size_t fsz = file.size();
      file.close();
      if (name[0] == '.') continue;
      std::string lower = name;
      for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower == "system volume information" || lower == "my clippings.txt" || lower == "my lookups.txt") continue;
      if (isDir && (lower == "crosspoint" || lower == "library" || lower.compare(0,5,"sleep")==0 ||
                    lower == "font" || lower == "fonts" || lower == "dictionaries" || lower == "exports")) continue;
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
      if (FsHelpers::hasEpubExtension(fn) || FsHelpers::hasXtcExtension(fn) ||
          FsHelpers::hasTxtExtension(fn) || FsHelpers::hasMarkdownExtension(fn)) {
        if (std::strcmp(name, "if_found.txt") != 0 && std::strcmp(name, "crash_report.txt") != 0) {
          onFile(child.c_str(), fsz);
        }
      }
    }
    rootFile.close();
  }
}

}  // anonymous namespace

// =========================================================================
// Scan
// =========================================================================

bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir,
          int* outAdded, int* outRemoved) {
  LOG_DBG("LIB", "Scan: start root=%s", rootDir ? rootDir : "/");
  invalidateBookLookup();
  Storage.mkdir("/.crosspoint"); Storage.mkdir(kLibDir); Storage.mkdir(kTmpDir);

  struct ScanEntry { uint32_t hash; uint32_t mtime; uint32_t size; uint32_t id; };
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
  LOG_DBG("LIB", "Scan: loaded %u previous scan entries", (unsigned)prevScan.size());

  std::sort(prevScan.begin(), prevScan.end(),
            [](const ScanEntry& a, const ScanEntry& b) { return a.hash < b.hash; });

  int total = 0;
  const bool incremental = (popupRect.x == 0 && popupRect.y == 0);
  auto doEmit = incremental ? emitProgressIdle : emitProgress;

  if (!incremental) {
    walkDirs(rootDir, [&total](const char*, size_t) { ++total; }, false);
    LOG_DBG("LIB", "Scan: %d candidate files found", total);
    emitProgress(renderer, popupRect, 0, total);
    if (total > 10) kProgressInterval = std::max(1, total / 10);
  }

  struct ScanState { int interval; } state = { kProgressInterval };

  std::vector<ScanRec> newScan;
  newScan.reserve(total > 0 ? total : prevScan.size());

  {
    HalFile datFile = Storage.open(kDatFile);
    if (!datFile || prevScan.empty()) {
      if (datFile) datFile.close();
      HalFile tmp = Storage.open(kDatFile, O_CREAT | O_WRONLY | O_TRUNC);
      if (tmp) tmp.close();
      datFile = Storage.open(kDatFile);
    }
    if (!datFile) { LOG_ERR("LIB", "Scan: cannot open library.dat"); return false; }
    datFile.close();
  }

  uint32_t nextId = 1;
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
    yield(); esp_task_wdt_reset();
    if (pi % state.interval == 0) doEmit(renderer, popupRect, pi, total);
    ++pi;

    if (fsz == 0) { ++skipped; return; }
    const uint32_t mtime = (uint32_t)fsz;
    const uint32_t ph = hashPath(p);

    const ScanEntry* prev = nullptr;
    {
      auto lo = prevScan.begin();
      auto hi = prevScan.end();
      ScanEntry key{ph, 0, 0, 0};
      auto it = std::lower_bound(lo, hi, key,
                                 [](const ScanEntry& a, const ScanEntry& b) { return a.hash < b.hash; });
      if (it != hi && it->hash == ph) prev = &(*it);
    }

    if (prev && prev->mtime == mtime && prev->size == (uint32_t)fsz) {
      newScan.push_back({ph, mtime, (uint32_t)fsz, prev->id});
      ++skipped;
      return;
    }

    Record rec = {};
    rec.id = prev ? prev->id : nextId++;
    rec.file_size = (uint32_t)fsz;
    rec.mtime = mtime;
    std::strncpy(rec.path, p, sizeof(rec.path) - 1);
    rec.path[sizeof(rec.path)-1] = '\0';

    char title[65] = {}, author[49] = {}, series[81] = {};
    float seriesIndex = 0.0f;
    bool seriesFromFolder = false;

    if (FsHelpers::hasEpubExtension(std::string_view{p})) {
      std::string epTitle, epAuthor, epSeries;
      float epSeriesIdx = 0.0f;
      EpubParser::extractMetadata(p, "/.crosspoint", epTitle, epAuthor, &epSeries, &epSeriesIdx);
      std::strncpy(title, epTitle.c_str(), sizeof(title)-1); title[sizeof(title)-1] = '\0';
      std::strncpy(author, epAuthor.c_str(), sizeof(author)-1); author[sizeof(author)-1] = '\0';
      std::strncpy(series, epSeries.c_str(), sizeof(series)-1); series[sizeof(series)-1] = '\0';
      seriesIndex = epSeriesIdx;
    } else {
      extractMetadata(p, title, sizeof(title), author, sizeof(author));
    }

    if (series[0] == '\0' && SETTINGS.libraryFolderCollections) {
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
    std::strncpy(rec.title, title, sizeof(rec.title)-1); rec.title[sizeof(rec.title)-1] = '\0';
    std::strncpy(rec.author, author, sizeof(rec.author)-1); rec.author[sizeof(rec.author)-1] = '\0';

    if (prev) {
      Record old;
      if (readRecord(prev->id > 0 ? (prev->id - 1) : 0, old) && old.id == prev->id) {
        rec.flags = old.flags;
        rec.setTombstone(false);
      }
    }

    uint32_t pos = appendRecord(rec);
    if (pos == UINT32_MAX) { LOG_ERR("LIB", "Scan: append failed for %s", p); return; }
    newScan.push_back({ph, mtime, (uint32_t)fsz, rec.id});

    if (series[0] != '\0') {
      if (!seriesFromFolder && !SETTINGS.libraryMetadataSeries) {
        // skip
      } else if (seriesFromFolder && !SETTINGS.libraryFolderCollections) {
        // skip
      } else {
        SeriesRec sr = {};
        sr.bookId = rec.id;
        std::strncpy(sr.seriesName, series, sizeof(sr.seriesName)-1);
        sr.seriesName[sizeof(sr.seriesName)-1] = '\0';
        sr.seriesIndex = seriesIndex;
        sr.flags = seriesFromFolder ? 1 : 0;
        appendSeriesRec(sr);
      }
    }

    ++added;
  };

  walkDirs(rootDir, processFile, !incremental);

  // mark removed
  for (auto& old : prevScan) {
    bool found = false;
    for (auto& ns : newScan) {
      if (ns.bookId == old.id) { found = true; break; }
    }
    if (!found) {
      Record rec;
      for (uint32_t rp = 0; ; ++rp) {
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
          USER_COLLECTIONS.ensureLoaded();
          removeBookFromAllCollections(old.id);
          break;
        }
      }
    }
  }

  // write new scan_state.dat
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
  LOG_DBG("LIB", "Scan: added=%d skipped=%d removed=%d total=%d", added, skipped, removed, added+skipped);
  LOG_DBG("LIB", "Scan: done added=%d skipped=%d removed=%d newScan=%u", added, skipped, removed, (unsigned)newScan.size());
  return true;
}

// =========================================================================
// Build indices
// =========================================================================

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
  return 0;
}

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
  void close() { if (file) file.close(); }
};

static bool buildIndexFile(const char* outPath, int (*cmp)(const void*, const void*),
                           bool useAuthorKey) {
  HalFile dat = Storage.open(kDatFile);
  if (!dat) return false;
  const int totalRecs = static_cast<int>(dat.size() / kRecordSize);
  if (totalRecs == 0) { dat.close(); return false; }
  dat.close();

  int chunkCount = 0;
  {
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
          key[0] = 'z'; key[1] = 'z'; key[2] = 'z'; key[3] = '\0';
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
  }
  LOG_DBG("LIB", "IdxBuild: %d chunks written for %s", chunkCount, outPath);

  if (chunkCount == 0) return false;

  HalFile outF = Storage.open(outPath, O_CREAT | O_WRONLY | O_TRUNC);
  if (!outF) return false;

  std::vector<ChunkReader> readers(chunkCount);
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!readers[i].open(tmpPath)) {
      LOG_ERR("LIB", "IdxBuild: cannot open chunk %s", tmpPath);
    }
  }

  while (true) {
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

  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    Storage.remove(tmpPath);
  }

  LOG_DBG("LIB", "IdxBuild: merge complete for %s", outPath);
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
  if (!buildIndexFile(kIdxAuthor, cmpByAuthor, true)) {
    LOG_ERR("LIB", "BuildIndices: author index failed");
    return false;
  }
  if (!buildMixedIndex(SortMode::TITLE_ASC)) {
    LOG_ERR("LIB", "BuildIndices: mixed index failed");
    return false;
  }

  LOG_DBG("LIB", "BuildIndices: done in %lu ms", millis() - t0);
  return true;
}

// ---- Collections index builder ----

static void recordToBookRef(const Record& rec, BookRef& ref);  // fwd decl

bool buildCollectionsIndex() {
  LOG_DBG("LIB", "BuildCollIdx: start");
  invalidateBookLookup();
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) return false;

  const size_t seriesFileSize = sf.size();
  sf.close();
  if (seriesFileSize > 0 && seriesFileSize % sizeof(SeriesRec) != 0) {
    LOG_DBG("LIB", "BuildCollIdx: old series.dat format detected, removing for rebuild");
    Storage.remove(kSeriesDat);
    return false;
  }

  const int totalSeries = static_cast<int>(seriesFileSize / sizeof(SeriesRec));

  std::unordered_map<uint32_t, std::string> bookIdToPath;
  {
    HalFile datF = Storage.open(kDatFile);
    if (datF) {
      Record rec;
      while (datF.read(reinterpret_cast<uint8_t*>(&rec), sizeof(Record)) == static_cast<int>(sizeof(Record))) {
        if (!rec.tombstone()) {
          bookIdToPath[rec.id] = rec.path;
        }
      }
      datF.close();
    }
  }

  std::vector<SeriesRec> series;
  series.reserve(totalSeries);
  {
    HalFile f = Storage.open(kSeriesDat);
    SeriesRec sr;
    while (f.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == static_cast<int>(sizeof(SeriesRec))) {
      if (sr.bookId == 0) continue;
      auto pathIt = bookIdToPath.find(sr.bookId);
      if (pathIt == bookIdToPath.end()) continue;
      series.push_back(sr);
    }
    f.close();
  }

  std::sort(series.begin(), series.end(), [](const SeriesRec& a, const SeriesRec& b) {
    int c = cmpSortKey(a.seriesName, b.seriesName);
    if (c != 0) return c < 0;
    return a.seriesIndex < b.seriesIndex;
  });

  {
    HalFile f = Storage.open(kSeriesDat, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f) return false;
    for (const auto& sr : series) {
      f.write(reinterpret_cast<const uint8_t*>(&sr), sizeof(SeriesRec));
    }
    f.close();
  }

  std::vector<CollectionIndexRec> collections;
  {
    size_t i = 0;
    while (i < series.size()) {
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
          const char* p = pathIt->second.c_str();
          const char* lastSlash = strrchr(p, '/');
          if (lastSlash && lastSlash > p) {
            folderPath.assign(p, static_cast<size_t>(lastSlash - p));
          }
          groupKey = folderPath.c_str();
        }
      }

      CollectionIndexRec ci;
      std::strncpy(ci.collectionName, series[i].seriesName, sizeof(ci.collectionName)-1);
      ci.collectionName[sizeof(ci.collectionName)-1] = '\0';
      ci.firstSeriesOffset = static_cast<uint32_t>(i * sizeof(SeriesRec));
      ci.bookCount = 0;
      ci.flags = 0;

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
          if (cmpSortKey(series[j].seriesName, series[i].seriesName) != 0) break;
        }
        ++ci.bookCount;
        ++j;
      }
      collections.push_back(ci);
      i = j;
    }
  }

  std::vector<CollectionIndexRec> userCollections;
  {
    const std::string jsonPath = "/.crosspoint/user_collections.json";
    if (Storage.exists(jsonPath.c_str())) {
      const String json = Storage.readFile(jsonPath.c_str());
      if (!json.isEmpty()) {
        const char* p = json.c_str();
        const char* collectionsStart = strstr(p, "\"collections\"");
        if (collectionsStart) {
          const char* arrStart = strchr(collectionsStart, '[');
          if (arrStart) {
            const char* scan = arrStart + 1;
            while (*scan && *scan != ']') {
              const char* idKey = strstr(scan, "\"id\"");
              if (!idKey) break;
              const char* colon = strchr(idKey, ':');
              if (!colon) break;
              const char* valStart = strchr(colon, '"');
              if (!valStart) break;
              const char* valEnd = strchr(valStart + 1, '"');
              if (!valEnd) break;

              CollectionIndexRec ci;
              const size_t copyLen = std::min<size_t>(sizeof(ci.collectionName) - 1, static_cast<size_t>(valEnd - valStart - 1));
              std::strncpy(ci.collectionName, valStart + 1, copyLen);
              ci.collectionName[copyLen] = '\0';
              ci.firstSeriesOffset = 0;
              ci.bookCount = 0;
              ci.flags = 1;
              userCollections.push_back(ci);

              scan = valEnd + 1;
            }
          }
        }

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
  std::sort(userCollections.begin(), userCollections.end(), [&](const CollectionIndexRec& a, const CollectionIndexRec& b) {
    return cmpSortKey(sortKeyFor(a), sortKeyFor(b)) < 0;
  });

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
        merged.push_back(collections[autoIdx++]);
        merged.push_back(userCollections[userIdx++]);
      }
    }
  }

  std::vector<CollectionIndexRec> metadataSeries;
  std::vector<CollectionIndexRec> folderCollections;
  for (const auto& ci : collections) {
    HalFile sf2 = Storage.open(kSeriesDat);
    if (sf2) {
      sf2.seek(ci.firstSeriesOffset);
      SeriesRec sr;
      if (sf2.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == sizeof(SeriesRec)) {
        if ((sr.flags & 1) != 0) {
          folderCollections.push_back(ci);
        } else {
          metadataSeries.push_back(ci);
        }
      }
      sf2.close();
    } else {
      metadataSeries.push_back(ci);
    }
  }

  if (!metadataSeries.empty() && SETTINGS.libraryMetadataSeries) {
    HalFile outF = Storage.open(kIdxMetadataSeries, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : metadataSeries) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
    }
  } else {
    Storage.remove(kIdxMetadataSeries);
  }

  if (!folderCollections.empty() && SETTINGS.libraryFolderCollections) {
    HalFile outF = Storage.open(kIdxFolderCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : folderCollections) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
    }
  } else {
    Storage.remove(kIdxFolderCollections);
  }

  if (!userCollections.empty()) {
    HalFile outF = Storage.open(kIdxUserCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : userCollections) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
    }
  } else {
    Storage.remove(kIdxUserCollections);
  }

  if (!merged.empty()) {
    HalFile outF = Storage.open(kIdxCollections, O_CREAT | O_WRONLY | O_TRUNC);
    if (outF) {
      for (const auto& ci : merged) {
        outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
      }
      outF.close();
    }
  } else {
    Storage.remove(kIdxCollections);
  }

  LOG_DBG("LIB", "BuildCollIdx: %d metadata + %d folder + %d user = %d total entries",
          (int)metadataSeries.size(), (int)folderCollections.size(), (int)userCollections.size(), (int)merged.size());
  return true;
}

// =========================================================================
// Mixed index builder
// =========================================================================

bool buildMixedIndex(SortMode sortMode) {
  LOG_DBG("LIB", "BuildMixedIdx: start");
  const unsigned long t0 = millis();

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

    USER_COLLECTIONS.ensureLoaded();
    const auto& members = USER_COLLECTIONS.allMembers();
    collectionBookIds.reserve(collectionBookIds.size() + members.size());
    for (const auto& m : members) {
      if (m.bookId > 0) collectionBookIds.push_back(m.bookId);
    }
    std::sort(collectionBookIds.begin(), collectionBookIds.end());
  }

  int chunkCount = 0;
  {
    HalFile dat = Storage.open(kDatFile);
    if (!dat) return false;
    const int totalRecs = static_cast<int>(dat.size() / kRecordSize);
    if (totalRecs == 0) { dat.close(); return false; }

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

  {
    std::vector<CollectionIndexRec> allCollections;

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
      std::unordered_map<std::string, int> combinedIndexMap;
      {
        HalFile cf = Storage.open(kIdxCollections);
        if (cf) {
          const int total = static_cast<int>(cf.size() / sizeof(CollectionIndexRec));
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

        char key[82];
        std::snprintf(key, sizeof(key), "%s|%d", ci.collectionName, ci.flags);
        auto it = combinedIndexMap.find(key);
        int combinedIdx = (it != combinedIndexMap.end()) ? it->second : idx;
        ir.bookId = 0x80000000u | static_cast<uint32_t>(combinedIdx);

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
        ir.recordOffset = foundFirst ? static_cast<uint32_t>((firstRec.id > 0 ? (firstRec.id - 1) : 0) * kRecordSize) : 0xFFFFFFFFu;
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

  HalFile outF = Storage.open(kIdxMixed, O_CREAT | O_WRONLY | O_TRUNC);
  if (!outF) return false;

  std::vector<ChunkReader> readers(chunkCount);
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    readers[i].open(tmpPath);
  }

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
  }

  outF.close();
  for (int i = 0; i < chunkCount; ++i) readers[i].close();

  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    Storage.remove(tmpPath);
  }

  LOG_DBG("LIB", "BuildMixedIdx: done in %lu ms, %d chunks", millis() - t0, chunkCount);
  return true;
}

// =========================================================================
// Query helpers
// =========================================================================

static void recordToBookRef(const Record& rec, BookRef& ref) {
  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  ref.id = rec.id;
  std::strncpy(ref.title, rec.title, 64); ref.title[64] = '\0';
  std::strncpy(ref.author, rec.author, 48); ref.author[48] = '\0';
  std::strncpy(ref.path, rec.path, 128); ref.path[128] = '\0';
  ref.isFavorite  = FAVORITES.isFavorite(rec.path);
  const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
  ref.isOpened    = s && s->totalReadingMs > 0;
  ref.isCompleted = s && s->completed;
  ref.isHidden    = HIDDEN_BOOKS.isHidden(rec.path);
  ref.isCollection = false;
}

static bool matchesFilter(const Record& rec, LibraryIndex::FilterMode m) {
  HIDDEN_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  if (m != LibraryIndex::FilterMode::HIDDEN && HIDDEN_BOOKS.isHidden(rec.path)) {
    return false;
  }
  switch (m) {
    case LibraryIndex::FilterMode::ALL: return true;
    case LibraryIndex::FilterMode::FAVOURITES: return FAVORITES.isFavorite(rec.path);
    case LibraryIndex::FilterMode::LATEST_READ: {
      const auto& recent = RECENT_BOOKS.getBooks();
      for (const auto& rb : recent) {
        if (rb.path == rec.path || (!rb.bookId.empty() && rb.bookId == rec.path)) return true;
      }
      return false;
    }
    case LibraryIndex::FilterMode::UNREAD: {
      const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
      return !s || s->totalReadingMs == 0;
    }
    case LibraryIndex::FilterMode::COMPLETED: {
      const auto* s = READING_STATS.getHomeBookStatsForRender("", rec.path);
      return s && s->completed;
    }
    case LibraryIndex::FilterMode::HIDDEN: return HIDDEN_BOOKS.isHidden(rec.path);
  }
  return true;
}

// =========================================================================
// Collections query
// =========================================================================

int queryCollections(BookRef* out, int page, int pageSize, int coverWidth, int coverHeight) {
  if (!loadCollectionsIndexCache()) return 0;
  if (!loadSeriesCache()) return 0;

  const int total = static_cast<int>(g_collectionsIndexCache.size());
  const int start = page * pageSize;
  if (start >= total) return 0;

  int count = 0;
  for (int i = start; i < total && count < pageSize; ++i) {
    const CollectionIndexRec& ci = g_collectionsIndexCache[i];
    BookRef& ref = out[count];
    ref.id = 0x80000000u | static_cast<uint32_t>(i);

    std::string displayName = ci.collectionName;
    if ((ci.flags & 1) != 0) {
      USER_COLLECTIONS.ensureLoaded();
      const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
      if (uc) displayName = uc->name;
    }

    std::string subtitle;
    if ((ci.flags & 1) == 0 && ci.firstSeriesOffset < g_seriesCache.size() * sizeof(SeriesRec)) {
      for (uint32_t b = 0; b < ci.bookCount; ++b) {
        const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
        if (seriesIdx >= g_seriesCache.size()) break;
        const SeriesRec& sr = g_seriesCache[seriesIdx];
        if (sr.bookId == 0) continue;
        if ((sr.flags & 1) != 0) {
          Record rec;
          if (readRecordByBookId(sr.bookId, rec)) {
            const char* p = rec.path;
            const char* lastSlash = strrchr(p, '/');
            if (lastSlash && lastSlash > p) {
              const char* prevSlash = lastSlash;
              while (prevSlash > p && *(prevSlash - 1) != '/') --prevSlash;
              if (prevSlash > p) {
                subtitle.assign(prevSlash, static_cast<size_t>(lastSlash - prevSlash));
              }
            }
          }
          if (!subtitle.empty()) break;
        }
      }
    }
    if (!subtitle.empty()) {
      char combined[80];
      std::snprintf(combined, sizeof(combined), "%s / %s", displayName.c_str(), subtitle.c_str());
      std::strncpy(ref.title, combined, 64); ref.title[64] = '\0';
    } else {
      std::strncpy(ref.title, displayName.c_str(), 64); ref.title[64] = '\0';
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

    if (coverWidth > 0 && coverHeight > 0) {
      if (ci.flags & 1) {
        USER_COLLECTIONS.ensureLoaded();
        const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
        if (uc) {
          auto members = USER_COLLECTIONS.members(uc->id);
          for (const auto& m : members) {
            Record rec;
            if (readRecordByBookId(m.bookId, rec)) {
              const std::string bookPath(rec.path);
              const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
              if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                std::strncpy(ref.path, rec.path, sizeof(ref.path) - 1);
                ref.path[sizeof(ref.path) - 1] = '\0';
                break;
              }
            }
          }
        }
      } else {
        for (uint32_t b = 0; b < ci.bookCount; ++b) {
          const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
          if (seriesIdx >= g_seriesCache.size()) break;
          const SeriesRec& sr = g_seriesCache[seriesIdx];
          if (sr.bookId == 0) continue;
          Record rec;
          if (readRecordByBookId(sr.bookId, rec)) {
            const std::string bookPath(rec.path);
            const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
            if (!thumb.empty() && Storage.exists(thumb.c_str())) {
              std::strncpy(ref.path, rec.path, sizeof(ref.path) - 1);
              ref.path[sizeof(ref.path) - 1] = '\0';
              break;
            }
          }
        }
      }
    }

    ++count;
  }

  return count;
}

int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx) {
  if (!loadCollectionsIndexCache()) return 0;
  if (!loadSeriesCache()) return 0;

  if (collectionIdx < 0 || collectionIdx >= static_cast<int>(g_collectionsIndexCache.size())) return 0;
  const CollectionIndexRec& ci = g_collectionsIndexCache[collectionIdx];

  if (ci.flags & 1) {
    USER_COLLECTIONS.ensureLoaded();
    const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
    if (!uc) return 0;
    auto members = USER_COLLECTIONS.members(uc->id);
    std::sort(members.begin(), members.end(), [](const CollectionMember& a, const CollectionMember& b) {
      return a.position < b.position;
    });
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

  if (ci.firstSeriesOffset >= g_seriesCache.size() * sizeof(SeriesRec)) return 0;

  struct BookSlot {
    uint32_t bookId;
    float seriesIndex;
  };
  std::vector<BookSlot> slots;
  for (uint32_t i = 0; i < ci.bookCount; ++i) {
    const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + i;
    if (seriesIdx >= g_seriesCache.size()) break;
    const SeriesRec& sr = g_seriesCache[seriesIdx];
    if (sr.bookId == 0) continue;
    slots.push_back({sr.bookId, sr.seriesIndex});
  }

  std::sort(slots.begin(), slots.end(), [](const BookSlot& a, const BookSlot& b) {
    return a.seriesIndex < b.seriesIndex;
  });

  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(slots.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    Record rec;
    if (readRecordByBookId(slots[i].bookId, rec)) {
      recordToBookRef(rec, out[count++]);
    }
  }
  return count;
}

int totalCollections() {
  if (!loadCollectionsIndexCache()) return 0;
  return static_cast<int>(g_collectionsIndexCache.size());
}

int collectionBookCount(int collectionIdx) {
  if (!loadCollectionsIndexCache()) return 0;
  if (collectionIdx < 0 || collectionIdx >= static_cast<int>(g_collectionsIndexCache.size())) return 0;
  const CollectionIndexRec& ci = g_collectionsIndexCache[collectionIdx];

  if (ci.flags & 1) {
    USER_COLLECTIONS.ensureLoaded();
    const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
    if (uc) {
      return USER_COLLECTIONS.memberCount(uc->id);
    }
    return 0;
  }
  return static_cast<int>(ci.bookCount);
}

// Load all index caches safely - one at a time with heap checks
// Returns true if all essential caches loaded, false if OOM
static bool ensureIndexCachesLoaded() {
  // Load mixed index first (largest, ~28KB for 1000 books)
  if (!loadMixedIndexCache()) return false;
  // Then collections index (~5KB)
  if (!loadCollectionsIndexCache()) return false;
  // Then series cache (~18KB)
  if (!loadSeriesCache()) return false;
  return true;
}

// =========================================================================
// Mixed view query
// =========================================================================

int queryMixed(BookRef* out, int page, int pageSize, const char* searchFilter, FilterMode filterMode,
               int coverWidth, int coverHeight, SortMode sortMode) {
  if (!ensureIndexCachesLoaded()) return 0;

  const int total = static_cast<int>(g_mixedIndexCache.size());

  std::vector<BookRef> matches;
  matches.reserve(std::min(total, 512));

  for (int i = 0; i < total; ++i) {
    const IndexRec& ir = g_mixedIndexCache[i];

    bool isMatch = true;
    if (ir.bookId & 0x80000000u) {
      const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
      if (collIdx >= 0 && collIdx < static_cast<int>(g_collectionsIndexCache.size())) {
        const CollectionIndexRec& ci = g_collectionsIndexCache[collIdx];

        if (searchFilter && searchFilter[0] != '\0') {
          char key[20];
          makeTitleSortKey(ci.collectionName, key);
          isMatch = substringMatch(key, searchFilter);
        }

        if (isMatch && filterMode != FilterMode::ALL) {
          if (ci.flags & 1) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              bool any = false;
              for (const auto& m : members) {
                Record rec;
                if (readRecordByBookId(m.bookId, rec) && matchesFilter(rec, filterMode)) {
                  any = true;
                  break;
                }
              }
              isMatch = any;
            } else {
              isMatch = false;
            }
          } else {
            bool anyMatch = false;
            for (uint32_t b = 0; b < ci.bookCount; ++b) {
              const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
              if (seriesIdx >= g_seriesCache.size()) break;
              const SeriesRec& sr = g_seriesCache[seriesIdx];
              if (sr.bookId == 0) continue;
              Record rec;
              if (readRecordByBookId(sr.bookId, rec) && matchesFilter(rec, filterMode)) {
                anyMatch = true;
                break;
              }
            }
            isMatch = anyMatch;
          }
        }
      } else {
        isMatch = false;
      }
    } else {
      Record rec;
      if (readRecordByBookId(ir.bookId, rec)) {
        isMatch = matchesFilter(rec, filterMode);
        if (isMatch && searchFilter && searchFilter[0] != '\0') {
          char titleKey[20]; makeTitleSortKey(rec.title, titleKey);
          char authorKey[20]; makeSortKey(rec.author, authorKey);
          isMatch = substringMatch(titleKey, searchFilter) || substringMatch(authorKey, searchFilter);
        }
      } else {
        isMatch = false;
      }
    }

    if (!isMatch) continue;

    BookRef ref;
    if (ir.bookId & 0x80000000u) {
      const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
      if (collIdx >= 0 && collIdx < static_cast<int>(g_collectionsIndexCache.size())) {
        const CollectionIndexRec& ci = g_collectionsIndexCache[collIdx];
        ref.id = ir.bookId;

        std::string displayName = ci.collectionName;
        if ((ci.flags & 1) != 0) {
          USER_COLLECTIONS.ensureLoaded();
          const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
          if (uc) displayName = uc->name;
        }

        std::string subtitle;
        if ((ci.flags & 1) == 0) {
          for (uint32_t b = 0; b < ci.bookCount; ++b) {
            const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
            if (seriesIdx >= g_seriesCache.size()) break;
            const SeriesRec& sr = g_seriesCache[seriesIdx];
            if (sr.bookId == 0) continue;
            if ((sr.flags & 1) != 0) {
              Record rec;
              if (readRecordByBookId(sr.bookId, rec)) {
                const char* p = rec.path;
                const char* lastSlash = strrchr(p, '/');
                if (lastSlash && lastSlash > p) {
                  const char* prevSlash = lastSlash;
                  while (prevSlash > p && *(prevSlash - 1) != '/') --prevSlash;
                  if (prevSlash > p) {
                    subtitle.assign(prevSlash, static_cast<size_t>(lastSlash - prevSlash));
                  }
                }
              }
              if (!subtitle.empty()) break;
            }
          }
        }
        if (!subtitle.empty()) {
          char combined[80];
          std::snprintf(combined, sizeof(combined), "%s / %s", displayName.c_str(), subtitle.c_str());
          std::strncpy(ref.title, combined, 64); ref.title[64] = '\0';
        } else {
          std::strncpy(ref.title, displayName.c_str(), 64); ref.title[64] = '\0';
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

        if (coverWidth > 0 && coverHeight > 0) {
          if (ci.flags & 1) {
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              for (const auto& m : members) {
                Record rec;
                if (readRecordByBookId(m.bookId, rec)) {
                  const std::string bookPath(rec.path);
                  const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
                  if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                    std::strncpy(ref.path, rec.path, sizeof(ref.path) - 1);
                    ref.path[sizeof(ref.path) - 1] = '\0';
                    break;
                  }
                }
              }
            }
          } else {
            for (uint32_t b = 0; b < ci.bookCount; ++b) {
              const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
              if (seriesIdx >= g_seriesCache.size()) break;
              const SeriesRec& sr = g_seriesCache[seriesIdx];
              if (sr.bookId == 0) continue;
              Record rec;
              if (readRecordByBookId(sr.bookId, rec)) {
                const std::string bookPath(rec.path);
                const std::string thumb = thumbPathFor(bookPath, coverWidth, coverHeight);
                if (!thumb.empty() && Storage.exists(thumb.c_str())) {
                  std::strncpy(ref.path, rec.path, sizeof(ref.path) - 1);
                  ref.path[sizeof(ref.path) - 1] = '\0';
                  break;
                }
              }
            }
          }
        }
      }
    } else {
      Record rec;
      if (readRecordByBookId(ir.bookId, rec)) {
        recordToBookRef(rec, ref);
      } else {
        continue;
      }
    }

    matches.push_back(ref);
  }

  const bool reverse = (sortMode == SortMode::TITLE_DESC || sortMode == SortMode::AUTHOR_DESC);
  if (sortMode == SortMode::TITLE_ASC || sortMode == SortMode::TITLE_DESC) {
    std::sort(matches.begin(), matches.end(), [reverse](const BookRef& a, const BookRef& b) {
      int c = cmpSortKeyCI(a.title, b.title);
      return reverse ? c > 0 : c < 0;
    });
  } else if (sortMode == SortMode::AUTHOR_ASC || sortMode == SortMode::AUTHOR_DESC) {
    std::sort(matches.begin(), matches.end(), [reverse](const BookRef& a, const BookRef& b) {
      const char* authorA = a.isCollection ? a.title : a.author;
      const char* authorB = b.isCollection ? b.title : b.author;
      int c = cmpSortKeyCI(authorA, authorB);
      if (c != 0) return reverse ? c > 0 : c < 0;
      int c2 = cmpSortKeyCI(a.title, b.title);
      return reverse ? c2 > 0 : c2 < 0;
    });
  } else if (sortMode == SortMode::RECENT || sortMode == SortMode::PROGRESS) {
    READING_STATS.ensureLoaded();
    std::sort(matches.begin(), matches.end(), [sortMode](const BookRef& a, const BookRef& b) {
      const auto* sa = READING_STATS.findBook(a.path);
      const auto* sb = READING_STATS.findBook(b.path);
      if (sortMode == SortMode::RECENT) {
        uint32_t ta = sa ? sa->lastReadAt : 0;
        uint32_t tb = sb ? sb->lastReadAt : 0;
        if (ta != tb) return ta > tb;
      } else {
        bool ca = sa ? sa->completed : false;
        bool cb = sb ? sb->completed : false;
        if (ca != cb) return cb;
        uint8_t pa = sa ? sa->lastProgressPercent : 0;
        uint8_t pb = sb ? sb->lastProgressPercent : 0;
        if (pa != pb) return pa > pb;
      }
      int c = cmpSortKeyCI(a.title, b.title);
      return c < 0;
    });
  }

  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(matches.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    if (count >= pageSize) break;
    out[count++] = matches[i];
  }
  return count;
}

int totalMixed() {
  if (!loadMixedIndexCache()) return 0;
  return static_cast<int>(g_mixedIndexCache.size());
}

int totalMixedMatching(const char* searchFilter, FilterMode filterMode) {
  if (!ensureIndexCachesLoaded()) return 0;

  const bool hasSearch = (searchFilter && searchFilter[0] != '\0');
  if (!hasSearch && filterMode == FilterMode::ALL) {
    return totalMixed();
  }

  int count = 0;
  const int total = static_cast<int>(g_mixedIndexCache.size());
  for (int i = 0; i < total; ++i) {
    const IndexRec& ir = g_mixedIndexCache[i];

    bool matches = false;
    if (ir.bookId & 0x80000000u) {
      const int collIdx = static_cast<int>(ir.bookId & 0x7FFFFFFFu);
      if (collIdx >= 0 && collIdx < static_cast<int>(g_collectionsIndexCache.size())) {
        const CollectionIndexRec& ci = g_collectionsIndexCache[collIdx];
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
            USER_COLLECTIONS.ensureLoaded();
            const UserCollection* uc = USER_COLLECTIONS.findCollection(ci.collectionName);
            if (uc) {
              auto members = USER_COLLECTIONS.members(uc->id);
              bool any = false;
              for (const auto& m : members) {
                Record rec;
                if (readRecordByBookId(m.bookId, rec) && matchesFilter(rec, filterMode)) {
                  any = true;
                  break;
                }
              }
              matches = any;
            } else {
              matches = false;
            }
          } else {
            bool anyMatch = false;
            for (uint32_t b = 0; b < ci.bookCount; ++b) {
              const uint32_t seriesIdx = ci.firstSeriesOffset / sizeof(SeriesRec) + b;
              if (seriesIdx >= g_seriesCache.size()) break;
              const SeriesRec& sr = g_seriesCache[seriesIdx];
              if (sr.bookId == 0) continue;
              Record rec;
              if (readRecordByBookId(sr.bookId, rec) && matchesFilter(rec, filterMode)) {
                anyMatch = true;
                break;
              }
            }
            matches = anyMatch;
          }
        }
      }
    } else {
      Record rec;
      if (readRecordByBookId(ir.bookId, rec)) {
        matches = matchesFilter(rec, filterMode);
        if (matches && hasSearch) {
          char titleKey[20]; makeTitleSortKey(rec.title, titleKey);
          char authorKey[20]; makeSortKey(rec.author, authorKey);
          matches = substringMatch(titleKey, searchFilter) || substringMatch(authorKey, searchFilter);
        }
      }
    }

    if (matches) ++count;
  }

  return count;
}

// =========================================================================
// Sync / Query page / totals
// =========================================================================

bool sync(const char* rootDir) {
  if (!LibraryIndex::exists()) return false;
  return true;
}

static int walkIndex(const char* idxPath, bool reverse, int skip, int needed,
                     const char* search, FilterMode filter, BookRef* out) {
  HalFile f = Storage.open(idxPath);
  if (!f) return 0;

  const int total = static_cast<int>(f.size() / kIndexRecSize);
  int collected = 0;
  int skipped = 0;
  IndexRec ir;

  const int start = reverse ? (total - 1) : 0;
  const int end   = reverse ? -1 : total;
  const int step  = reverse ? -1 : 1;

  for (int pos = start; pos != end; pos += step) {
    const uint32_t off = static_cast<uint32_t>(pos) * kIndexRecSize;
    if (!f.seek(off)) break;
    if (!readIndexRec(f, ir)) break;

    Record rec;
    if (!readRecord(ir.recordOffset / kRecordSize, rec)) continue;
    if (rec.tombstone()) continue;
    if (!matchesFilter(rec, filter)) continue;  // note: uses CompactRecord overload via conversion? wait - fixed below

    if (search && search[0]) {
      if (!substringMatch(rec.title, search) && !substringMatch(rec.author, search)) continue;
    }

    if (skipped++ < skip) continue;

    recordToBookRef(rec, out[collected]);
    ++collected;
    if (needed > 0 && collected >= needed) break;
  }

  f.close();
  return collected;
}

static int scanFullText(LibraryIndex::BookRef* out, int page, int pageSize, LibraryIndex::SortMode sortMode,
                        const char* search, LibraryIndex::FilterMode filter) {
  if (sortMode == LibraryIndex::SortMode::RECENT || sortMode == LibraryIndex::SortMode::PROGRESS) {
    READING_STATS.ensureLoaded();
  }

  HalFile f = Storage.open(kDatFile);
  if (!f) return 0;

  const int total = static_cast<int>(f.size() / kRecordSize);
  std::vector<uint32_t> matchOffsets;
  matchOffsets.reserve(64);

  Record rec;
  for (int rp = 0; rp < total; ++rp) {
    if (!f.seek(static_cast<uint32_t>(rp) * kRecordSize)) break;
    if (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) break;
    if (rec.tombstone()) continue;
    if (search && search[0]) {
      if (!substringMatch(rec.title, search) && !substringMatch(rec.author, search)) continue;
    }
    if (!matchesFilter(rec, filter)) continue;
    matchOffsets.push_back(static_cast<uint32_t>(rp));
  }
  f.close();

  std::sort(matchOffsets.begin(), matchOffsets.end(), [sortMode](uint32_t aOff, uint32_t bOff) {
    Record ra, rb;
    if (!readRecord(aOff, ra) || !readRecord(bOff, rb)) return aOff < bOff;

    if (sortMode == LibraryIndex::SortMode::RECENT) {
      const auto* sa = READING_STATS.findBook(ra.path);
      const auto* sb = READING_STATS.findBook(rb.path);
      uint32_t ta = sa ? sa->lastReadAt : 0;
      uint32_t tb = sb ? sb->lastReadAt : 0;
      if (ta != tb) return ta > tb;
      int c = cmpSortKey(ra.title, rb.title);
      return c < 0;
    }
    if (sortMode == LibraryIndex::SortMode::PROGRESS) {
      const auto* sa = READING_STATS.findBook(ra.path);
      const auto* sb = READING_STATS.findBook(rb.path);
      if (sa && sb && sa->completed != sb->completed) return sb->completed;
      uint8_t pa = sa ? sa->lastProgressPercent : 0;
      uint8_t pb = sb ? sb->lastProgressPercent : 0;
      if (pa != pb) return pa > pb;
      int c = cmpSortKey(ra.title, rb.title);
      return c < 0;
    }
    if (sortMode == SortMode::TITLE_ASC || sortMode == SortMode::TITLE_DESC) {
      int c = cmpSortKey(ra.title, rb.title);
      return (sortMode == SortMode::TITLE_ASC) ? (c < 0) : (c > 0);
    }
    int c = cmpSortKey(ra.author, rb.author);
    if (c != 0) return (sortMode == SortMode::AUTHOR_ASC) ? (c < 0) : (c > 0);
    c = cmpSortKey(ra.title, rb.title);
    return c < 0;
  });

  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(matchOffsets.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    if (count >= pageSize) break;
    Record r;
    if (!readRecord(matchOffsets[i], r)) continue;
    recordToBookRef(r, out[count++]);
  }
  return count;
}

int queryPage(BookRef* out, int page, int pageSize, SortMode sortMode,
              const char* searchFilter, FilterMode filterMode, int coverWidth, int coverHeight) {
  if (!out || pageSize <= 0) return 0;
  if (!exists()) return 0;

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

  const char* idxPath = byAuthor ? kIdxAuthor : kIdxTitle;
  const int skip = page * pageSize;
  return walkIndex(idxPath, reverse, skip, pageSize, nullptr, filterMode, out);
}

int totalBooks() {
  HalFile f = Storage.open(kDatFile);
  if (!f) return 0;
  const int raw = static_cast<int>(f.size() / kRecordSize);
  f.close();
  return raw > 0 ? raw : 0;
}

int totalMatching(const char* searchFilter, FilterMode filterMode) {
  if (!exists()) return 0;
  const bool hasSearch = (searchFilter && searchFilter[0] != '\0');
  if (!hasSearch && filterMode == FilterMode::ALL) {
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
  int count = 0;
  HalFile f = Storage.open(kIdxTitle);
  if (!f) return 0;
  IndexRec ir;
  while (readIndexRec(f, ir)) {
    Record rec;
    if (!readRecord(ir.recordOffset / kRecordSize, rec)) continue;
    if (rec.tombstone()) continue;
    if (!matchesFilter(rec, filterMode)) continue;
    if (hasSearch) {
      if (!substringMatch(rec.title, searchFilter) && !substringMatch(rec.author, searchFilter)) continue;
    }
    ++count;
  }
  f.close();
  return count;
}

void invalidate() {
  Storage.remove(kDatFile);
  Storage.remove(kScanFile);
  Storage.remove(kIdxTitle);
  Storage.remove(kIdxAuthor);
  Storage.remove(kIdxMetadataSeries);
  Storage.remove(kIdxFolderCollections);
  Storage.remove(kIdxMixed);
  Storage.remove(kSeriesDat);
  for (int i = 0; i < 9999; ++i) {
    char tmpPath[96];
    std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!Storage.exists(tmpPath)) break;
    Storage.remove(tmpPath);
  }
  LOG_DBG("LIB", "invalidate: automatic library indices deleted, user collections preserved");
}

bool init() {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kLibDir);
  Storage.mkdir(kTmpDir);
  return true;
}

bool loadIndexCache() {
  if (!exists()) return false;
  bool ok = true;
  ok = loadMixedIndexCache() && ok;
  ok = loadCollectionsIndexCache() && ok;
  ok = loadSeriesCache() && ok;
  return ok;
}

// =========================================================================
// User collections API
// =========================================================================

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
    std::strncpy(ref.title, collections[i].name.c_str(), 64); ref.title[64] = '\0';
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

  std::sort(members.begin(), members.end(), [](const CollectionMember& a, const CollectionMember& b) {
    return a.position < b.position;
  });

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