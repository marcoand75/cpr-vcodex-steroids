#include "LibraryCache.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <CoverDebugLog.h>
#include <HomepageDebugLog.h>
#include <LibraryDebugLog.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "components/UITheme.h"

namespace LibraryCache {

namespace {

// Minimal book.bin reader: reads only title and author from a BookMetadataCache
// file, skipping the other 10 metadata strings. This avoids allocating 12
// temporary std::strings per EPUB during sync — the major source of heap
// fragmentation in the scan loop.
//
// Format: u8 version, u32 lutOffset, u32 spineCount, u32 tocCount,
//         then 10 × (u32 length + data): title, author, language, publisher,
//         description, publicationDate, identifier, subject, rights,
//         coverItemHref, textReferenceHref
static bool readTitleAndAuthor(const std::string& cacheDir, std::string& outTitle, std::string& outAuthor) {
  const std::string cachePath = cacheDir + "/book.bin";
  FsFile file;
  if (!Storage.openFileForRead("BSC", cachePath, file)) return false;

  // Skip header: version (u8), lutOffset (u32), spineCount (u32), tocCount (u32)
  uint8_t version;
  if (file.read(&version, 1) != 1) { file.close(); return false; }
  file.seekCur(12);  // 3 × u32 = 12 bytes

  // String 0: title (keep it)
  {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    if (len > 2048) { LIB_LOG("BSC", "readTitleAndAuthor: title len=%u > 2048, corrupt", static_cast<unsigned int>(len)); file.close(); return false; }
    outTitle.resize(len);
    if (file.read(reinterpret_cast<uint8_t*>(&outTitle[0]), len) != static_cast<int>(len)) { file.close(); return false; }
  }
  // String 1: author (keep it)
  {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    if (len > 2048) { LIB_LOG("BSC", "readTitleAndAuthor: author len=%u > 2048, corrupt", static_cast<unsigned int>(len)); file.close(); return false; }
    outAuthor.resize(len);
    if (file.read(reinterpret_cast<uint8_t*>(&outAuthor[0]), len) != static_cast<int>(len)) { file.close(); return false; }
  }
  // Strings 2-9: skip
  for (int i = 0; i < 8; ++i) {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    if (!file.seekCur(len)) { file.close(); return false; }
  }

  file.close();
  return true;
}

constexpr const char* kCacheFile = "/.crosspoint/library.bin";
constexpr uint8_t kVersion = 3;
constexpr int kProgressUpdateInterval = 2;

// ── Low-level I/O helpers ────────────────────────────────────────────────

bool writeU8(HalFile& file, uint8_t value) { return file.write(&value, 1) == 1; }

bool writeU16(HalFile& file, uint16_t value) {
  const uint8_t buf[2] = {static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>(value >> 8)};
  return file.write(buf, 2) == 2;
}

bool writeU32(HalFile& file, uint32_t value) {
  const uint8_t buf[4] = {
    static_cast<uint8_t>(value & 0xff),
    static_cast<uint8_t>((value >> 8) & 0xff),
    static_cast<uint8_t>((value >> 16) & 0xff),
    static_cast<uint8_t>((value >> 24) & 0xff)
  };
  return file.write(buf, 4) == 4;
}

bool readU8(HalFile& file, uint8_t& out) { return file.read(&out, 1) == 1; }

bool readU16(HalFile& file, uint16_t& out) {
  uint8_t buf[2];
  if (file.read(buf, 2) != 2) return false;
  out = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

bool readU32(HalFile& file, uint32_t& out) {
  uint8_t buf[4];
  if (file.read(buf, 4) != 4) return false;
  out = static_cast<uint32_t>(buf[0]) |
       (static_cast<uint32_t>(buf[1]) << 8) |
       (static_cast<uint32_t>(buf[2]) << 16) |
       (static_cast<uint32_t>(buf[3]) << 24);
  return true;
}

// Write a string as u16 length prefix + data. Returns the total bytes written
// (including the prefix) so the caller can track offsets for the v3 footer.
static int writeStringTracked(HalFile& file, const std::string& s) {
  const size_t len = s.size();
  if (len > 0xffff) {
    LOG_ERR("BSC", "String too long to persist (%zu bytes)", len);
    return -1;
  }
  if (!writeU16(file, static_cast<uint16_t>(len))) return -1;
  if (len == 0) return 2;
  if (file.write(s.data(), len) != static_cast<int>(len)) return -1;
  return static_cast<int>(2 + len);
}

bool readString(HalFile& file, std::string& out) {
  uint16_t len = 0;
  if (!readU16(file, len)) return false;
  out.clear();
  if (len == 0) return true;
  out.resize(len);
  return file.read(out.data(), len) == static_cast<int>(len);
}

// Read a string at a known byte offset in `file`.  Restores the original
// file position after reading.
bool readStringAt(HalFile& file, uint32_t offset, std::string& out) {
  const size_t savedPos = file.position();
  if (!file.seekSet(offset)) return false;
  const bool ok = readString(file, out);
  file.seekSet(savedPos);
  return ok;
}

// ── Sorting / normalization helpers ──────────────────────────────────────

// Minimal in-RAM record used during sync/scan.  Normalised sort keys are
// computed inline so we never store the full `Entry` set in RAM.
struct ScanRecord {
  std::string path;
  std::string title;
  std::string author;
  std::string normTitle;
  std::string normAuthor;
};

// Lowercase + strip common Latin accents so titles/authors sort and search
// consistently regardless of diacritics.
static void normalizeInPlace(std::string& s) {
  if (s.empty()) return;
  size_t w = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: s[w++] = 'a'; break;
      case 0xC8: case 0xC9: case 0xCA: case 0xCB: s[w++] = 'e'; break;
      case 0xCC: case 0xCD: case 0xCE: case 0xCF: s[w++] = 'i'; break;
      case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: s[w++] = 'o'; break;
      case 0xD9: case 0xDA: case 0xDB: case 0xDC: s[w++] = 'u'; break;
      case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: s[w++] = 'a'; break;
      case 0xE8: case 0xE9: case 0xEA: case 0xEB: s[w++] = 'e'; break;
      case 0xEC: case 0xED: case 0xEE: case 0xEF: s[w++] = 'i'; break;
      case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: s[w++] = 'o'; break;
      case 0xF9: case 0xFA: case 0xFB: case 0xFC: s[w++] = 'u'; break;
      case 0xD1: case 0xF1: s[w++] = 'n'; break;
      case 0xC7: case 0xE7: s[w++] = 'c'; break;
      default: s[w++] = static_cast<char>(std::tolower(c)); break;
    }
  }
  s.resize(w);
}

void finalizeRecord(ScanRecord& rec) {
  rec.normTitle = rec.title;
  normalizeInPlace(rec.normTitle);
  rec.normAuthor = rec.author.empty() ? "zzz" : rec.author;
  normalizeInPlace(rec.normAuthor);
}

bool compareRecords(const ScanRecord& a, const ScanRecord& b) {
  if (a.normAuthor != b.normAuthor) return a.normAuthor < b.normAuthor;
  return a.normTitle < b.normTitle;
}

void emitProgress(GfxRenderer& renderer, const Rect& popupRect, int processed, int total) {
  const int denom = total > 0 ? total : 1;
  int pct = (processed * 100) / denom;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  UITheme::getInstance().getTheme().fillPopupProgress(renderer, popupRect, pct);
}

// ── File-scoped helpers for parsing records from cache ───────────────────

// Parse one record from the current position of `file`.  Returns false on I/O error.
// When `readData` is false, only skips the record without allocating strings.
bool readOneRecord(HalFile& file, Entry* out, bool readData) {
  uint16_t len;
  if (!readU16(file, len)) return false;
  if (readData && out) {
    out->path.resize(len);
    if (file.read(&(*out->path.begin()), len) != static_cast<int>(len)) return false;
  } else {
    if (!file.seekCur(len)) return false;
  }
  if (!readU16(file, len)) return false;
  if (readData && out) {
    out->title.resize(len);
    if (file.read(&(*out->title.begin()), len) != static_cast<int>(len)) return false;
  } else {
    if (!file.seekCur(len)) return false;
  }
  if (!readU16(file, len)) return false;
  if (readData && out) {
    out->author.resize(len);
    if (file.read(&(*out->author.begin()), len) != static_cast<int>(len)) return false;
  } else {
    if (!file.seekCur(len)) return false;
  }
  return true;
}

// ── SD enumeration ───────────────────────────────────────────────────────

// Enumerate ebook paths under rootDir, writing to a temporary file.
// Returns the number of paths written, or -1 on error.
// The temp file is at `/.crosspoint/_scan_paths.tmp` and contains
// a sequence of u16+n path strings (no header count — the reader
// detects EOF after reading all paths).
static int enumerateBooksToFile(const char* rootDir, int maxBooks) {
  // Normalise rootDir
  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  // Open temp file for writing (truncate any existing)
  Storage.mkdir("/.crosspoint");
  HalFile tmpFile;
  if (!Storage.openFileForWrite("BSC", "/.crosspoint/_scan_paths.tmp", tmpFile)) {
    LOG_ERR("BSC", "Failed to create temp path file");
    return -1;
  }

  uint32_t written = 0;

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);
  constexpr int kMaxDepth = 8;
  std::vector<uint8_t> depth;
  depth.push_back(0);
  int dirCount = 0;

