#include "LibraryCache.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <CoverDebugLog.h>
#include <HomepageDebugLog.h>
#include <PngToBmpConverter.h>
#include <Txt.h>
#include <Xtc.h>
#include <ZipFile.h>
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

  // Read the 10 strings: we only keep the first 2 (title, author), skip the rest.
  // Each string: u32 length + data bytes.
  auto readAndSkip = [&file](std::string* keep, int skipCount) -> bool {
    for (int i = 0; i < skipCount; ++i) {
      uint32_t len;
      if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { return false; }
      if (keep && i == 0) {
        // First string in this batch: we want to read it
        if (file.read(reinterpret_cast<uint8_t*>(keep->data()), len) != static_cast<int>(len)) { return false; }
      } else {
        if (file.seekCur(len) < 0) { return false; }
      }
    }
    return true;
  };

  // String 0: title (keep it)
  {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    outTitle.resize(len);
    if (file.read(reinterpret_cast<uint8_t*>(&outTitle[0]), len) != static_cast<int>(len)) { file.close(); return false; }
  }
  // String 1: author (keep it)
  {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    outAuthor.resize(len);
    if (file.read(reinterpret_cast<uint8_t*>(&outAuthor[0]), len) != static_cast<int>(len)) { file.close(); return false; }
  }
  // Strings 2-9 (language, publisher, description, publicationDate, identifier,
  // subject, rights, coverItemHref): skip 8 strings
  for (int i = 0; i < 8; ++i) {
    uint32_t len;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    if (file.seekCur(len) < 0) { file.close(); return false; }
  }

  file.close();
  return true;
}

