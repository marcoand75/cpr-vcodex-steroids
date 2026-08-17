#include "LibraryIndex.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HiddenBooksStore.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

#include "components/UITheme.h"
#include "EpubParser.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"

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
constexpr const char* kSeriesDat = "/.crosspoint/LIBRARY/series.dat";
constexpr const char* kTmpDir   = "/.crosspoint/LIBRARY/tmp";

int kProgressInterval = 10;

// ---- Fixed-length record sizes ----
constexpr size_t kRecordSize    = sizeof(Record);        // 256
constexpr size_t kScanRecSize   = 16;                    // path_hash(4)+mtime(4)+size(4)+id(4)
constexpr size_t kIndexRecSize  = 28;                    // key(20)+id(4)+offset(4)

// ---- External merge-sort chunk size (records per chunk) ----
// 4 KB / 256 = 16 records.  Adjust via build flag if needed.
#ifndef LIBIDX_CHUNK_RECS
#define LIBIDX_CHUNK_RECS 16
#endif
constexpr int kChunkRecs = LIBIDX_CHUNK_RECS;           // records per temp chunk

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
  char     seriesName[80];     // collection/series name
  float    seriesIndex;        // position in series
};
static_assert(sizeof(SeriesRec) == 88, "SeriesRec must be 88 bytes");

// ---- Scan state record (on-disk) ----
struct __attribute__((packed)) ScanRec {
  uint32_t pathHash;
  uint32_t mtime;
  uint32_t fileSize;
  uint32_t bookId;
};
static_assert(sizeof(ScanRec) == 16, "ScanRec must be 16 bytes");

// =========================================================================
// Helpers
// =========================================================================