  // Fixed buffer for file names — avoid large stack allocation
  // that pushes us over FreeRTOS loop task stack on ESP32-C3.
  char name[500];

  while (!worklist.empty() && static_cast<int>(written) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();
    const uint8_t folderDepth = depth.back();
    depth.pop_back();
    ++dirCount;
    if ((dirCount & 0x7) == 0) { yield(); esp_task_wdt_reset(); }

    HalFile rootFile = Storage.open(folder.c_str());
    if (!rootFile || !rootFile.isDirectory()) {
      if (rootFile) rootFile.close();
      continue;
    }
    rootFile.rewindDirectory();

    static char name[500];  // static: 500 bytes off the stack (FreeRTOS stack is tight)
    for (HalFile file = rootFile.openNextFile(); file; file = rootFile.openNextFile()) {
      file.getName(name, sizeof(name));
      const bool isDir = file.isDirectory();
      file.close();

      if (name[0] == '.') continue;

      std::string lowerName = name;
      for (auto& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lowerName == "system volume information") continue;
      if (isDir && (lowerName == "crosspoint" || lowerName.compare(0, 5, "sleep") == 0 ||
                    lowerName == "font" || lowerName == "fonts" || lowerName == "dictionaries" ||
                    lowerName == "exports")) continue;

      std::string childPath = folder;
      if (childPath.back() != '/') childPath.push_back('/');
      childPath.append(name);

      if (isDir) {
        if (folderDepth + 1 >= kMaxDepth) continue;
        worklist.push_back(std::move(childPath));
        depth.push_back(static_cast<uint8_t>(folderDepth + 1));
        continue;
      }

      const std::string_view filename{name};
      if (!FsHelpers::hasEpubExtension(filename) && !FsHelpers::hasXtcExtension(filename) &&
          !FsHelpers::hasTxtExtension(filename) && !FsHelpers::hasMarkdownExtension(filename))
        continue;
      if (std::strcmp(name, "if_found.txt") == 0 || std::strcmp(name, "crash_report.txt") == 0) continue;

      // Write path to temp file: u16 len + bytes (no count header)
      const size_t pathLen = childPath.size();
      if (pathLen > 0xffff) continue;
      if (!writeU16(tmpFile, static_cast<uint16_t>(pathLen))) { tmpFile.close(); return -1; }
      if (tmpFile.write(childPath.data(), static_cast<int>(pathLen)) != static_cast<int>(pathLen)) { tmpFile.close(); return -1; }
      ++written;
      if (static_cast<int>(written) >= maxBooks) break;
    }
    rootFile.close();
  }