// Minimal EPUB metadata reader: reads only title and author directly from the
// EPUB ZIP, without using expat XML parser or requiring book.bin cache.
// This avoids the heap fragmentation caused by XML_GetBuffer() allocations
// in the full EPUB load path.
//
// Flow:
// 1. Read META-INF/container.xml to find content.opf path
// 2. Read content.opf into a buffer
// 3. Search for <dc:title> and <dc:creator> tags with simple string matching
//
// Memory: only 2 temporary heap allocations (container.xml + content.opf buffers),
// both freed immediately after parsing. Much lighter than expat-based parsing.
static bool readTitleAndAuthorFromEpub(const std::string& epubPath, std::string& outTitle, std::string& outAuthor) {
  ZipFile zip(epubPath);

  // 1. Read container.xml to find content.opf path
  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize)) return false;
  if (containerSize == 0 || containerSize > 8192) return false;  // sanity check

  uint8_t* containerData = zip.readFileToMemory("META-INF/container.xml", &containerSize);
  if (!containerData) return false;

  // Find full-path="..." in container.xml
  std::string contentOpfPath;
  const char* fpAttr = strstr((const char*)containerData, "full-path=\"");
  if (fpAttr) {
    fpAttr += 11;  // skip 'full-path="'
    const char* fpEnd = strchr(fpAttr, '"');
    if (fpEnd) {
      contentOpfPath.assign(fpAttr, fpEnd - fpAttr);
    }
  }
  free(containerData);
  containerData = nullptr;

  if (contentOpfPath.empty()) return false;

  // Normalize the path (resolve relative paths like "../OEBPS/content.opf")
  contentOpfPath = FsHelpers::normalisePath(contentOpfPath);

  // 2. Read content.opf
  size_t opfSize = 0;
  if (!zip.getInflatedFileSize(contentOpfPath.c_str(), &opfSize)) return false;
  if (opfSize == 0) return false;
  // Limit to 64KB to avoid large heap allocations on fragmented heap.
  // 32KB was too restrictive for EPUBs with many spine items (500+)
  // where the OPF file can exceed this limit.
  if (opfSize > 64 * 1024) opfSize = 64 * 1024;

  uint8_t* opfData = zip.readFileToMemory(contentOpfPath.c_str(), &opfSize);
  if (!opfData) return false;

  const char* opfStr = (const char*)opfData;
  const char* opfEnd = opfStr + opfSize;

  // 3. Find <dc:title...>...</dc:title> and <dc:creator...>...</dc:creator>
  auto findDcTag = [opfStr, opfEnd](const char* localName, std::string& out) -> bool {
    char tagPattern[32];
    snprintf(tagPattern, sizeof(tagPattern), "dc:%s", localName);
    const size_t patternLen = strlen(tagPattern);

    const char* pos = opfStr;

    while (pos < opfEnd) {
      const char* tagStart = strstr(pos, tagPattern);
      if (!tagStart || tagStart >= opfEnd) break;

      // Verify it's an opening tag: preceded by '<'
      if (tagStart == opfStr || *(tagStart - 1) != '<') {
        pos = tagStart + patternLen;
        continue;
      }

      // Find end of opening tag '>'
      const char* openEnd = tagStart + patternLen;
      while (openEnd < opfEnd && *openEnd != '>') openEnd++;
      if (openEnd >= opfEnd) break;
      openEnd++;  // skip '>'

      // Build closing tag: "</dc:localName>"
      char closePattern[40];
      snprintf(closePattern, sizeof(closePattern), "</dc:%s>", localName);

      // Find closing tag
      const char* closePos = strstr(openEnd, closePattern);
      if (!closePos || closePos >= opfEnd) {
        pos = openEnd;
        continue;
      }

      // Extract content
      out.assign(openEnd, closePos - openEnd);

      // Trim whitespace
      while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r' || out.back() == '\t'))
        out.pop_back();
      size_t start = 0;
      while (start < out.size() && (out[start] == ' ' || out[start] == '\n' || out[start] == '\r' || out[start] == '\t'))
        start++;
      if (start > 0) out = out.substr(start);

      return !out.empty();
    }
    return false;
  };

  findDcTag("title", outTitle);
  findDcTag("creator", outAuthor);

  free(opfData);

  return !outTitle.empty() || !outAuthor.empty();
}

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
static void normalizeInPlace(std::string& s) {
  // Lowercase + strip common Latin accents in-place.  Avoids allocating a
  // temporary output string per call, which eliminates ~2 alloc/free cycles
  // per book during sync — the dominant source of heap fragmentation.
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

  // Cap recursion depth.  A malformed card with circular symlinks or deeply
  // nested directories could otherwise push thousands of paths and peak
  // hundreds of KB of RAM.  8 levels is plenty for any realistic library
  // layout (genre/author/series/title).
  constexpr int kMaxDepth = 8;
  std::vector<uint8_t> depth;
  depth.push_back(0);

  int dirCount = 0;
  // Only collect up to maxBooks candidates: scan() can only index maxBooks
  // entries anyway, and collecting thousands of extra path strings needlessly
  // peaks RAM on the constrained ESP32-C3 (was maxBooks * 8, which OOM'd large
  // libraries mid-scan).
  while (!worklist.empty() && static_cast<int>(outPaths.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();
    const uint8_t folderDepth = depth.back();
    depth.pop_back();

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
        // Refuse to descend past kMaxDepth. The path may still be opened by
        // the user later, but we keep the indexing walk bounded.
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

// Helper: extract EPUB metadata, trying the persistent cache first
// to avoid opening the ZIP when the book was previously read.
// Returns true if metadata was successfully extracted, false on OOM/failure.
// On any failure the record is left with empty title/author and the caller
// falls back to the filename-derived title.
bool extractBookMetadata(ScanRecord& rec) {
  rec.title.clear();
  rec.author.clear();

  // Defensive sanity check: the path must point to an existing non-empty
  // regular file.  A corrupt FAT entry or stale path can otherwise crash
  // the underlying open() call inside the parser.
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
    // Scope-limit the Epub object so it is destroyed before we move to
    // the next book, releasing ZIP buffer / page data immediately.
    bool haveMeta = false;
    {
      // Diagnostic: log heap fragmentation state before metadata extraction
      // to help debug the MinFree=8432 issue on real hardware.
      COVER_LOG("BSC", "pre-extract(%s): free=%u maxA=%u largest=%u",
                rec.path.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

      // First, try to read title+author directly from the EPUB ZIP without
      // using expat XML parser or requiring book.bin cache. This is the
      // lightest approach: only 2 temporary heap allocations (container.xml
      // + content.opf buffers), both freed immediately after parsing.
      if (readTitleAndAuthorFromEpub(rec.path, rec.title, rec.author)) {
        haveMeta = !rec.title.empty() || !rec.author.empty();
      }

      // If direct ZIP read failed, try reading from book.bin cache (if it exists).
      // This is also lightweight but requires the cache to have been built previously.
      if (!haveMeta) {
        Epub epub(rec.path, "/.crosspoint");
        const std::string& cacheDir = epub.getCachePath();
        if (Storage.exists(cacheDir.c_str())) {
          if (readTitleAndAuthor(cacheDir, rec.title, rec.author)) {
            haveMeta = !rec.title.empty() || !rec.author.empty();
          }
        }
      }
    }  // Epub destroyed here — release ZIP buffer / page data immediately
  } else if (FsHelpers::hasXtcExtension(rec.path)) {
    // XTC load is cheaper than EPUB but still allocates a per-page table.
    // Guard against fragmentation on the constrained ESP32-C3.
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
    // TXT/Markdown: Txt::load is cheap (it only counts lines), but
    // still skip on very low heap to leave room for the next book.
    if (ESP.getFreeHeap() < 16000) {
      LOG_DBG("BSC", "Skipping TXT parse for %s (free heap %u < 16 KB)", rec.path.c_str(), ESP.getFreeHeap());
      return false;
    }
    Txt txt(rec.path, "/.crosspoint");
    if (txt.load()) {
      rec.title = txt.getTitle();
      // Txt has no getAuthor() — leave author empty; UI falls back to filename.
    }
  }
  // TXT/Markdown have no embedded metadata by default; title will be
  // derived from the filename below when empty.

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

// Minimal EPUB cover extractor: finds and extracts cover image directly from
// the EPUB ZIP, without using expat XML parser or requiring book.bin cache.
// This avoids the heap fragmentation caused by XML_GetBuffer() allocations.
//
// Flow:
// 1. Read META-INF/container.xml to find content.opf path
// 2. Read content.opf into a buffer
// 3. Search for cover image reference using string matching:
//    - <meta name="cover" content="ID"/> → then find <item id="ID" href="..."/>
//    - Or <item properties="cover-image" href="..."/> (EPUB3)
// 4. Extract cover image from ZIP
// 5. Decode (JPEG/PNG) and convert to BMP thumbnail
//
// Memory: only 2-3 temporary heap allocations (container.xml + content.opf + image),
// all freed immediately after processing. Much lighter than expat-based parsing.
static bool extractCoverFromEpub(const std::string& epubPath, int coverW, int coverH) {
  ZipFile zip(epubPath);

  // 1. Read container.xml to find content.opf path
  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize)) return false;
  if (containerSize == 0 || containerSize > 8192) return false;

  uint8_t* containerData = zip.readFileToMemory("META-INF/container.xml", &containerSize);
  if (!containerData) return false;

  // Find full-path="..." in container.xml
  std::string contentOpfPath;
  const char* fpAttr = strstr((const char*)containerData, "full-path=\"");
  if (fpAttr) {
    fpAttr += 11;
    const char* fpEnd = strchr(fpAttr, '"');
    if (fpEnd) {
      contentOpfPath.assign(fpAttr, fpEnd - fpAttr);
    }
  }
  free(containerData);
  containerData = nullptr;

  if (contentOpfPath.empty()) return false;
  contentOpfPath = FsHelpers::normalisePath(contentOpfPath);

  // Compute base path for resolving relative hrefs
  std::string basePath;
  const size_t lastSlash = contentOpfPath.find_last_of('/');
  if (lastSlash != std::string::npos) {
    basePath = contentOpfPath.substr(0, lastSlash + 1);
  }

  // 2. Read content.opf
  size_t opfSize = 0;
  if (!zip.getInflatedFileSize(contentOpfPath.c_str(), &opfSize)) return false;
  if (opfSize == 0) return false;
  if (opfSize > 32 * 1024) opfSize = 32 * 1024;

  uint8_t* opfData = zip.readFileToMemory(contentOpfPath.c_str(), &opfSize);
  if (!opfData) return false;

  const char* opfStr = (const char*)opfData;
  const char* opfEnd = opfStr + opfSize;

  // 3. Find cover image href
  std::string coverImageHref;

  // Strategy 1: Look for <meta name="cover" content="ID"/>
  const char* metaCover = strstr(opfStr, "name=\"cover\"");
  if (metaCover && metaCover < opfEnd) {
    // Find the enclosing <meta> tag
    const char* tagStart = metaCover;
    while (tagStart > opfStr && *(tagStart - 1) != '<') tagStart--;
    const char* tagEnd = metaCover;
    while (tagEnd < opfEnd && *tagEnd != '>') tagEnd++;
    
    // Find content="ID" within this tag
    const char* contentAttr = strstr(tagStart, "content=\"");
    if (contentAttr && contentAttr < tagEnd) {
      contentAttr += 9;
      const char* contentEnd = strchr(contentAttr, '"');
      if (contentEnd && contentEnd <= tagEnd) {
        std::string coverId(contentAttr, contentEnd - contentAttr);
        
        // Now find <item id="ID" href="..."/>
        char idPattern[128];
        snprintf(idPattern, sizeof(idPattern), "id=\"%s\"", coverId.c_str());
        const char* itemTag = strstr(opfStr, idPattern);
        if (itemTag && itemTag < opfEnd) {
          // Find href="..." within this item tag
          const char* itemStart = itemTag;
          while (itemStart > opfStr && *(itemStart - 1) != '<') itemStart--;
          const char* itemEnd = itemTag;
          while (itemEnd < opfEnd && *itemEnd != '>') itemEnd++;
          
          const char* hrefAttr = strstr(itemStart, "href=\"");
          if (hrefAttr && hrefAttr < itemEnd) {
            hrefAttr += 6;
            const char* hrefEnd = strchr(hrefAttr, '"');
            if (hrefEnd && hrefEnd <= itemEnd) {
              std::string href(hrefAttr, hrefEnd - hrefAttr);
              coverImageHref = FsHelpers::normalisePath(basePath + FsHelpers::decodeUriEscapes(href));
            }
          }
        }
      }
    }
  }

  // Strategy 2: Look for <item properties="cover-image" href="..."/> (EPUB3)
  if (coverImageHref.empty()) {
    const char* propCover = strstr(opfStr, "properties=\"cover-image\"");
    if (!propCover) propCover = strstr(opfStr, "properties=\"cover-image ");
    if (!propCover) propCover = strstr(opfStr, "properties=\" cover-image\"");
    
    if (propCover && propCover < opfEnd) {
      // Find the enclosing <item> tag
      const char* tagStart = propCover;
      while (tagStart > opfStr && *(tagStart - 1) != '<') tagStart--;
      const char* tagEnd = propCover;
      while (tagEnd < opfEnd && *tagEnd != '>') tagEnd++;
      
      // Find href="..." within this tag
      const char* hrefAttr = strstr(tagStart, "href=\"");
      if (hrefAttr && hrefAttr < tagEnd) {
        hrefAttr += 6;
        const char* hrefEnd = strchr(hrefAttr, '"');
        if (hrefEnd && hrefEnd <= tagEnd) {
          std::string href(hrefAttr, hrefEnd - hrefAttr);
          coverImageHref = FsHelpers::normalisePath(basePath + FsHelpers::decodeUriEscapes(href));
        }
      }
    }
  }

  free(opfData);
  opfData = nullptr;

  if (coverImageHref.empty()) {
    LOG_DBG("BSC", "Cover: no cover image found in content.opf path=%s", epubPath.c_str());
    return false;
  }

  LOG_DBG("BSC", "Cover: found href=%s free=%u maxA=%u",
          coverImageHref.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // 4. Extract cover image from ZIP to temp file
  const unsigned long long hash = static_cast<unsigned long long>(std::hash<std::string>{}(epubPath));
  char cacheDir[64];
  snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", hash);
  
  std::string coverTempPath;
  if (FsHelpers::hasJpgExtension(coverImageHref)) {
    coverTempPath = std::string(cacheDir) + "/.cover.jpg";
  } else if (FsHelpers::hasPngExtension(coverImageHref)) {
    coverTempPath = std::string(cacheDir) + "/.cover.png";
  } else {
    COVER_LOG("BSC", "Cover: unsupported format href=%s", coverImageHref.c_str());
    return false;
  }

  // Ensure cache directory exists before writing the temp cover file.
  // mkdir returns false if the directory already exists on some FAT drivers,
  // so accept "exists" as success too.
  if (!Storage.exists(cacheDir) && !Storage.mkdir(cacheDir)) {
    LOG_DBG("BSC", "Cover: failed to create cache dir %s", cacheDir);
    return false;
  }

  // Extract image from ZIP - stream directly to file to avoid large memory allocation
  FsFile coverFile;
  if (!Storage.openFileForWrite("BSC", coverTempPath, coverFile)) {
    LOG_DBG("BSC", "Cover: failed to open temp file %s", coverTempPath.c_str());
    return false;
  }

  // Use streaming extraction (4KB chunks) instead of loading entire image into memory
  // This works much better with fragmented heap
  LOG_DBG("BSC", "Cover: extracting href=%s to=%s free=%u maxA=%u",
          coverImageHref.c_str(), coverTempPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (!zip.readFileToStream(coverImageHref.c_str(), coverFile, 4096)) {
    coverFile.close();
    Storage.remove(coverTempPath.c_str());
    LOG_DBG("BSC", "Cover: failed to extract image from ZIP href=%s", coverImageHref.c_str());
    return false;
  }
  
  size_t imageSize = coverFile.position();
  coverFile.close();

  LOG_DBG("BSC", "Cover: extracted to %s size=%u free=%u maxA=%u",
          coverTempPath.c_str(), imageSize, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // 5. Decode and convert to BMP
  const std::string thumbPath = thumbPathFor(epubPath, coverW, coverH);
  
  // Ensure the cache directory exists before writing the BMP.
  // mkdir returns false if the directory already exists on some FAT drivers,
  // so accept "exists" as success too.
  if (!Storage.exists(cacheDir) && !Storage.mkdir(cacheDir)) {
    LOG_DBG("BSC", "Cover: failed to create cache dir for BMP %s", cacheDir);
    return false;
  }

  if (FsHelpers::hasJpgExtension(coverTempPath)) {
    // Check heap for JPEG decoding
    if (ESP.getMaxAllocHeap() < 28 * 1024) {
      LOG_DBG("BSC", "Cover: insufficient heap for JPEG decode maxA=%u", ESP.getMaxAllocHeap());
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    FsFile coverJpg;
    if (!Storage.openFileForRead("BSC", coverTempPath, coverJpg)) {
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    // Write BMP to a temp file first, then rename atomically.
    // This prevents isBookCoverReady (which runs in parallel with JPEG
    // conversion across activity frames) from seeing a 0-byte file and
    // deleting it before the conversion completes.
    const std::string thumbTmpPath = thumbPath + ".tmp";
    FsFile thumbBmp;
    if (!Storage.openFileForWrite("BSC", thumbTmpPath, thumbBmp)) {
      coverJpg.close();
      Storage.remove(coverTempPath.c_str());
      LOG_DBG("BSC", "Cover: failed to open BMP temp for writing %s", thumbTmpPath.c_str());
      return false;
    }

    const bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(
        coverJpg, thumbBmp, coverW, coverH, nullptr);
    
    thumbBmp.close();

    if (success) {
      coverJpg.close();
      Storage.remove(coverTempPath.c_str());
      Storage.remove(thumbPath.c_str());
      if (Storage.rename(thumbTmpPath.c_str(), thumbPath.c_str())) {
        LOG_DBG("BSC", "Cover: JPEG thumb OK %s free=%u maxA=%u", thumbPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else {
        LOG_DBG("BSC", "Cover: JPEG thumb rename failed %s -> %s", thumbTmpPath.c_str(), thumbPath.c_str());
        Storage.remove(thumbTmpPath.c_str());
        return false;
      }
      return true;
    }

    // Full JPEG decode failed (likely OOM). Try EXIF thumbnail fallback
    // which uses far less memory (~4KB vs ~28KB for full decode).
    Storage.remove(thumbTmpPath.c_str());
    coverJpg.close();
    coverJpg = FsFile();  // re-open below

    LOG_DBG("BSC", "Cover: JPEG full decode failed, trying EXIF thumbnail free=%u maxA=%u",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    if (!Storage.openFileForRead("BSC", coverTempPath, coverJpg)) {
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    // Re-open BMP temp for the EXIF attempt
    if (!Storage.openFileForWrite("BSC", thumbTmpPath, thumbBmp)) {
      coverJpg.close();
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    const bool exifOk = JpegToBmpConverter::jpegExifThumbnailTo1BitBmpStreamWithSize(
        coverJpg, thumbTmpPath, thumbBmp, coverW, coverH, nullptr);

    coverJpg.close();
    thumbBmp.close();
    Storage.remove(coverTempPath.c_str());

    if (exifOk) {
      Storage.remove(thumbPath.c_str());
      if (Storage.rename(thumbTmpPath.c_str(), thumbPath.c_str())) {
        LOG_DBG("BSC", "Cover: EXIF thumb OK %s free=%u maxA=%u", thumbPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else {
        LOG_DBG("BSC", "Cover: EXIF thumb rename failed %s -> %s", thumbTmpPath.c_str(), thumbPath.c_str());
        Storage.remove(thumbTmpPath.c_str());
        return false;
      }
    } else {
      LOG_DBG("BSC", "Cover: EXIF thumb FAILED %s free=%u maxA=%u", thumbPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      Storage.remove(thumbTmpPath.c_str());
    }
    return exifOk;

  } else if (FsHelpers::hasPngExtension(coverTempPath)) {
    // Check heap for PNG decoding
    if (ESP.getMaxAllocHeap() < 40 * 1024) {
      COVER_LOG("BSC", "Cover: insufficient heap for PNG decode maxA=%u", ESP.getMaxAllocHeap());
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    FsFile coverPng;
    if (!Storage.openFileForRead("BSC", coverTempPath, coverPng)) {
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    FsFile thumbBmp;
    if (!Storage.openFileForWrite("BSC", thumbPath, thumbBmp)) {
      coverPng.close();
      Storage.remove(coverTempPath.c_str());
      return false;
    }

    const bool success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(
        coverPng, thumbBmp, coverW, coverH);
    
    coverPng.close();
    thumbBmp.close();
    Storage.remove(coverTempPath.c_str());

    if (success) {
      COVER_LOG("BSC", "Cover: PNG thumb OK free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    } else {
      COVER_LOG("BSC", "Cover: PNG thumb FAILED free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      Storage.remove(thumbPath.c_str());
    }
    return success;
  }

  return false;
}

bool generateCoverForBook(const std::string& path, int coverW, int coverH) {
  // Keep the main-task watchdog fed. Callers (per-page library indexing) may
  // invoke this from the activity loop with no other WDT reset, and an EPUB/XTC
  // cover decode can exceed the loop WDT timeout on the ESP32-C3 (~380 KB heap).
  yield();
  esp_task_wdt_reset();

  // Aggressive early-out: if contiguous heap is critically low, return
  // immediately without touching ZIP / SD. The caller will retry on the
  // next frame or when the user scrolls. This prevents "Failed to init
  // inflate reader" aborts and keeps the render loop responsive.
  uint32_t maxA = ESP.getMaxAllocHeap();
  if (maxA < 40 * 1024) {
    LOG_DBG("BSC", "Skipping cover gen for %s (maxAlloc=%u < 40 KB)", path.c_str(), maxA);
    return false;
  }

  if (FsHelpers::hasEpubExtension(path)) {
    // EPUB cover extraction using minimal direct ZIP reading (no expat).
    // This avoids the heap fragmentation caused by XML_GetBuffer() allocations
    // in the full EPUB load path.
    uint32_t freeH = ESP.getFreeHeap();
    maxA  = ESP.getMaxAllocHeap();
    COVER_LOG("BSC", "EPUB start: path=%s W=%d H=%d free=%u maxA=%u", path.c_str(), coverW, coverH, freeH, maxA);
    
    // Minimal heap requirement: ZIP inflate reader needs ~32KB contiguous
    // for the streaming decompression buffer. Raising the guard prevents
    // "Failed to init inflate reader" errors from ZipFile internals.
    // Books that fail here will be retried on the next pass when heap recovers.
    if (maxA < 32 * 1024 || freeH < 28 * 1024) {
      COVER_LOG("BSC", "EPUB SKIP (insufficient heap for ZIP inflate): maxA=%u < 32768 || free=%u < 28672", maxA, freeH);
      LOG_DBG("BSC", "Skipping EPUB thumb gen for %s (maxAlloc=%u < 32 KB || free=%u < 28 KB)",
              path.c_str(), maxA, freeH);
      return false;
    }

    // Use custom minimal extractor (no expat, no book.bin cache needed)
    const bool thumbOk = extractCoverFromEpub(path, coverW, coverH);
    COVER_LOG("BSC", "EPUB thumb(%s): result=%d free=%u maxA=%u",
              path.c_str(), thumbOk ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return thumbOk;
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
  // Refuse to operate when heap is critically low: the load/save round-trip
  // doubles the cache's resident size, which OOMs on the ESP32-C3 with a
  // large library.
  if (ESP.getFreeHeap() < 30000) {
    LOG_ERR("BSC", "removeBook skipped: free heap %u < 30 KB", ESP.getFreeHeap());
    return false;
  }

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

  HOMEPAGE_LOG("BSC", "sync: start heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  std::string root = rootDir ? rootDir : "";
  if (root.empty()) root = "/";
  if (root[0] != '/') root.insert(0, "/");
  while (root.size() > 1 && root.back() == '/') root.pop_back();

  std::vector<Entry> cached;
  if (!load(cached)) {
    LOG_DBG("BSC", "sync: cache not available, falling back to full scan");
    return false;
  }

  HOMEPAGE_LOG("BSC", "sync: after load(cached) heap=%u maxA=%u cached=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), cached.size());

  std::sort(cached.begin(), cached.end(),
            [](const Entry& a, const Entry& b) { return a.path < b.path; });

  std::vector<std::string> worklist;
  worklist.reserve(16);
  worklist.emplace_back(root);

  // Mirror enumerateBooks() depth cap so an attacker-supplied / pathological
  // card layout cannot blow the heap during incremental sync either.
  constexpr int kMaxDepth = 8;
  std::vector<uint8_t> depth;
  depth.push_back(0);

  std::vector<ScanRecord> records;
  records.reserve(std::min<int>(static_cast<int>(cached.size()) + 16, maxBooks));

  HOMEPAGE_LOG("BSC", "sync: after reserve(records) heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

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

  int loopIter = 0;
  while (!worklist.empty() && static_cast<int>(records.size()) < maxBooks) {
    const std::string folder = std::move(worklist.back());
    worklist.pop_back();
    const uint8_t folderDepth = depth.back();
    depth.pop_back();

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

      ++sdFileCount;
      ++loopIter;

      const int ci = cachedIndexForPath(childPath);
      if (ci >= 0) {
        cachedMatched[ci] = true;
        ScanRecord rec;
        rec.path = std::move(childPath);
        // Copy strings from cache into the ScanRecord.  All three are needed
        // for the final sort — the cache entries themselves will be released.
        rec.title = cached[ci].title;
        rec.author = cached[ci].author;
        finalizeRecord(rec);
        records.push_back(std::move(rec));
        ++kept;
      } else {
        // Newly discovered file: parse metadata, but never let a single
        // bad book take down the whole sync. extractBookMetadata already
        // logs the reason and returns false on failure; on false we
        // silently skip the book (its path won't enter records).
        ScanRecord rec;
        rec.path = std::move(childPath);
        if (extractBookMetadata(rec)) {
          finalizeRecord(rec);
          records.push_back(std::move(rec));
          ++added;
        }
      }

      // Log every 20 iterations to track heap fragmentation pattern
      if (loopIter % 20 == 0) {
        HOMEPAGE_LOG("BSC", "sync loop: iter=%d records=%zu heap=%u maxA=%u", loopIter, records.size(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      }

      if (static_cast<int>(records.size()) >= maxBooks) break;
    }
    rootFile.close();
  }

  HOMEPAGE_LOG("BSC", "sync: after scan loop heap=%u maxA=%u records=%zu kept=%d added=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), records.size(), kept, added);

  for (size_t i = 0; i < cached.size(); ++i) {
    if (!cachedMatched[i]) {
      ++removed;
      LOG_DBG("BSC", "sync: removed stale entry: %s", cached[i].path.c_str());
    }
  }

  if (removed == 0 && added == 0) {
    out.swap(cached);
    HOMEPAGE_LOG("BSC", "sync: no changes, after swap heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    LOG_DBG("BSC", "sync: no changes detected (%d entries, %d sd files scanned)", kept, sdFileCount);
    return true;
  }

  std::sort(records.begin(), records.end(), compareRecords);

  HOMEPAGE_LOG("BSC", "sync: before copy records->out heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  out.reserve(records.size());
  for (auto& rec : records) {
    Entry entry;
    entry.path = std::move(rec.path);
    entry.title = std::move(rec.title);
    entry.author = std::move(rec.author);
    out.push_back(std::move(entry));
  }

  HOMEPAGE_LOG("BSC", "sync: after copy records->out heap=%u maxA=%u out=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), out.size());

  // Explicitly release cached and records to see the effect
  cached.clear();
  cached.shrink_to_fit();
  HOMEPAGE_LOG("BSC", "sync: after cached.clear() heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  records.clear();
  records.shrink_to_fit();
  HOMEPAGE_LOG("BSC", "sync: after records.clear() heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  const bool persisted = save(out);
  HOMEPAGE_LOG("BSC", "sync: after save() heap=%u maxA=%u persisted=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), persisted ? 1 : 0);
  LOG_DBG("BSC", "sync complete: %d kept, %d added, %d removed (total %d, %d sd scanned), persisted=%d. post-sync heap: free=%u maxA=%u largest=%u minFree=%u",
          kept, added, removed, static_cast<int>(out.size()), sdFileCount, persisted ? 1 : 0,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT), ESP.getMinFreeHeap());
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
  int skipped = 0;
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

    // Pre-flight heap check: if we're below the safety threshold, stop
    // indexing rather than risk OOM inside the parser.
    const auto heap = MemoryBudget::snapshot();
    constexpr uint32_t kScanMinFree = 50000;   // ~50 KB
    constexpr uint32_t kScanMinMaxAlloc = 32000;
    if (heap.freeHeap < kScanMinFree || heap.maxAllocHeap < kScanMinMaxAlloc) {
      LOG_ERR("BSC", "Stopping scan: heap too low (free=%u maxAlloc=%u) after %d books",
              heap.freeHeap, heap.maxAllocHeap, processed - 1);
      skipped += (totalCandidates - processed + 1);
      break;
    }

    ScanRecord rec;
    rec.path = std::move(fullPath);
    if (!extractBookMetadata(rec)) {
      // extractBookMetadata logs the reason; continue to next book.
      ++skipped;
      if (processed % kProgressUpdateInterval == 0) emitProgress(renderer, popupRect, processed, totalCandidates);
      continue;
    }
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
  LOG_DBG("BSC", "Scan complete: %d books (of %d candidates, %d skipped), persisted=%d",
          static_cast<int>(out.size()), totalCandidates, skipped, persisted ? 1 : 0);
  return persisted;
}

}  // namespace LibraryCache