#include "LibraryCache.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <CoverDebugLog.h>
#include <HomepageDebugLog.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "components/UITheme.h"
#include "EpubParser.h"

namespace LibraryCache {

namespace {
constexpr const char* kCacheFile = "/.crosspoint/library.bin";
constexpr uint8_t kVersion = 2;
constexpr int kProgressUpdateInterval = 2;

struct ScanRecord {
  std::string path;
  std::string title;
  std::string author;
  std::string normTitle;
  std::string normAuthor;
};

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

bool writeU8(HalFile& file, uint8_t value) { return file.write(&value, 1) == 1; }
bool writeU16(HalFile& file, uint16_t value) {
  const uint8_t buf[2] = {static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>(value >> 8)};
  return file.write(buf, 2) == 2;
}
bool writeString(HalFile& file, const std::string& s) {
  const size_t len = s.size();
  if (len > 0xffff) return false;
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
  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);

  constexpr int kMaxDepth = 8;
  std::vector<uint8_t> depth;
  depth.push_back(0);

  int dirCount = 0;
  while (!worklist.empty() && static_cast<int>(outPaths.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();
    const uint8_t folderDepth = depth.back();
    depth.pop_back();

    if ((++dirCount & 0x7) == 0) { yield(); esp_task_wdt_reset(); }

    HalFile rootFile = Storage.open(folder.c_str());
    if (!rootFile || !rootFile.isDirectory()) { if (rootFile) rootFile.close(); continue; }
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
                    lowerName == "font" || lowerName == "fonts" || lowerName == "dictionaries" || lowerName == "exports")) continue;

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

bool extractBookMetadata(ScanRecord& rec) {
  rec.title.clear(); rec.author.clear();
  if (rec.path.empty() || rec.path[0] != '/') return false;
  
  HalFile stat = Storage.open(rec.path.c_str());
  if (!stat || stat.isDirectory() || stat.size() == 0) { if (stat) stat.close(); return false; }
  stat.close();

  if (FsHelpers::hasEpubExtension(rec.path)) {
    if (!EpubParser::extractMetadata(rec.path, "/.crosspoint", rec.title, rec.author)) {
      // Fallback handled by caller or empty title
    }
  } else if (FsHelpers::hasXtcExtension(rec.path)) {
    if (ESP.getFreeHeap() < 20000) return false;
    Xtc xtc(rec.path, "/.crosspoint");
    if (xtc.load()) { rec.title = xtc.getTitle(); rec.author = xtc.getAuthor(); }
  } else if (FsHelpers::hasTxtExtension(rec.path) || FsHelpers::hasMarkdownExtension(rec.path)) {
    if (ESP.getFreeHeap() < 16000) return false;
    Txt txt(rec.path, "/.crosspoint");
    if (txt.load()) rec.title = txt.getTitle();
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

} // namespace

std::string thumbPathFor(const std::string& path, int coverW, int coverH) {
  const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(path));
  char buf[96];
  if (FsHelpers::hasXtcExtension(path)) {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/xtc_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/txt_%llu/cover.bmp", hash);
  } else {
    std::snprintf(buf, sizeof(buf), "/.crosspoint/epub_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  }
  return buf;
}

// ============================================================================
// FIX CRITICO: Allineamento totale al sistema di failover di HomeActivity
// ============================================================================
bool generateCoverForBook(const std::string& path, int coverW, int coverH) {
  yield(); esp_task_wdt_reset();
  if (ESP.getMaxAllocHeap() < 40 * 1024) return false;

  if (FsHelpers::hasEpubExtension(path)) {
    if (ESP.getMaxAllocHeap() < 32 * 1024 || ESP.getFreeHeap() < 28 * 1024) return false;
    
    // 1. Primo tentativo: estrattore leggero ZIP-only (nessun expat, nessun book.bin)
    if (EpubParser::generateCover(path, coverW, coverH)) return true;

    // 2. Fallback: generazione stile HomeActivity (parser completo)
    Epub epub(path, "/.crosspoint");
    if (epub.load(true, true)) {
      yield();
      esp_task_wdt_reset();
      
      // Ricontrolla l'heap dopo il caricamento, poiché il parsing OPF/TOC può frammentarlo
      if (ESP.getMaxAllocHeap() < 28 * 1024) {
        LOG_DBG("BSC", "EPUB SKIP post-load (low heap): maxA=%u", ESP.getMaxAllocHeap());
        return false;
      }

      const bool genOk = epub.generateThumbBmp(coverW, coverH);
      if (genOk) {
        // VALIDAZIONE OBBLIGATORIA: verifica che il file sia stato scritto correttamente
        const std::string epubThumbPath = epub.getThumbBmpPath();
        FsFile file;
        bool isValid = false;
        if (!epubThumbPath.empty() && Storage.openFileForRead("LIB", epubThumbPath, file)) {
          Bitmap bmp(file);
          isValid = (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0);
          file.close();
        }
        
        if (!isValid) {
          LOG_DBG("BSC", "EPUB fallback generated invalid cover, removing: %s", epubThumbPath.c_str());
          if (!epubThumbPath.empty()) Storage.remove(epubThumbPath.c_str());
          return false;
        }
        return true;
      }
    }
    return false;
  }
  
  if (FsHelpers::hasXtcExtension(path)) {
    if (ESP.getFreeHeap() < 20000) return false;
    
    // Lambda per tentativo di generazione con validazione
    auto tryXtc = [&]() -> bool {
      Xtc xtc(path, "/.crosspoint");
      if (!xtc.load()) return false;
      if (!xtc.generateThumbBmp(coverW, coverH)) return false;
      
      const std::string xtcThumbPath = xtc.getThumbBmpPath();
      FsFile file;
      bool isValid = false;
      if (!xtcThumbPath.empty() && Storage.openFileForRead("LIB", xtcThumbPath, file)) {
        Bitmap bmp(file);
        isValid = (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0);
        file.close();
      }
      if (!isValid && !xtcThumbPath.empty()) {
        Storage.remove(xtcThumbPath.c_str());
      }
      return isValid;
    };

    // Primo tentativo
    if (tryXtc()) return true;
    
    // Fallback: riprova con un'istanza fresca in caso di frammentazione temporanea dell'heap
    if (tryXtc()) return true;
    
    return false;
  }
  
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    if (ESP.getFreeHeap() < 16000) return false;
    Txt txt(path, "/.crosspoint");
    if (txt.load() && txt.generateCoverBmp()) {
      const std::string txtCoverPath = txt.getCoverBmpPath();
      FsFile file;
      bool isValid = false;
      if (!txtCoverPath.empty() && Storage.openFileForRead("LIB", txtCoverPath, file)) {
        Bitmap bmp(file);
        isValid = (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0);
        file.close();
      }
      if (!isValid && !txtCoverPath.empty()) {
        Storage.remove(txtCoverPath.c_str());
      }
      return isValid;
    }
    return false;
  }
  
  return false;
}

bool exists() { return Storage.exists(kCacheFile); }

bool load(std::vector<Entry>& out) {
  out.clear();
  HalFile file;
  if (!Storage.openFileForRead("BSC", kCacheFile, file)) return false;

  uint8_t version = 0;
  if (!readU8(file, version) || version != kVersion) { file.close(); return false; }

  uint16_t count = 0;
  if (!readU16(file, count)) { file.close(); return false; }

  out.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    Entry entry;
    if (!readString(file, entry.path) || !readString(file, entry.title) || !readString(file, entry.author)) {
      file.close(); out.clear(); return false;
    }
    if (!entry.path.empty()) out.push_back(std::move(entry));
  }
  file.close();
  return true;
}

bool save(const std::vector<Entry>& entries) {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("BSC", kCacheFile, file)) return false;