void emitProgress(GfxRenderer& r, const Rect& popup, int done, int total) {
  const int denom = total > 0 ? total : 1;
  int pct = (done * 100) / denom;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
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

// Compare two sort keys (memcmp-like)
int cmpSortKey(const char* a, const char* b) {
  return std::strncmp(a, b, 20);
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
// Storage helpers (file I/O at fixed record granularity)
// =========================================================================

// Read one Record by its zero-based position in library.dat.
// Returns true on success.  RAM: 256 bytes stack.
bool readRecord(uint32_t pos, Record& rec) {
  HalFile f = Storage.open(kDatFile);
  if (!f) return false;
  const uint32_t offset = pos * kRecordSize;
  if (!f.seek(offset)) { f.close(); return false; }
  const bool ok = (f.read(reinterpret_cast<uint8_t*>(&rec), kRecordSize) == static_cast<int>(kRecordSize));
  f.close();
  return ok;
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
  if (!f.seek(currentSize)) { f.close(); return UINT32_MAX; }
  const uint32_t pos = static_cast<uint32_t>(currentSize / kRecordSize);
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize) != static_cast<int>(kRecordSize)) {
    f.close(); return UINT32_MAX;
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
  if (!f.seek(currentSize)) { f.close(); return UINT32_MAX; }
  const uint32_t pos = static_cast<uint32_t>(currentSize / sizeof(SeriesRec));
  if (f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) {
    f.close(); return UINT32_MAX;
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
  if (!f) return false;
  const size_t sz = f.size();
  f.close();
  if (sz < kRecordSize) return false;
  return true;
}

// =========================================================================
// thumbPathFor (legacy, delegate to same logic)
// =========================================================================

std::string thumbPathFor(const std::string& bookPath, int coverW, int coverH) {
  const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
  char buf[96];
  if (FsHelpers::hasXtcExtension(bookPath)) {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/xtc_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/txt_%llu/cover.bmp", hash);
  } else {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/epub_%llu/thumb_%dx%d_fit.bmp", hash, coverW, coverH);
  }
  return buf;
}

// =========================================================================
// Metadata extraction (adapted from LibraryCache)
// =========================================================================

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
      size_t fsz = file.size();  // capture before close
      file.close();
      if (name[0] == '.') continue;
      std::string lower = name; for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower == "system volume information" || lower == "my clippings.txt" || lower == "my lookups.txt") continue;
      if (isDir && (lower == "crosspoint" || lower == "library" || lower.compare(0,5,"sleep")==0 ||
                    lower == "font" || lower == "fonts" || lower == "dictionaries" || lower == "exports")) continue;
      std::string child = folder; if (child.back() != '/') child.push_back('/'); child.append(name);
      if (isDir) {
        if (fd + 1 >= kMaxDepth) continue;
        worklist.push_back(std::move(child)); depth.push_back(static_cast<uint8_t>(fd + 1));
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

static uint32_t hashPath(const char* p) {
  uint32_t h = 5381; while (*p) { h = ((h << 5) + h) + static_cast<unsigned char>(*p); ++p; }
  return h;
}

}  // namespace

bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir,
          int* outAdded, int* outRemoved) {
  LOG_DBG("LIB", "Scan: start root=%s", rootDir ? rootDir : "/");
  Storage.mkdir("/.crosspoint"); Storage.mkdir(kLibDir); Storage.mkdir(kTmpDir);

  // ---- Phase 1: load existing scan_state.dat into RAM map ----
  struct ScanEntry { uint32_t hash; uint32_t mtime; uint32_t size; uint32_t id; };
  std::vector<ScanEntry> prevScan;
  {
    HalFile sf = Storage.open(kScanFile);
    if (sf) {
      const size_t fsz = sf.size();
      prevScan.reserve(fsz / kScanRecSize + 1);
      ScanRec r; while (sf.read(reinterpret_cast<uint8_t*>(&r), kScanRecSize) == static_cast<int>(kScanRecSize)) {
        prevScan.push_back({r.pathHash, r.mtime, r.fileSize, r.bookId});
      }
      sf.close();
    }
  }
  LOG_DBG("LIB", "Scan: loaded %u previous scan entries", prevScan.size());

  // Build hash→index map for O(1) lookup
  // Sort prevScan by hash for O(log n) binary search — zero extra RAM,
  // avoids ~96 KB std::unordered_map overhead at 3000 books.
  std::sort(prevScan.begin(), prevScan.end(),
            [](const ScanEntry& a, const ScanEntry& b) { return a.hash < b.hash; });
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
    emitProgress(renderer, popupRect, 0, total);
    // Dynamic progress interval: ~10 refreshes total regardless of library size
    if (total > 10) kProgressInterval = std::max(1, total / 10);
  }

  struct ScanState { int interval; } state = { kProgressInterval };

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
    if (!datFile) { LOG_ERR("LIB", "Scan: cannot open library.dat"); return false; }
    datFile.close();
  }

  uint32_t nextId = 1;
  // Find max id from existing records
  {
    HalFile datFile = Storage.open(kDatFile);
    if (datFile) {
      const size_t existing = datFile.size() / kRecordSize;
      datFile.close();
      Record last; if (existing > 0 && readRecord(static_cast<uint32_t>(existing - 1), last)) {
        nextId = last.id + 1;
      }
    }
  }

  int added = 0, skipped = 0, removed = 0, pi = 0;

  auto processFile = [&](const char* p, size_t fsz) {
    yield(); esp_task_wdt_reset();
    if (pi % state.interval == 0) doEmit(renderer, popupRect, pi, total);
    ++pi;

    // File size from directory entry — no extra Storage.open() needed
    if (fsz == 0) { ++skipped; return; }
    // mtime: use file size as proxy (ESP32 VFS doesn't expose mtime reliably via Arduino)
    const uint32_t mtime = (uint32_t)fsz;

    const uint32_t ph = hashPath(p);
    // Binary search in sorted prevScan for O(log n) lookup (zero extra RAM)
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
    rec.path[sizeof(rec.path)-1] = '\0';

    char title[65] = {}, author[49] = {}, series[81] = {};
    float seriesIndex = 0.0f;
    // For EPUBs, extract series/collection info via the fast ZIP parser.
    // Other formats (XTC, TXT) don't have series metadata.
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
    std::strncpy(rec.title, title, sizeof(rec.title)-1); rec.title[sizeof(rec.title)-1] = '\0';
    std::strncpy(rec.author, author, sizeof(rec.author)-1); rec.author[sizeof(rec.author)-1] = '\0';

    // Restore flags from previous record
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

    // Write series entry if book has series/collection metadata
    if (series[0] != '\0') {
      SeriesRec sr = {};
      sr.bookId = rec.id;
      std::strncpy(sr.seriesName, series, sizeof(sr.seriesName)-1);
      sr.seriesName[sizeof(sr.seriesName)-1] = '\0';
      sr.seriesIndex = seriesIndex;
      appendSeriesRec(sr);
    }

    ++added;
  };

  walkDirs(rootDir, processFile, !incremental);  // only yield when showing progress

  // ---- Phase 4: mark removed files (present in old scan but not new) ----
  for (auto& old : prevScan) {
    bool found = false;
    for (auto& ns : newScan) { if (ns.bookId == old.id) { found = true; break; } }
    if (!found) {
      Record rec;
      for (uint32_t rp = 0; ; ++rp) {
        if (!readRecord(rp, rec)) break;
        if (rec.id == old.id && !rec.tombstone()) {
          rec.setTombstone(true);
          HalFile f = Storage.open(kDatFile, O_WRONLY);
          if (f) { f.seek(rp * kRecordSize); f.write(reinterpret_cast<const uint8_t*>(&rec), kRecordSize); f.close(); }
          ++removed;
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
  LOG_DBG("LIB", "Scan: added=%d skipped=%d removed=%d total=%d", added, skipped, removed, added+skipped);
  LOG_DBG("LIB", "Scan: done added=%d skipped=%d removed=%d newScan=%u", added, skipped, removed, newScan.size());
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
  bool advance() { if (eof) return false; eof = !readIndexRec(file, cur); return !eof; }
  void close() { if (file) file.close(); }
};

static bool buildIndexFile(const char* outPath, int (*cmp)(const void*, const void*),
                           bool useAuthorKey) {
  // Phase 1: read library.dat in chunks, sort each chunk, write chunk_*.tmp
  HalFile dat = Storage.open(kDatFile);
  if (!dat) return false;
  const int totalRecs = static_cast<int>(dat.size() / kRecordSize);
  if (totalRecs == 0) { dat.close(); return false; }
  dat.close();

  int chunkCount = 0;
  {
    HalFile df = Storage.open(kDatFile);
    Record rec;
    std::vector<IndexRec> chunk; chunk.reserve(kChunkRecs);
    for (int rp = 0; rp < totalRecs; ++rp) {
      if (!readRecord(static_cast<uint32_t>(rp), rec)) continue;
      if (rec.tombstone()) continue;
      IndexRec ir;
      if (useAuthorKey) {
        char key[65]; std::strncpy(key, rec.author, 64); key[64] = '\0';
        if (key[0] == '\0') { key[0] = 'z'; key[1] = 'z'; key[2] = 'z'; key[3] = '\0'; }
        makeSortKey(key, ir.sortKey);
      } else {
        makeSortKey(rec.title, ir.sortKey);
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

  // ... rest of merge remains the same ...
  for (int i = 0; i < chunkCount; ++i) {
    char tmpPath[96]; std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!readers[i].open(tmpPath)) { LOG_ERR("LIB", "IdxBuild: cannot open chunk %s", tmpPath); }
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
    char tmpPath[96]; std::snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    Storage.remove(tmpPath);
  }

  LOG_DBG("LIB", "IdxBuild: merge complete for %s", outPath);
  return true;
}

bool buildIndices() {
  LOG_DBG("LIB", "BuildIndices: start");
  const unsigned long t0 = millis();

  if (!buildIndexFile(kIdxTitle, cmpByTitle, false)) {
    LOG_ERR("LIB", "BuildIndices: title index failed");
    return false;
  }
  if (!buildIndexFile(kIdxAuthor, cmpByAuthor, true)) {
    LOG_ERR("LIB", "BuildIndices: author index failed");
    return false;
  }

  LOG_DBG("LIB", "BuildIndices: done in %lu ms", millis() - t0);
  return true;
}

// ---- Collections index builder ----
// Builds idx_collections.bin from series.dat — one entry per unique collection,
// sorted alphabetically.  The index stores: collection name (key), first record
// offset in series.dat (where books of this collection start).
// The series.dat is sorted by collection name during the Build process.

static void recordToBookRef(const Record& rec, BookRef& ref);  // fwd decl

struct __attribute__((packed)) CollectionIndexRec {
  char     collectionName[80];
  uint32_t firstSeriesOffset;  // byte offset into series.dat where this collection starts
  uint32_t bookCount;          // number of books in this collection
};
static_assert(sizeof(CollectionIndexRec) == 88, "CollectionIndexRec must be 88 bytes");

bool buildCollectionsIndex() {
  LOG_DBG("LIB", "BuildCollIdx: start");
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) return false;
  const int totalSeries = static_cast<int>(sf.size() / sizeof(SeriesRec));
  if (totalSeries == 0) { sf.close(); return false; }
  sf.close();

  // Read all series records, validate against library.dat (skip tombstoned books)
  std::vector<SeriesRec> series;
  series.reserve(totalSeries);
  {
    HalFile f = Storage.open(kSeriesDat);
    HalFile datF = Storage.open(kDatFile);
    SeriesRec sr;
    while (f.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) == static_cast<int>(sizeof(SeriesRec))) {
      // Verify the book still exists and isn't tombstoned
      if (datF) {
        Record rec;
        bool found = false;
        datF.seek(0);
        while (datF.read(reinterpret_cast<uint8_t*>(&rec), sizeof(Record)) == static_cast<int>(sizeof(Record))) {
          if (rec.id == sr.bookId && !rec.tombstone()) { found = true; break; }
        }
        if (!found) continue;  // skip deleted/tombstoned books
      }
      series.push_back(sr);
    }
    f.close();
    if (datF) datF.close();
  }

  std::sort(series.begin(), series.end(), [](const SeriesRec& a, const SeriesRec& b) {
    int c = cmpSortKey(a.seriesName, b.seriesName);
    if (c != 0) return c < 0;
    return a.seriesIndex < b.seriesIndex;
  });

  // Write sorted series.dat
  {
    HalFile f = Storage.open(kSeriesDat, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f) return false;
    for (const auto& sr : series) {
      f.write(reinterpret_cast<const uint8_t*>(&sr), sizeof(SeriesRec));
    }
    f.close();
  }

  // Build collections index: collect unique collection names
  std::vector<CollectionIndexRec> collections;
  for (size_t i = 0; i < series.size();) {
    CollectionIndexRec ci;
    std::strncpy(ci.collectionName, series[i].seriesName, sizeof(ci.collectionName)-1);
    ci.collectionName[sizeof(ci.collectionName)-1] = '\0';
    ci.firstSeriesOffset = static_cast<uint32_t>(i * sizeof(SeriesRec));
    ci.bookCount = 0;
    size_t j = i;
    while (j < series.size() && strncmp(series[j].seriesName, ci.collectionName, sizeof(ci.collectionName)) == 0) {
      ++ci.bookCount;
      ++j;
    }
    collections.push_back(ci);
    i = j;
  }

  // Write idx_collections.bin
  HalFile outF = Storage.open(kIdxCollections, O_CREAT | O_WRONLY | O_TRUNC);
  if (!outF) return false;
  for (const auto& ci : collections) {
    outF.write(reinterpret_cast<const uint8_t*>(&ci), sizeof(CollectionIndexRec));
  }
  outF.close();

  LOG_DBG("LIB", "BuildCollIdx: %d collections from %d series entries", collections.size(), totalSeries);
  return true;
}

// ---- Collections query ----

int queryCollections(BookRef* out, int page, int pageSize) {
  HalFile f = Storage.open(kIdxCollections);
  if (!f) return 0;
  const int total = static_cast<int>(f.size() / sizeof(CollectionIndexRec));
  const int start = page * pageSize;
  if (start >= total) { f.close(); return 0; }
  f.seek(static_cast<uint32_t>(start) * sizeof(CollectionIndexRec));
  CollectionIndexRec ci;
  int count = 0;
  for (int i = start; i < total && count < pageSize; ++i) {
    if (f.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != static_cast<int>(sizeof(CollectionIndexRec))) break;
    BookRef& ref = out[count];
    ref.id = static_cast<uint32_t>(i);  // use index as id for collection picking
    std::strncpy(ref.title, ci.collectionName, 64); ref.title[64] = '\0';
    // Show bookCount in author field as "N books"
    snprintf(ref.author, sizeof(ref.author), "%d books", ci.bookCount);
    std::strncpy(ref.path, kSeriesDat, 128); ref.path[128] = '\0';
    ref.isFavorite = false;
    ref.isOpened = false;
    ref.isCompleted = false;
    ++count;
  }
  f.close();
  return count;
}

int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx) {
  HalFile cf = Storage.open(kIdxCollections);
  if (!cf) return 0;
  const int totalColls = static_cast<int>(cf.size() / sizeof(CollectionIndexRec));
  if (collectionIdx < 0 || collectionIdx >= totalColls) { cf.close(); return 0; }
  cf.seek(static_cast<uint32_t>(collectionIdx) * sizeof(CollectionIndexRec));
  CollectionIndexRec ci;
  if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != static_cast<int>(sizeof(CollectionIndexRec))) {
    cf.close(); return 0;
  }
  cf.close();

  // Read series entries for this collection
  HalFile sf = Storage.open(kSeriesDat);
  if (!sf) return 0;
  sf.seek(ci.firstSeriesOffset);

  std::vector<uint32_t> bookIds;
  SeriesRec sr;
  for (uint32_t i = 0; i < ci.bookCount; ++i) {
    if (sf.read(reinterpret_cast<uint8_t*>(&sr), sizeof(SeriesRec)) != static_cast<int>(sizeof(SeriesRec))) break;
    // Find Record by bookId in library.dat
    if (sr.bookId > 0) {
      // Scan library.dat for this bookId (linear, but collections are small)
      HalFile df = Storage.open(kDatFile);
      if (df) {
        Record rec;
        uint32_t rp = 0;
        while (df.read(reinterpret_cast<uint8_t*>(&rec), sizeof(Record)) == static_cast<int>(sizeof(Record))) {
          if (rec.id == sr.bookId && !rec.tombstone()) {
            bookIds.push_back(rp);
            break;
          }
          ++rp;
        }
        df.close();
      }
    }
  }
  sf.close();

  // Paginate
  const int start = page * pageSize;
  const int end = std::min(start + pageSize, static_cast<int>(bookIds.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    Record rec;
    if (!readRecord(bookIds[i], rec)) continue;
    recordToBookRef(rec, out[count++]);
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
  if (collectionIdx < 0 || collectionIdx >= totalColls) { cf.close(); return 0; }
  cf.seek(static_cast<uint32_t>(collectionIdx) * sizeof(CollectionIndexRec));
  CollectionIndexRec ci;
  if (cf.read(reinterpret_cast<uint8_t*>(&ci), sizeof(CollectionIndexRec)) != static_cast<int>(sizeof(CollectionIndexRec))) {
    cf.close(); return 0;
  }
  cf.close();
  return static_cast<int>(ci.bookCount);
}

// =========================================================================
// Sync
// =========================================================================

// =========================================================================
// Sync — incremental, falls back to scan() if no library.dat
// =========================================================================

bool sync(const char* rootDir) {
  if (!LibraryIndex::exists()) return false;  // caller must do full scan
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
static void recordToBookRef(const Record& rec, BookRef& ref) {
  ref.id = rec.id;
  std::strncpy(ref.title, rec.title, 64); ref.title[64] = '\0';
  std::strncpy(ref.author, rec.author, 48); ref.author[48] = '\0';
  std::strncpy(ref.path, rec.path, 128); ref.path[128] = '\0';
  ref.isFavorite  = FAVORITES.isFavorite(rec.path);
  const auto* s = READING_STATS.findBook(rec.path);
  ref.isOpened    = s && s->totalReadingMs > 0;
  ref.isCompleted = s && s->completed;
  ref.isHidden    = HIDDEN_BOOKS.isHidden(rec.path);
}

// Check if a Record matches the active filter (favourites/recent/etc.)
// Hidden books are always excluded except when explicitly showing hidden only.
static bool matchesFilter(const Record& rec, FilterMode m) {
  // Hidden books are excluded from all standard views.
  if (m != FilterMode::HIDDEN && HIDDEN_BOOKS.isHidden(rec.path)) {
    LOG_DBG("LIBIDX", "matchesFilter: hidden book excluded: %s", rec.path);
    return false;
  }
  switch (m) {
    case FilterMode::ALL: return true;
    case FilterMode::FAVOURITES: return FAVORITES.isFavorite(rec.path);
    case FilterMode::LATEST_READ: {
      const auto& recent = RECENT_BOOKS.getBooks();
      for (const auto& rb : recent) {
        if (rb.path == rec.path || (!rb.bookId.empty() && rb.bookId == rec.path)) return true;
      }
      return false;
    }
    case FilterMode::UNREAD: {
      const auto* s = READING_STATS.findBook(rec.path);
      return !s || s->totalReadingMs == 0;
    }
    case FilterMode::COMPLETED: {
      const auto* s = READING_STATS.findBook(rec.path);
      return s && s->completed;
    }
    case FilterMode::HIDDEN: return HIDDEN_BOOKS.isHidden(rec.path);
  }
  return true;
}

// Walk an index file sequentially, collecting records that match filter/search.
// Stops after collecting `needed` entries (0 = collect all).
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
    if (!matchesFilter(rec, filter)) continue;

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

// Full-text search without index (scans library.dat directly).
// Full scan with optional search filter and sort
static int scanFullText(BookRef* out, int page, int pageSize, SortMode sortMode,
                        const char* search, FilterMode filter) {
  // O(n) scan of library.dat — used for full-text search AND for
  // RECENT/PROGRESS sorts which can't use alphabetical indices.
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

  // Sort matches by sortMode
  std::sort(matchOffsets.begin(), matchOffsets.end(), [sortMode](uint32_t aOff, uint32_t bOff) {
    Record ra, rb;
    if (!readRecord(aOff, ra) || !readRecord(bOff, rb)) return aOff < bOff;

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
  const int end = std::min(start + pageSize, static_cast<int>(matchOffsets.size()));
  int count = 0;
  for (int i = start; i < end; ++i) {
    if (count >= pageSize) break;
    Record r; if (!readRecord(matchOffsets[i], r)) continue;
    recordToBookRef(r, out[count++]);
  }
  return count;
}

int queryPage(BookRef* out, int page, int pageSize, SortMode sortMode,
              const char* searchFilter, FilterMode filterMode) {
  if (!out || pageSize <= 0) return 0;
  if (!exists()) return 0;

  if (sortMode == SortMode::COLLECTIONS) {
    return queryCollections(out, page, pageSize);
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
  return walkIndex(idxPath, reverse, skip, pageSize, nullptr, filterMode, out);
}

int totalBooks() {
  HalFile f = Storage.open(kDatFile);
  if (!f) return 0;
  const int raw = static_cast<int>(f.size() / kRecordSize);
  f.close();
  // Count non-tombstone (rough estimate; caller can refine via totalMatching)
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
  Storage.remove(kIdxCollections);
  Storage.remove(kSeriesDat);
  // Clean temp merge-sort chunks
  for (int i = 0; i < 9999; ++i) {
    char tmpPath[96];
    snprintf(tmpPath, sizeof(tmpPath), "%s/chunk_%04d.tmp", kTmpDir, i);
    if (!Storage.exists(tmpPath)) break;
    Storage.remove(tmpPath);
  }
  LOG_DBG("LIB", "invalidate: all library files deleted");
}

bool init() {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kLibDir);
  Storage.mkdir(kTmpDir);
  return true;
}

}  // namespace LibraryIndex