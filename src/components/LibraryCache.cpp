#include "LibraryCache.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "components/UITheme.h"

namespace LibraryCache {

namespace {

constexpr const char* kCacheFile = "/.crosspoint/library.bin";
constexpr uint8_t kVersion = 2;
constexpr int kProgressUpdateInterval = 2;

struct ScanRecord {
  std::string path;
  std::string title;
  std::string author;
  std::string normTitle;   // pre-normalized for zero-alloc sort
  std::string normAuthor;  // pre-normalized for zero-alloc sort (sort key: "zzz" when empty)
};

// Lowercase + strip common Latin accents so titles/authors sort and search
// consistently regardless of diacritics. Shared by finalizeRecord (scan/sync)
// and by load() so cached entries also carry normalized keys.
std::string normalizeString(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: out.push_back('a'); break;
      case 0xC8: case 0xC9: case 0xCA: case 0xCB: out.push_back('e'); break;
      case 0xCC: case 0xCD: case 0xCE: case 0xCF: out.push_back('i'); break;
      case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: out.push_back('o'); break;
      case 0xD9: case 0xDA: case 0xDB: case 0xDC: out.push_back('u'); break;
      case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: out.push_back('a'); break;
      case 0xE8: case 0xE9: case 0xEA: case 0xEB: out.push_back('e'); break;
      case 0xEC: case 0xED: case 0xEE: case 0xEF: out.push_back('i'); break;
      case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: out.push_back('o'); break;
      case 0xF9: case 0xFA: case 0xFB: case 0xFC: out.push_back('u'); break;
      case 0xD1: case 0xF1: out.push_back('n'); break;
      case 0xC7: case 0xE7: out.push_back('c'); break;
      default: out.push_back(static_cast<char>(std::tolower(c))); break;
    }
  }
  return out;
}

void finalizeRecord(ScanRecord& rec) {
  rec.normTitle = normalizeString(rec.title);
  rec.normAuthor = normalizeString(rec.author.empty() ? "zzz" : rec.author);
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

bool writeU8(HalFile& file, uint8_t value) { return file.write(&value, 1) == 1; }

bool writeU16(HalFile& file, uint16_t value) {
  const uint8_t buf[2] = {static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>(value >> 8)};
  return file.write(buf, 2) == 2;
}

bool writeString(HalFile& file, const std::string& s) {
  const size_t len = s.size();
  if (len > 0xffff) {
    LOG_ERR("BSC", "String too long to persist (%zu bytes)", len);
    return false;
  }
  if (!writeU16(file, static_cast<uint16_t>(len))) return false;
  if (len == 0) return true;
  return file.write(s.data(), len) == static_cast<int>(len);
}

bool readU8(HalFile& file, uint8_t& out) { return file.read(&out, 1) == 1; }

bool readU16(HalFile& file, uint16_t& out) {
  uint8_t buf[2];
  if (file.read(buf, 2) != 2) return false;
  out = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

bool readString(HalFile& file, std::string& out) {
  uint16_t len = 0;
  if (!readU16(file, len)) return false;
  out.clear();
  if (len == 0) return true;
  out.resize(len);
  return file.read(out.data(), len) == static_cast<int>(len);
}

void enumerateBooks(std::vector<std::string>& outPaths, const char* rootDir, int maxBooks) {
  // Normalise rootDir: ensure it starts with "/" and does not end with "/"
  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);

  int dirCount = 0;
  // Only collect up to maxBooks candidates: scan() can only index maxBooks
  // entries anyway, and collecting thousands of extra path strings needlessly
  // peaks RAM on the constrained ESP32-C3 (was maxBooks * 8, which OOM'd large
  // libraries mid-scan).
  while (!worklist.empty() && static_cast<int>(outPaths.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();

    ++dirCount;
    if ((dirCount & 0x7) == 0) {
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
        worklist.push_back(std::move(childPath));
        continue;
      }

      const std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
          FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
        if (std::strcmp(name, "if_found.txt") != 0 && std::strcmp(name, "crash_report.txt") != 0) {
          outPaths.push_back(std::move(childPath));
        }
      }
    }
    rootFile.close();
  }
}