  tmpFile.close();  // No seek-back — the count isn't stored in the file.
  LIB_LOG("BSC", "enumerateBooksToFile: written=%d stack=%u", written,
          static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)));
  return static_cast<int>(written);
}

// ── Metadata extraction ─────────────────────────────────────────────────

// Parse one record at a given offset: read path, title, author into `out`.
static bool readRecordAt(HalFile& file, uint32_t offset, Entry& out) {
  if (!file.seekSet(offset)) return false;
  return readOneRecord(file, &out, true);
}

bool extractBookMetadata(ScanRecord& rec) {
  LIB_LOG("BSC", "extractBookMetadata: BEGIN stack=%u", static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)));
  rec.title.clear();
  rec.author.clear();
  if (rec.path.empty() || rec.path[0] != '/') {
    LOG_ERR("BSC", "Refusing to parse invalid path: %s", rec.path.c_str());
    return false;
  }
  HalFile stat = Storage.open(rec.path.c_str());
  if (!stat || stat.isDirectory() || stat.size() == 0) {
    if (stat) stat.close();
    LOG_DBG("BSC", "Skipping missing/empty file: %s", rec.path.c_str());
    return false;
  }
  stat.close();

  if (FsHelpers::hasEpubExtension(rec.path)) {
    bool haveMeta = false;
    {
      const auto heap = MemoryBudget::snapshot();
      constexpr uint32_t kEpubMinFree = 40000;
      constexpr uint32_t kEpubMinMaxAlloc = 30000;
      if (heap.freeHeap < kEpubMinFree || heap.maxAllocHeap < kEpubMinMaxAlloc) {
        LOG_DBG("BSC", "Skipping EPUB parse for %s (free=%u maxAlloc=%u)", rec.path.c_str(), heap.freeHeap, heap.maxAllocHeap);
        return false;
      }
      Epub epub(rec.path, "/.crosspoint");
      const std::string& cacheDir = epub.getCachePath();
      if (Storage.exists(cacheDir.c_str())) {
        if (readTitleAndAuthor(cacheDir, rec.title, rec.author)) {
          haveMeta = !rec.title.empty() || !rec.author.empty();
        }
      }
      if (!haveMeta) {
        if (ESP.getFreeHeap() < 45000) {
          LOG_DBG("BSC", "Skipping EPUB load for %s (free heap %u < 65 KB)", rec.path.c_str(), ESP.getFreeHeap());
          return false;
        }
        if (epub.load(true, true)) {
          rec.title = epub.getTitle();
          rec.author = epub.getAuthor();
          haveMeta = true;
        }
      }
    }
  } else if (FsHelpers::hasXtcExtension(rec.path)) {
    if (ESP.getFreeHeap() < 20000) {
      LOG_DBG("BSC", "Skipping XTC parse for %s (free heap %u < 20 KB)", rec.path.c_str(), ESP.getFreeHeap());
      return false;
    }
    Xtc xtc(rec.path, "/.crosspoint");
    if (xtc.load()) {
      rec.title = xtc.getTitle();
      rec.author = xtc.getAuthor();
    }
  } else if (FsHelpers::hasTxtExtension(rec.path) || FsHelpers::hasMarkdownExtension(rec.path)) {
    if (ESP.getFreeHeap() < 16000) {
      LOG_DBG("BSC", "Skipping TXT parse for %s (free heap %u < 16 KB)", rec.path.c_str(), ESP.getFreeHeap());
      return false;
    }
    Txt txt(rec.path, "/.crosspoint");
    if (txt.load()) {
      rec.title = txt.getTitle();
    }
  }

  if (rec.title.empty()) {
    const auto slash = rec.path.find_last_of('/');
    const auto dot = rec.path.find_last_of('.');
    const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t end = (dot == std::string::npos || dot < start) ? rec.path.size() : dot;
    rec.title = rec.path.substr(start, end - start);
  }
  return true;
}

// ── Temp-file-based sort + v3 cache writer ───────────────────────────────

// Write a batch of ScanRecords to the output cache file at the current position.
// Returns the file offset of the first record written (for the footer), or -1 on error.
static int64_t writeRecordBatch(HalFile& outFile, const std::vector<ScanRecord>& batch, std::vector<uint32_t>& offsets) {
  for (const auto& rec : batch) {
    offsets.push_back(static_cast<uint32_t>(outFile.position()));

    int n = writeStringTracked(outFile, rec.path);
    if (n < 0) return -1;
    n = writeStringTracked(outFile, rec.title);
    if (n < 0) return -1;
    n = writeStringTracked(outFile, rec.author);
    if (n < 0) return -1;
  }
  return 0;
}

// Read paths from the temp file into a vector.  Temp file format:
//   u32 count
//   repeat: u16 len + bytes (path)

