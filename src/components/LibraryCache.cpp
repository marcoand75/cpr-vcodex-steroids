#include "LibraryCache.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

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
};

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

// Normalize a single char for sort comparison: strip accents then lowercase.
char normalizeChar(unsigned char c) {
  // Map common accented uppercase to base uppercase, then lowercase below.
  switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'a';  // ÀÁÂÃÄÅ
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'e';  // ÈÉÊË
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'i';  // ÌÍÎÏ
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'o';  // ÒÓÔÕÖ
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'u';  // ÙÚÛÜ
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';  // àáâãäå
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';  // èéêë
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';  // ìíîï
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';  // òóôõö
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';  // ùúûü
    case 0xD1: case 0xF1: return 'n';  // Ññ
    case 0xC7: case 0xE7: return 'c';  // Çç
    default: break;
  }
  return static_cast<char>(std::tolower(c));
}

std::string normalizeForSort(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    out.push_back(normalizeChar(static_cast<unsigned char>(s[i])));
  }
  return out;
}

bool compareRecords(const ScanRecord& a, const ScanRecord& b) {
  const std::string aa = normalizeForSort(a.author.empty() ? "zzz" : a.author);
  const std::string ba = normalizeForSort(b.author.empty() ? "zzz" : b.author);
  if (aa != ba) return aa < ba;
  return normalizeForSort(a.title) < normalizeForSort(b.title);
}

void enumerateBooks(std::vector<std::string>& outPaths, int maxBooks) {
  std::vector<std::string> worklist;
  worklist.reserve(8);
  worklist.emplace_back("/");

  while (!worklist.empty() && static_cast<int>(outPaths.size()) < maxBooks * 4) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();

    HalFile root = Storage.open(folder.c_str());
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      continue;
    }
    root.rewindDirectory();

    char name[500];
    for (HalFile file = root.openNextFile(); file; file = root.openNextFile()) {
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
      if (childPath.empty() || childPath.back() != '/') childPath.push_back('/');
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
    root.close();
  }
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
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (!epub.load(true, true)) return false;
    return epub.generateThumbBmp(coverW, coverH);
  }
  if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) return false;
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

bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out, int maxBooks) {
  out.clear();
  if (maxBooks <= 0) return true;

  std::vector<std::string> paths;
  paths.reserve(64);
  enumerateBooks(paths, maxBooks);

  const int totalCandidates = static_cast<int>(paths.size());
  emitProgress(renderer, popupRect, 0, totalCandidates);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(totalCandidates, maxBooks));

  int processed = 0;
  for (const std::string& fullPath : paths) {
    ++processed;
    if (static_cast<int>(records.size()) >= maxBooks) {
      if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
      continue;
    }

    ScanRecord rec;
    rec.path = fullPath;

    if (FsHelpers::hasEpubExtension(fullPath)) {
      Epub epub(fullPath, "/.crosspoint");
      if (!epub.load(true, true)) {
        if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
        continue;
      }
      rec.title = epub.getTitle();
      rec.author = epub.getAuthor();
    } else if (FsHelpers::hasXtcExtension(fullPath)) {
      Xtc xtc(fullPath, "/.crosspoint");
      if (!xtc.load()) {
        if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
        continue;
      }
      rec.title = xtc.getTitle();
      rec.author = xtc.getAuthor();
    } else {
      rec.title.clear();
      rec.author.clear();
    }

    if (rec.title.empty()) {
      const auto slash = fullPath.find_last_of('/');
      const auto dot = fullPath.find_last_of('.');
      const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
      const size_t end = (dot == std::string::npos || dot < start) ? fullPath.size() : dot;
      rec.title = fullPath.substr(start, end - start);
    }

    records.push_back(std::move(rec));

    if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
  }
  emitProgress(renderer, popupRect, totalCandidates, totalCandidates);

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