  const size_t count = std::min<size_t>(entries.size(), 0xffff);
  if (!writeU8(file, kVersion) || !writeU16(file, static_cast<uint16_t>(count))) { file.close(); return false; }

  for (size_t i = 0; i < count; ++i) {
    if (!writeString(file, entries[i].path) || !writeString(file, entries[i].title) || !writeString(file, entries[i].author)) {
      file.close(); return false;
    }
  }
  file.close();
  return true;
}

void invalidate() {
  if (Storage.exists(kCacheFile)) Storage.remove(kCacheFile);
}

bool removeBook(const std::string& path) {
  if (ESP.getFreeHeap() < 30000) return false;
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
  if (!load(cached)) return false;

  std::sort(cached.begin(), cached.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);

  constexpr int kMaxDepth = 8;
  std::vector<uint8_t> depth;
  depth.push_back(0);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(static_cast<int>(cached.size()) + 16, maxBooks));

  int removed = 0, added = 0, kept = 0, sdFileCount = 0, dirCount = 0;

  auto cachedIndexForPath = [&cached](const std::string& path) -> int {
    const auto it = std::lower_bound(cached.begin(), cached.end(), path, [](const Entry& e, const std::string& p) { return e.path < p; });
    if (it != cached.end() && it->path == path) return static_cast<int>(it - cached.begin());
    return -1;
  };