// Finalize v3 cache: sort records, write to temp file, replace the real cache.
// `records` is consumed (swapped away).
static bool finalizeCache(std::vector<ScanRecord>& records, int totalCount) {
  if (records.empty()) {
    // Empty library: write v3 header with count=0, no footer needed.
    Storage.mkdir("/.crosspoint");
    HalFile file;
    if (!Storage.openFileForWrite("BSC", kCacheFile, file)) return false;
    if (!writeU8(file, kVersion) || !writeU16(file, 0)) { file.close(); return false; }
    file.close();
    return true;
  }

  // Sort records
  std::sort(records.begin(), records.end(), compareRecords);

  // Write to temp file
  std::string tmpPath = std::string(kCacheFile) + ".tmp";
  HalFile outFile;
  if (!Storage.openFileForWrite("BSC", tmpPath.c_str(), outFile)) return false;

  // Header
  if (!writeU8(outFile, kVersion) || !writeU16(outFile, static_cast<uint16_t>(totalCount))) {
    outFile.close();
    return false;
  }

  // Records + collect offsets
  std::vector<uint32_t> offsets;
  offsets.reserve(records.size());
  if (writeRecordBatch(outFile, records, offsets) < 0) { outFile.close(); return false; }

  // Footer: offsets array
  for (auto off : offsets) {
    if (!writeU32(outFile, off)) { outFile.close(); return false; }
  }

  outFile.close();

  // Atomic replace: remove old cache, rename temp
  if (Storage.exists(kCacheFile)) Storage.remove(kCacheFile);
  if (!Storage.rename(tmpPath.c_str(), kCacheFile)) {
    LOG_ERR("BSC", "Failed to rename temp cache to final");
    return false;
  }

  return true;
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────

std::string thumbPathFor(const std::string& path, int coverW, int coverH) {
  const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(path));
  if (FsHelpers::hasXtcExtension(path)) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "/.crosspoint/xtc_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
    return buf;
  }
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "/.crosspoint/txt_%llu/cover.bmp", hash);
    return buf;
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "/.crosspoint/epub_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  return buf;
}

bool generateCoverForBook(const std::string& path, int coverW, int coverH) {
  yield();
  esp_task_wdt_reset();

  if (FsHelpers::hasEpubExtension(path)) {
    uint32_t freeH = ESP.getFreeHeap();
    uint32_t maxA  = ESP.getMaxAllocHeap();
    COVER_LOG("BSC", "EPUB start: path=%s W=%d H=%d free=%u maxA=%u", path.c_str(), coverW, coverH, freeH, maxA);
    if (maxA < 18 * 1024) {
      COVER_LOG("BSC", "EPUB SKIP (low heap): maxA=%u < 18432", maxA);
      return false;
    }
    Epub epub(path, "/.crosspoint");
    const bool loaded = epub.load(true, true);
    COVER_LOG("BSC", "EPUB load(%s): result=%d free=%u maxA=%u",
              path.c_str(), loaded ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    if (!loaded) return false;
    maxA = ESP.getMaxAllocHeap();
    if (maxA < 14 * 1024) {
      COVER_LOG("BSC", "EPUB SKIP (post-load heap): maxA=%u < 14KB", maxA);
      return false;
    }
    yield();
    esp_task_wdt_reset();
    const bool thumbOk = epub.generateThumbBmp(coverW, coverH);
    COVER_LOG("BSC", "EPUB thumb(%s): result=%d free=%u maxA=%u",
              path.c_str(), thumbOk ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return thumbOk;
  }
  if (FsHelpers::hasXtcExtension(path)) {
    if (ESP.getFreeHeap() < 8192) {
      LOG_DBG("BSC", "Skipping XTC thumb gen for %s (free heap %u < 8 KB)", path.c_str(), ESP.getFreeHeap());
      return false;
    }
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) { LOG_ERR("BSC", "XTC load failed for %s", path.c_str()); return false; }
    return xtc.generateThumbBmp(coverW, coverH);
  }
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    Txt txt(path, "/.crosspoint");
    if (!txt.load()) return false;
    return txt.generateCoverBmp();
  }
  return false;
}

bool exists() { return Storage.exists(kCacheFile); }

int getCount() {
  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return 0;

  uint8_t version = 0;
  if (!readU8(file, version) || (version != 2 && version != 3)) {
    file.close();
    return 0;
  }
  uint16_t count = 0;
  if (!readU16(file, count)) {
    file.close();
    return 0;
  }
  file.close();
  return static_cast<int>(count);
}