// Helper: extract EPUB metadata, trying the persistent cache first
// to avoid opening the ZIP when the book was previously read.
// Returns true if metadata was successfully extracted, false on OOM/failure.
bool extractBookMetadata(ScanRecord& rec) {
  rec.title.clear();
  rec.author.clear();

  if (FsHelpers::hasEpubExtension(rec.path)) {
    // Scope-limit the Epub object so it is destroyed before we move to
    // the next book, releasing ZIP buffer / page data immediately.
    bool haveMeta = false;
    {
      Epub epub(rec.path, "/.crosspoint");
      const std::string& cacheDir = epub.getCachePath();
      if (Storage.exists(cacheDir.c_str())) {
        BookMetadataCache metaCache(cacheDir);
        if (metaCache.load()) {
          rec.title = metaCache.coreMetadata.title;
          rec.author = metaCache.coreMetadata.author;
          haveMeta = true;
        }
      }
      // Fallback: full EPUB load (this builds the cache if missing).
      // Skip only when heap is critically low; building the cache now
      // makes cover generation several seconds faster per book.
      if (!haveMeta) {
        if (ESP.getFreeHeap() < 65000) {
          LOG_DBG("BSC", "Skipping EPUB load for %s (free heap %u < 65 KB)", rec.path.c_str(), ESP.getFreeHeap());
          return false;
        }
        if (epub.load(true, true)) {
          rec.title = epub.getTitle();
          rec.author = epub.getAuthor();
          haveMeta = true;
        }
      }
    }  // Epub destroyed here
  } else if (FsHelpers::hasXtcExtension(rec.path)) {
    Xtc xtc(rec.path, "/.crosspoint");
    if (xtc.load()) {
      rec.title = xtc.getTitle();
      rec.author = xtc.getAuthor();
    }
  }
  // TXT/Markdown: no embedded metadata — title will be derived from filename below

  if (rec.title.empty()) {
    const auto slash = rec.path.find_last_of('/');
    const auto dot = rec.path.find_last_of('.');
    const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t end = (dot == std::string::npos || dot < start) ? rec.path.size() : dot;
    rec.title = rec.path.substr(start, end - start);
  }
  return true;
}

}  // namespace

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
  // Keep the main-task watchdog fed. Callers (per-page library indexing) may
  // invoke this from the activity loop with no other WDT reset, and an EPUB/XTC
  // cover decode can exceed the loop WDT timeout on the ESP32-C3 (~380 KB heap).
  yield();
  esp_task_wdt_reset();

  if (FsHelpers::hasEpubExtension(path)) {
    // Scope-limit the Epub object so ZIP/page buffers are released immediately
    // after the thumbnail is written.
    Epub epub(path, "/.crosspoint");
    if (!epub.load(true, true)) return false;
    return epub.generateThumbBmp(coverW, coverH);
  }
  if (FsHelpers::hasXtcExtension(path)) {
    // XTC thumbnail generation now streams from the cover BMP (row by row)
    // and requires very little contiguous heap (~300 bytes). Keep a small
    // safety margin so we don't attempt generation when the heap is truly exhausted.
    if (ESP.getFreeHeap() < 8192) {
      LOG_DBG("BSC", "Skipping XTC thumb gen for %s (free heap %u < 8 KB)", path.c_str(), ESP.getFreeHeap());
      return false;
    }
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) {
      LOG_ERR("BSC", "XTC load failed for %s", path.c_str());
      return false;
    }
    const bool generated = xtc.generateThumbBmp(coverW, coverH);
    if (!generated) {
      LOG_ERR("BSC", "XTC thumb gen failed for %s (%dx%d) — pageCount=%lu, freeHeap=%u",
              path.c_str(), coverW, coverH, xtc.getPageCount(), ESP.getFreeHeap());
    }
    return generated;
  }
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    Txt txt(path, "/.crosspoint");
    if (!txt.load()) return false;
    return txt.generateCoverBmp();
  }
  return false;
}

bool exists() { return Storage.exists(kCacheFile); }

