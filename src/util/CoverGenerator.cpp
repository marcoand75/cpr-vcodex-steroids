#include "CoverGenerator.h"

#include <HalStorage.h>
#include <FsHelpers.h>
#include <Esp.h>

#include "components/LibraryIndex.h"
#include "util/CprVcodexLogs.h"

#include <ZipFile.h>
#include <Epub.h>
#include <Xtc.h>
#include <Txt.h>

namespace CoverGenerator {

bool generateCover(const std::string& bookPath, int width, int height) {
  if (bookPath.empty()) {
    return false;
  }

  const std::string thumbPath = LibraryIndex::thumbPathFor(bookPath, width, height);
  if (thumbPath.empty()) {
    return false;
  }

  // Ensure the cache directory exists (hash must match the Epub/Xtc cache path)
  char cacheDir[64];
  if (FsHelpers::hasEpubExtension(bookPath)) {
    const uint64_t hash = ZipFile::fnvHash64(bookPath.c_str(), bookPath.size());
    snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", static_cast<unsigned long long>(hash));
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    const unsigned long long hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
    snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/xtc_%llu", hash);
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    // TXT/MD files use the same cache directory as EPUB for simplicity
    const uint64_t hash = ZipFile::fnvHash64(bookPath.c_str(), bookPath.size());
    snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", static_cast<unsigned long long>(hash));
  } else {
    // Unsupported file type
    return false;
  }
  if (!Storage.exists(cacheDir)) {
    if (!Storage.mkdir(cacheDir)) {
      LOG_DBG("COVER", "Failed to create cache dir: %s", cacheDir);
      return false;
    }
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    // Check heap before loading EPUB (can be memory intensive)
    if (ESP.getMaxAllocHeap() < 32 * 1024) {
      LOG_DBG("COVER", "EPUB SKIP low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    Epub epub(bookPath, "/.crosspoint");
    if (!epub.load(true, true)) {
      LOG_DBG("COVER", "EPUB load FAIL %s", bookPath.c_str());
      return false;
    }
    // Check heap after loading
    if (ESP.getMaxAllocHeap() < 28 * 1024) {
      LOG_DBG("COVER", "EPUB SKIP post-load low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    // Adaptive contain: the resulting BMP is never larger than the tile box
    const bool ok = epub.generateAdaptiveThumbBmp(width, height);
    LOG_DBG("COVER", "EPUB thumb gen=%d path=%s heap=%u maxA=%u",
            ok ? 1 : 0, bookPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return ok;
  }

  if (FsHelpers::hasXtcExtension(bookPath)) {
    if (ESP.getFreeHeap() < 20000) {
      LOG_DBG("COVER", "XTC SKIP low free heap %u", ESP.getFreeHeap());
      return false;
    }
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load()) {
      LOG_DBG("COVER", "XTC load FAIL %s", bookPath.c_str());
      return false;
    }
    const bool ok = xtc.generateThumbBmp(width, height);
    LOG_DBG("COVER", "XTC thumb gen=%d path=%s heap=%u maxA=%u",
            ok ? 1 : 0, bookPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return ok;
  }

  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    if (!txt.load()) {
      LOG_DBG("COVER", "TXT/MD load FAIL %s", bookPath.c_str());
      return false;
    }
    const bool ok = txt.generateCoverBmp();
    LOG_DBG("COVER", "TXT/MD cover gen=%d path=%s heap=%u maxA=%u",
            ok ? 1 : 0, bookPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return ok;
  }

  // Should not reach here
  return false;
}

}  // namespace CoverGenerator