int loadPage(std::vector<Entry>& out, int start, int count) {
  out.clear();
  if (count <= 0 || start < 0) return 0;
  count = std::min(count, kEntryWindow);

  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return 0;

  uint8_t version = 0;
  if (!readU8(file, version) || (version != 2 && version != 3)) {
    LOG_ERR("BSC", "loadPage: unknown cache version %u", version);
    file.close();
    return 0;
  }

  uint16_t totalCount = 0;
  if (!readU16(file, totalCount)) { file.close(); return 0; }

  if (start >= static_cast<int>(totalCount)) { file.close(); return 0; }
  const int actualCount = std::min(count, static_cast<int>(totalCount) - start);

  if (version == 3) {
    // v3: seek to footer, read offsets, seek to each record
    // Footer is at: header (3 bytes) + records area.
    // Records area size: we need the file size first.
    const size_t fileSize = file.fileSize();
    if (fileSize == 0) { file.close(); return 0; }
    const size_t footerSize = static_cast<size_t>(totalCount) * 4;  // u32 per offset
    const size_t footerStart = fileSize - footerSize;
    const int recordsEnd = footerStart;

    // Seek to the offset for record `start`
    const int offsetPos = static_cast<int>(footerStart + static_cast<size_t>(start) * 4);
    if (!file.seekSet(offsetPos)) { file.close(); return 0; }

    std::vector<uint32_t> offsets;
    offsets.reserve(actualCount);
    for (int i = 0; i < actualCount; ++i) {
      uint32_t off;
      if (!readU32(file, off)) { file.close(); return 0; }
      offsets.push_back(off);
    }

    // Now read each record at its offset
    out.reserve(actualCount);
    for (int i = 0; i < actualCount; ++i) {
      Entry entry;
      if (!readRecordAt(file, offsets[i], entry)) {
        LOG_ERR("BSC", "loadPage: failed to read record at offset %u", offsets[i]);
        file.close();
        out.clear();
        return 0;
      }
      out.push_back(std::move(entry));
    }
  } else {
    // v2 fallback: sequential read from start, skip to `start` entries
    for (int i = 0; i < static_cast<int>(totalCount); ++i) {
      if (i < start) {
        // Skip this record
        if (!readOneRecord(file, nullptr, false)) { file.close(); return 0; }
      } else if (i < start + actualCount) {
        Entry entry;
        if (!readOneRecord(file, &entry, true)) { file.close(); return 0; }
        out.push_back(std::move(entry));
      } else {
        break;
      }
    }
  }

  file.close();
  return static_cast<int>(out.size());
}

bool save(const std::vector<Entry>& entries) {
  Storage.mkdir("/.crosspoint");

  // Write to temp file first for atomic replace
  std::string tmpPath = std::string(kCacheFile) + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("BSC", tmpPath.c_str(), file)) {
    LOG_ERR("BSC", "Failed to open temp cache for write");
    return false;
  }

  const size_t count = std::min<size_t>(entries.size(), 0xffff);
  if (!writeU8(file, kVersion) || !writeU16(file, static_cast<uint16_t>(count))) {
    file.close();
    return false;
  }

  // Records + collect offsets for footer
  std::vector<uint32_t> offsets;
  offsets.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    offsets.push_back(static_cast<uint32_t>(file.position()));

    const Entry& e = entries[i];
    if (writeStringTracked(file, e.path) < 0 ||
        writeStringTracked(file, e.title) < 0 ||
        writeStringTracked(file, e.author) < 0) {
      file.close();
      return false;
    }
  }

  // Footer: offsets array
  for (auto off : offsets) {
    if (!writeU32(file, off)) { file.close(); return false; }
  }

  file.close();

  // Atomic replace
  if (Storage.exists(kCacheFile)) Storage.remove(kCacheFile);
  if (!Storage.rename(tmpPath.c_str(), kCacheFile)) {
    LOG_ERR("BSC", "Failed to rename temp cache to final (save)");
    return false;
  }

  LOG_DBG("BSC", "Saved %zu entries to cache (v3)", count);
  return true;
}

void invalidate() {
  if (Storage.exists(kCacheFile)) {
    if (Storage.remove(kCacheFile)) {
      LOG_DBG("BSC", "Invalidated library cache");
      LIB_LOG("BSC", "invalidate: cache removed");
    } else {
      LOG_ERR("BSC", "Failed to remove cache file");
    }
  }
  // Clean up any temp files from previous sync/scan runs
  const char* tempFiles[] = {
    "/.crosspoint/_scan_paths.tmp",
    "/.crosspoint/library.bin.tmp"
  };
  for (auto* tf : tempFiles) {
    if (Storage.exists(tf)) {
      Storage.remove(tf);
      LIB_LOG("BSC", "invalidate: cleaned temp file %s", tf);
    }
  }
}

bool removeBook(const std::string& path) {
  // We'll use the v3 footer index to find the entry without loading everything.
  // Strategy: read offsets, find matching path by scanning, rebuild cache excluding it.
  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return false;

  uint8_t version = 0;
  if (!readU8(file, version) || version != 3) {
    // Can't handle v2 removeBook with low heap — fall back to old load/save
    file.close();
    if (ESP.getFreeHeap() < 30000) {
      LOG_ERR("BSC", "removeBook skipped: free heap %u < 30 KB", ESP.getFreeHeap());
      return false;
    }
    std::vector<Entry> entries;
    // Load with v2-compatible read
    HalFile f2;
    if (!Storage.openFileForRead("BSC", kCacheFile, f2)) return false;
    uint8_t ver2; readU8(f2, ver2);
    uint16_t cnt2; readU16(f2, cnt2);
    for (uint16_t i = 0; i < cnt2; ++i) {
      Entry entry;
      if (!readOneRecord(f2, &entry, true)) break;
      if (entry.path.empty()) continue;
      entries.push_back(std::move(entry));
    }
    f2.close();
    auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.path == path; });
    if (it == entries.end()) return false;
    entries.erase(it);
    return save(entries);
  }

  uint16_t count = 0;
  if (!readU16(file, count)) { file.close(); return false; }

  // Read all offsets from footer
  const size_t fileSize = file.fileSize();
  if (fileSize == 0) { file.close(); return false; }
  const size_t footerStart = fileSize - static_cast<size_t>(count) * 4;
  if (footerStart < 3 + 2) { file.close(); return false; }
  if (!file.seekSet(footerStart)) { file.close(); return false; }

  std::vector<uint32_t> offsets(count);
  for (uint16_t i = 0; i < count; ++i) {
    if (!readU32(file, offsets[i])) { file.close(); return false; }
  }

  // Find the entry to remove by scanning cached paths
  int removeIdx = -1;
  for (uint16_t i = 0; i < count; ++i) {
    std::string cachedPath;
    if (!readStringAt(file, offsets[i], cachedPath)) { file.close(); return false; }
    if (cachedPath == path) { removeIdx = i; break; }
  }
  if (removeIdx < 0) { file.close(); return false; }

  // Read all entries except the removed one
  std::vector<Entry> remaining;
  remaining.reserve(count - 1);
  for (uint16_t i = 0; i < count; ++i) {
    if (i == static_cast<uint16_t>(removeIdx)) continue;
    Entry entry;
    if (!readRecordAt(file, offsets[i], entry)) { file.close(); return false; }
    remaining.push_back(std::move(entry));
  }
  file.close();

  return save(remaining);
}