bool load(std::vector<Entry>& out) {
  out.clear();

  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return false;

  uint8_t version = 0;
  if (!readU8(file, version) || version != kVersion) {
    LOG_ERR("BSC", "Unknown library cache version %u (expected %u)", version, kVersion);
    file.close();
    return false;
  }

  uint16_t count = 0;
  if (!readU16(file, count)) {
    file.close();
    return false;
  }

  out.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    Entry entry;
    if (!readString(file, entry.path) || !readString(file, entry.title) || !readString(file, entry.author)) {
      LOG_ERR("BSC", "Truncated cache file at entry %u/%u", i, count);
      file.close();
      out.clear();
      return false;
    }
    if (entry.path.empty()) continue;
    out.push_back(std::move(entry));
  }
  file.close();
  LOG_DBG("BSC", "Loaded %d entries from cache", static_cast<int>(out.size()));
  return true;
}

bool save(const std::vector<Entry>& entries) {
  Storage.mkdir("/.crosspoint");

  HalFile file;
  if (!Storage.openFileForWrite("BSC", kCacheFile, file)) {
    LOG_ERR("BSC", "Failed to open cache file for write");
    return false;
  }

  const size_t count = std::min<size_t>(entries.size(), 0xffff);
  if (!writeU8(file, kVersion) || !writeU16(file, static_cast<uint16_t>(count))) {
    LOG_ERR("BSC", "Failed to write cache header");
    file.close();
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    const Entry& e = entries[i];
    if (!writeString(file, e.path) || !writeString(file, e.title) || !writeString(file, e.author)) {
      LOG_ERR("BSC", "Failed to write entry %zu", i);
      file.close();
      return false;
    }
  }

  file.close();
  LOG_DBG("BSC", "Saved %zu entries to cache", count);
  return true;
}

void invalidate() {
  if (!Storage.exists(kCacheFile)) return;
  if (Storage.remove(kCacheFile)) {
    LOG_DBG("BSC", "Invalidated library cache");
  } else {
    LOG_ERR("BSC", "Failed to remove cache file");
  }
}

bool removeBook(const std::string& path) {
  std::vector<Entry> entries;
  if (!load(entries)) return false;

  auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.path == path; });
  if (it == entries.end()) return false;

  entries.erase(it);
  return save(entries);
}