  std::vector<bool> cachedMatched(cached.size(), false);
  int loopIter = 0;

  while (!worklist.empty() && static_cast<int>(records.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();
    const uint8_t folderDepth = depth.back();
    depth.pop_back();

    if ((++dirCount & 0x7) == 0) { yield(); esp_task_wdt_reset(); }

    HalFile rootFile = Storage.open(folder.c_str());
    if (!rootFile || !rootFile.isDirectory()) { if (rootFile) rootFile.close(); continue; }
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
                    lowerName == "font" || lowerName == "fonts" || lowerName == "dictionaries" || lowerName == "exports")) continue;

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
          !FsHelpers::hasTxtExtension(filename) && !FsHelpers::hasMarkdownExtension(filename)) continue;
      if (std::strcmp(name, "if_found.txt") == 0 || std::strcmp(name, "crash_report.txt") == 0) continue;

      ++sdFileCount;
      const int ci = cachedIndexForPath(childPath);
      if (ci >= 0) {
        cachedMatched[ci] = true;
        ScanRecord rec;
        rec.path = std::move(childPath);
        rec.title = cached[ci].title;
        rec.author = cached[ci].author;
        finalizeRecord(rec);
        records.push_back(std::move(rec));
        ++kept;
      } else {
        ScanRecord rec;
        rec.path = std::move(childPath);
        if (extractBookMetadata(rec)) {
          finalizeRecord(rec);
          records.push_back(std::move(rec));
          ++added;
        }
      }

      if (++loopIter % 20 == 0) yield();
      if (static_cast<int>(records.size()) >= maxBooks) break;
    }
    rootFile.close();
  }

  for (size_t i = 0; i < cached.size(); ++i) {
    if (!cachedMatched[i]) ++removed;
  }

  if (removed == 0 && added == 0) {
    out.swap(cached);
    return true;
  }

  std::sort(records.begin(), records.end(), compareRecords);
  out.reserve(records.size());
  for (auto& rec : records) {
    out.push_back(Entry{std::move(rec.path), std::move(rec.title), std::move(rec.author)});
  }

  cached.clear(); cached.shrink_to_fit();
  records.clear(); records.shrink_to_fit();

  return save(out);
}

bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out, const char* rootDir, int maxBooks) {
  out.clear();
  if (maxBooks <= 0) return true;

  std::vector<std::string> paths;
  paths.reserve(128);
  enumerateBooks(paths, rootDir, maxBooks);
  const int totalCandidates = static_cast<int>(paths.size());

  emitProgress(renderer, popupRect, 0, totalCandidates);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(totalCandidates, maxBooks));

  int processed = 0, skipped = 0;
  for (auto& fullPath : paths) {
    ++processed;
    if (static_cast<int>(records.size()) >= maxBooks) {
      if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
      continue;
    }

    yield(); esp_task_wdt_reset();
    const auto heap = MemoryBudget::snapshot();
    if (heap.freeHeap < 50000 || heap.maxAllocHeap < 32000) {
      skipped += (totalCandidates - processed + 1);
      break;
    }

    ScanRecord rec;
    rec.path = std::move(fullPath);
    if (!extractBookMetadata(rec)) {
      ++skipped;
    } else {
      finalizeRecord(rec);
      records.push_back(std::move(rec));
    }

    if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
  }
  emitProgress(renderer, popupRect, totalCandidates, totalCandidates);

  paths.clear(); paths.shrink_to_fit();
  std::sort(records.begin(), records.end(), compareRecords);

  out.reserve(records.size());
  for (auto& rec : records) {
    out.push_back(Entry{std::move(rec.path), std::move(rec.title), std::move(rec.author)});
  }

  return save(out);
}

} // namespace LibraryCache