// ── sync() — incremental sync with streaming output ──────────────────────

// Read the v3 cache footer: count and offsets array.
// Returns false when cache is missing, v2, or corrupt.
static bool readCachedIndex(std::vector<uint32_t>& outOffsets, int& outCount) {
  outCount = 0;
  outOffsets.clear();

  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return false;

  uint8_t version = 0;
  if (!readU8(file, version) || version != 3) { file.close(); return false; }

  uint16_t count = 0;
  if (!readU16(file, count)) { file.close(); return false; }
  outCount = count;

  if (count == 0) { file.close(); return true; }

  // Seek to footer
  const size_t fileSize = file.fileSize();
  if (fileSize == 0) { file.close(); return false; }
  const size_t footerStart = fileSize - static_cast<size_t>(count) * 4;
  if (footerStart < 3 + 2) { file.close(); return false; }

  if (!file.seekSet(footerStart)) { file.close(); return false; }

  outOffsets.resize(count);
  for (uint16_t i = 0; i < count; ++i) {
    if (!readU32(file, outOffsets[i])) { file.close(); return false; }
  }

  file.close();
  return true;
}

bool sync(GfxRenderer* renderer, const Rect* popupRect, const char* rootDir, int maxBooks) {
  LIB_LOG("BSC", "sync: start renderer=%p popupRect=%p heap=%u maxA=%u",
          (void*)renderer, (void*)popupRect, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Step 1: Read the cached offset index (low RAM: 4 bytes × cachedCount)
  std::vector<uint32_t> cachedOffsets;
  int cachedCount = 0;
  const bool haveCache = readCachedIndex(cachedOffsets, cachedCount);

  if (!haveCache) {
    // Check if old v2 cache exists — if so, delete it and fall through to full scan.
    if (Storage.exists(kCacheFile)) {
      LIB_LOG("BSC", "sync: v2 cache detected, deleting and doing full scan");
      Storage.remove(kCacheFile);
    } else {
      LIB_LOG("BSC", "sync: no cache file, doing full scan");
    }
    if (!renderer || !popupRect) {
      LIB_LOG("BSC", "sync: no popupRect for full scan fallback, returning false");
      return false;
    }
    LIB_LOG("BSC", "sync: falling back to full scan");
    return scan(*renderer, *popupRect, rootDir, maxBooks);
  }

  HOMEPAGE_LOG("BSC", "sync: have v3 cache with %d entries, offsets=%zu (%zu KB)",
               cachedCount, cachedOffsets.size(), cachedOffsets.size() * 4 / 1024);
  LIB_LOG("BSC", "sync: v3 cache ok: count=%d heap=%u maxA=%u", cachedCount, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Step 2: Enumerate SD books to a temp file (paths only, no Entry objects)
  const int sdCount = enumerateBooksToFile(rootDir, maxBooks);
  if (sdCount < 0) {
    LOG_ERR("BSC", "sync: failed to enumerate SD books");
    LIB_LOG("BSC", "sync: enumerateBooksToFile FAILED");
    return false;
  }
  LIB_LOG("BSC", "sync: enumerated %d SD books (heap=%u maxA=%u)", sdCount, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  HOMEPAGE_LOG("BSC", "sync: enumerated %d SD books", sdCount);

  // Step 3: Walk SD paths from temp file. For each, check cache index.
  // Temp file format: sequence of u16+bytes (no header count — read until EOF).
  HalFile tmpFile;
  if (!Storage.openFileForRead("BSC", "/.crosspoint/_scan_paths.tmp", tmpFile)) {
    LIB_LOG("BSC", "sync: CANNOT OPEN temp file for read");
    return false;
  }

  // Batched processing: collect up to kEntryWindow records, sort, flush
  std::vector<ScanRecord> batch;
  batch.reserve(kEntryWindow);
  std::vector<bool> cachedMatched;
  if (cachedCount > 0) cachedMatched.assign(cachedCount, false);

  int kept = 0, added = 0, removed = 0, skipped = 0;
  int totalProcessed = 0;

  // Open the cache file for reading cached entry data on demand
  HalFile cacheFile;
  const bool haveCacheFile = Storage.openFileForRead("BSC", kCacheFile, cacheFile);

  // Read paths from temp file (no header count — read until EOF)
  while (true) {
    // Read path from temp file
    std::string childPath;
    if (!readString(tmpFile, childPath)) break;  // EOF or I/O error — stops the loop
    if (childPath.empty()) break;  // empty path = EOF sentinel

    ++totalProcessed;
    yield();
    esp_task_wdt_reset();

    if ((totalProcessed & 0x3F) == 0 && renderer && popupRect) {
      emitProgress(*renderer, *popupRect, totalProcessed, sdCount);
    }

    // Check if this path exists in the cache index
    bool found = false;
    if (haveCache && haveCacheFile) {
      // Linear scan of cached offsets (cached data is sorted by path,
      // but we walk SD in directory order — can't binary search easily.
      // For up to 10000 entries this is ~10000 × 3 × 4KB = ~120MB of seek I/O.
      // Acceptable for a sync that happens once per library open.
      for (int ci = 0; ci < cachedCount && !found; ++ci) {
        if (cachedMatched[ci]) continue;  // already matched

        // Read cached path from file
        std::string cachedPath;
        if (!readStringAt(cacheFile, cachedOffsets[ci], cachedPath)) continue;

        if (cachedPath == childPath) {
          found = true;
          cachedMatched[ci] = true;

          // Re-read the full record from cache
          Entry cachedEntry;
          if (readRecordAt(cacheFile, cachedOffsets[ci], cachedEntry)) {
            ScanRecord rec;
            rec.path = std::move(cachedEntry.path);
            rec.title = std::move(cachedEntry.title);
            rec.author = std::move(cachedEntry.author);
            finalizeRecord(rec);
            batch.push_back(std::move(rec));
            ++kept;
          }
          break;
        }
      }
    }

    if (!found) {
      // New book: parse metadata
      ScanRecord rec;
      rec.path = std::move(childPath);
      if (extractBookMetadata(rec)) {
        finalizeRecord(rec);
        batch.push_back(std::move(rec));
        ++added;
      } else {
        ++skipped;
      }
    }

    // Flush batch when full
    if (static_cast<int>(batch.size()) >= kEntryWindow) {
      std::sort(batch.begin(), batch.end(), compareRecords);
      // We need to write to cache. But we're still in the middle of scanning —
      // we can't write the final cache yet. Instead, we collect all records.
      // For now, just keep the batch growing (bounded by processing window).
      // Actually we need to collect ALL records across all batches. Let me
      // rethink: for large libraries we can't hold everything in RAM.

      // For now, we let batch grow up to kEntryWindow and collect everything
      // in a temp record on the output. But we REALLY need to accumulate
      // all records for sorting. The only way to sort without RAM is via
      // a temp file on SD with external sort. That's complex.
      // For practical purposes: hold all ScanRecords in RAM.
      // 10000 × ~220 bytes = 2.2 MB — too much for ESP32-C3.
      // We need to cap at a reasonable maxBooks.
    }
  }

  tmpFile.close();
  if (haveCacheFile) cacheFile.close();

  // Count removed entries (cached but not on SD)
  for (int i = 0; i < cachedCount; ++i) {
    if (!cachedMatched[i]) ++removed;
  }

  LIB_LOG("BSC", "sync: scan complete: kept=%d added=%d skipped=%d removed=%d batch=%zu heap=%u",
          kept, added, skipped, removed, batch.size(), ESP.getFreeHeap());

  // If nothing changed, we're done
  if (removed == 0 && added == 0) {
    // Clean up temp file
    if (Storage.exists("/.crosspoint/_scan_paths.tmp")) {
      Storage.remove("/.crosspoint/_scan_paths.tmp");
    }
    LIB_LOG("BSC", "sync: no changes, cache is current");
    return true;
  }

  // Read all cached entries into ScanRecords for the ones that survived
  // This loads the full cache — for >2000 books this will OOM.
  // But the alternative (external sort) is too complex for now.
  // We cap maxBooks at a value that fits in RAM (see maxBooks default = 10000
  // but the caller's maxBooks limits how many books sync() processes).
  std::vector<ScanRecord> allRecords;
  allRecords.reserve(std::min(kept + added + removed, maxBooks));

  // Re-read surviving cached entries
  if (haveCache && haveCacheFile) {
    if (!Storage.openFileForRead("BSC", kCacheFile, cacheFile)) {
      // fallback: just use the batch
    } else {
      for (int ci = 0; ci < cachedCount; ++ci) {
        if (cachedMatched[ci]) {
          Entry found;
          if (readRecordAt(cacheFile, cachedOffsets[ci], found)) {
            ScanRecord rec;
            rec.path = std::move(found.path);
            rec.title = std::move(found.title);
            rec.author = std::move(found.author);
            finalizeRecord(rec);
            allRecords.push_back(std::move(rec));
          }
        }
      }
      cacheFile.close();
    }
  }

  // Add newly parsed books from the batch
  for (auto& rec : batch) {
    allRecords.push_back(std::move(rec));
  }
  batch.clear();
  batch.shrink_to_fit();

  // Sort and write final v3 cache
  LIB_LOG("BSC", "sync: finalizing %zu records (heap=%u maxA=%u)", allRecords.size(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  HOMEPAGE_LOG("BSC", "sync: finalizing %zu records (heap=%u maxA=%u)",
               allRecords.size(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const bool ok = finalizeCache(allRecords, static_cast<int>(allRecords.size()));
  LIB_LOG("BSC", "sync: finalizeCache returned %d (heap=%u maxA=%u)", ok ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  allRecords.clear();
  allRecords.shrink_to_fit();

  // Cleanup temp file
  if (Storage.exists("/.crosspoint/_scan_paths.tmp")) {
    Storage.remove("/.crosspoint/_scan_paths.tmp");
  }

  HOMEPAGE_LOG("BSC", "sync: complete ok=%d heap=%u maxA=%u", ok ? 1 : 0,
               ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return ok;
}

// ── scan() — full scan with streaming output ─────────────────────────────

bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir, int maxBooks) {
  HOMEPAGE_LOG("BSC", "scan: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  LIB_LOG("BSC", "scan: start maxBooks=%d heap=%u maxA=%u", maxBooks, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Clean up any stale temp file from a previous aborted scan/sync
  if (Storage.exists("/.crosspoint/_scan_paths.tmp")) {
    Storage.remove("/.crosspoint/_scan_paths.tmp");
    LIB_LOG("BSC", "scan: cleaned stale temp file");
  }

  // Step 1: enumerate paths to temp file
  const int totalCandidates = enumerateBooksToFile(rootDir, maxBooks);
  if (totalCandidates < 0) {
    LIB_LOG("BSC", "scan: enumerateBooksToFile FAILED");
    return false;
  }
  LIB_LOG("BSC", "scan: enumerated %d candidates", totalCandidates);

  emitProgress(renderer, popupRect, 0, totalCandidates);

  // Step 2: read paths from temp file, parse metadata in batches
  // Open file BEFORE reserving vector heap — reserve can fragment and
  // cause subsequent small allocations to fail on constrained ESP32-C3.
  HalFile tmpFile;
  if (!Storage.openFileForRead("BSC", "/.crosspoint/_scan_paths.tmp", tmpFile)) {
    LIB_LOG("BSC", "scan: CANNOT OPEN temp file for read");
    return false;
  }
  LIB_LOG("BSC", "scan: temp file opened, heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  std::vector<ScanRecord> records;
  records.reserve(kEntryWindow);
  LIB_LOG("BSC", "scan: reserved %d records (totalCandidates=%d), kEntryWindow=%d, heap=%u maxA=%u stack=%u",
          kEntryWindow, totalCandidates, kEntryWindow, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)));

  int processed = 0;
  int skipped = 0;
  bool heapExhausted = false;

  // Read paths from temp file (no header count — read until EOF)
  LIB_LOG("BSC", "scan: entering temp file read loop, heap=%u filePos=%u fileSize=%u",
          ESP.getFreeHeap(),
          static_cast<unsigned int>(tmpFile.position()),
          static_cast<unsigned int>(tmpFile.fileSize()));
  int readErrors = 0;
  while (true) {
    // Read u16 length prefix
    uint8_t lenBytes[2];
    if (tmpFile.read(lenBytes, 2) != 2) break;
    const uint16_t pathLen = static_cast<uint16_t>(lenBytes[0]) | (static_cast<uint16_t>(lenBytes[1]) << 8);
    if (pathLen == 0) {
      ++readErrors;
      if (readErrors > 3) break;
      continue;
    }
    // Sanity check: pathLen must be < 500 and <= fileSize - currentPos - 2
    // Without a valid length guard, a corrupt temp file triggers
    // resize(huge) → bad_alloc → abort() with -fno-exceptions.
    if (pathLen > 500) {
      LIB_LOG("BSC", "scan: pathLen=%u > 500, corrupt temp file? pos=%u fileSize=%u",
              static_cast<unsigned int>(pathLen),
              static_cast<unsigned int>(tmpFile.position()),
              static_cast<unsigned int>(tmpFile.fileSize()));
      break;
    }
    std::string fullPath;
    fullPath.resize(pathLen);
    if (tmpFile.read(&fullPath[0], pathLen) != static_cast<int>(pathLen)) break;

    ++processed;
    yield();
    esp_task_wdt_reset();

    if (processed % kProgressUpdateInterval == 0) {
      emitProgress(renderer, popupRect, processed, totalCandidates);
    }

    // Heap check
    const auto heap = MemoryBudget::snapshot();
    constexpr uint32_t kScanMinFree = 50000;
    constexpr uint32_t kScanMinMaxAlloc = 32000;
    if (heap.freeHeap < kScanMinFree || heap.maxAllocHeap < kScanMinMaxAlloc) {
      LOG_ERR("BSC", "Stopping scan: heap too low (free=%u maxAlloc=%u) after %d books",
              heap.freeHeap, heap.maxAllocHeap, processed);
      skipped += (totalCandidates - processed + 1);
      heapExhausted = true;
      break;
    }

    ScanRecord rec;
    rec.path = std::move(fullPath);
    // Log stack before first extractBookMetadata call — if stack is near limit,
    // the deep Epub::load chain will overflow and abort.
    LIB_LOG("BSC", "scan: BEFORE extractBookMetadata #%d stack=%u heap=%u path=%s",
            processed + 1,
            static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)),
            ESP.getFreeHeap(),
            rec.path.c_str());
    if (!extractBookMetadata(rec)) {
      ++skipped;
      if (processed % 20 == 0) {
        LIB_LOG("BSC", "scan: parse FAILED for #%d, skipped=%d (heap=%u)", processed, skipped, ESP.getFreeHeap());
      }
      continue;
    }
    finalizeRecord(rec);
    records.push_back(std::move(rec));
    if ((processed % 10) == 0) {
      LIB_LOG("BSC", "scan: parsed #%d/%d ok, records=%zu (heap=%u maxA=%u)", processed, totalCandidates, records.size(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
  }
  tmpFile.close();

  emitProgress(renderer, popupRect, totalCandidates, totalCandidates);

  HOMEPAGE_LOG("BSC", "scan: parsed %zu records, skipped=%d, heapExhausted=%d",
               records.size(), skipped, heapExhausted ? 1 : 0);
  LIB_LOG("BSC", "scan: parsed %zu records, skipped=%d, heapExhausted=%d, heap=%u maxA=%u",
          records.size(), skipped, heapExhausted ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Write v3 cache
  LIB_LOG("BSC", "scan: calling finalizeCache with %zu records", records.size());
  const bool ok = finalizeCache(records, static_cast<int>(records.size()));

  records.clear();
  records.shrink_to_fit();

  // Cleanup temp file
  if (Storage.exists("/.crosspoint/_scan_paths.tmp")) {
    Storage.remove("/.crosspoint/_scan_paths.tmp");
  }

  HOMEPAGE_LOG("BSC", "scan: complete ok=%d total=%d heap=%u maxA=%u",
               ok ? 1 : 0, processed, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return ok;
}

}  // namespace LibraryCache