bool sync(std::vector<Entry>& out, const char* rootDir, int maxBooks) {
  out.clear();
  if (maxBooks <= 0) return true;

  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<Entry> cached;
  if (!load(cached)) {
    LOG_DBG("BSC", "sync: cache not available, falling back to full scan");
    return false;
  }

  std::sort(cached.begin(), cached.end(),
            [](const Entry& a, const Entry& b) { return a.path < b.path; });

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(static_cast<int>(cached.size()) + 16, maxBooks));

  int removed = 0;
  int added = 0;
  int kept = 0;
  int sdFileCount = 0;
  int dirCount = 0;

  auto cachedIndexForPath = [&cached](const std::string& path) -> int {
    const auto it = std::lower_bound(cached.begin(), cached.end(), path,
                                     [](const Entry& e, const std::string& p) { return e.path < p; });
    if (it != cached.end() && it->path == path)
      return static_cast<int>(it - cached.begin());
    return -1;
  };

  std::vector<bool> cachedMatched(cached.size(), false);

  while (!worklist.empty() && static_cast<int>(records.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();

    ++dirCount;
    if ((dirCount & 0x7) == 0) {
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
      const bool isDir = file.isDirectory();
      file.close();

      if (name[0] == '.') continue;

      std::string lowerName = name;
      for (auto& c : lowerName)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lowerName == "system volume information") continue;
      if (isDir && (lowerName == "crosspoint" || lowerName.compare(0, 5, "sleep") == 0 ||
                    lowerName == "font" || lowerName == "fonts" || lowerName == "dictionaries" ||
                    lowerName == "exports")) continue;

      std::string childPath = folder;
      if (childPath.back() != '/') childPath.push_back('/');
      childPath.append(name);

      if (isDir) {
        worklist.push_back(std::move(childPath));
        continue;
      }

      const std::string_view filename{name};
      if (!FsHelpers::hasEpubExtension(filename) && !FsHelpers::hasXtcExtension(filename) &&
          !FsHelpers::hasTxtExtension(filename) && !FsHelpers::hasMarkdownExtension(filename))
        continue;
      if (std::strcmp(name, "if_found.txt") == 0 || std::strcmp(name, "crash_report.txt") == 0) continue;

      ++sdFileCount;

      const int ci = cachedIndexForPath(childPath);
      if (ci >= 0) {
        cachedMatched[ci] = true;
        ScanRecord rec;
        rec.path = childPath;
        rec.title = cached[ci].title;
        rec.author = cached[ci].author;
        finalizeRecord(rec);
        records.push_back(std::move(rec));
        ++kept;
      } else {
        ScanRecord rec;
        rec.path = std::move(childPath);
        extractBookMetadata(rec);
        finalizeRecord(rec);
        records.push_back(std::move(rec));
        ++added;
      }

      if (static_cast<int>(records.size()) >= maxBooks) break;
    }
    rootFile.close();
  }

  for (size_t i = 0; i < cached.size(); ++i) {
    if (!cachedMatched[i]) {
      ++removed;
      LOG_DBG("BSC", "sync: removed stale entry: %s", cached[i].path.c_str());
    }
  }

  if (removed == 0 && added == 0) {
    out.swap(cached);
    LOG_DBG("BSC", "sync: no changes detected (%d entries, %d sd files scanned)", kept, sdFileCount);
    return true;
  }

  std::sort(records.begin(), records.end(), compareRecords);

  out.reserve(records.size());
  for (auto& rec : records) {
    Entry entry;
    entry.path = std::move(rec.path);
    entry.title = std::move(rec.title);
    entry.author = std::move(rec.author);
    out.push_back(std::move(entry));
  }

  const bool persisted = save(out);
  LOG_DBG("BSC", "sync complete: %d kept, %d added, %d removed (total %d, %d sd scanned), persisted=%d", kept, added,
          removed, static_cast<int>(out.size()), sdFileCount, persisted ? 1 : 0);
  return persisted;
}

// Full scan: walk SD on-the-fly (no pre-accumulated path vector),
// parse metadata for every book, sort with pre-normalized keys, persist.
bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out, const char* rootDir, int maxBooks) {
  out.clear();
  if (maxBooks <= 0) return true;

  // Count total candidates via a lightweight pass (only string copies, no EPUB parsing)
  std::vector<std::string> paths;
  paths.reserve(128);
  enumerateBooks(paths, rootDir, maxBooks);
  const int totalCandidates = static_cast<int>(paths.size());

  emitProgress(renderer, popupRect, 0, totalCandidates);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(totalCandidates, maxBooks));

  int processed = 0;
  for (auto& fullPath : paths) {
    ++processed;
    if (static_cast<int>(records.size()) >= maxBooks) {
      if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
      continue;
    }

    // Yield and reset WDT before parsing each book, since EPUB load
    // can be both memory-heavy and slow on the ESP32-C3 (~380 KB heap).
    yield();
    esp_task_wdt_reset();

    ScanRecord rec;
    rec.path = std::move(fullPath);
    extractBookMetadata(rec);
    finalizeRecord(rec);
    records.push_back(std::move(rec));

    if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
  }
  emitProgress(renderer, popupRect, totalCandidates, totalCandidates);

  // paths vector can be freed here (it was moved into rec.path)
  paths.clear();
  paths.shrink_to_fit();

  std::sort(records.begin(), records.end(), compareRecords);

  out.reserve(records.size());
  for (auto& rec : records) {
    Entry entry;
    entry.path = std::move(rec.path);
    entry.title = std::move(rec.title);
    entry.author = std::move(rec.author);
    out.push_back(std::move(entry));
  }

  const bool persisted = save(out);
  LOG_DBG("BSC", "Scan complete: %d books (of %d candidates), persisted=%d", static_cast<int>(out.size()),
          totalCandidates, persisted ? 1 : 0);
  return persisted;
}

}  // namespace LibraryCache