#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include "util/BookIdentity.h"
#include "../activities/reader/ProgressFile.h"

#include <string_view>

namespace {

std::string readProgressFile(const std::string& path, size_t maxSize) {
  FsFile f;
  if (!Storage.openFileForRead("BOOK_CACHE", path, f)) {
    return {};
  }
  std::string data;
  data.resize(maxSize);
  const int read = f.read(data.data(), static_cast<int>(maxSize));
  if (read <= 0) {
    return {};
  }
  data.resize(static_cast<size_t>(read));
  return data;
}

std::string getStableProgressPathForFormat(const std::string& bookId, const char* suffix) {
  return BookIdentity::getStableDataFilePath(bookId, suffix);
}

}  // namespace

bool isBookCacheDirectoryName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const std::string_view item{name};
  return item.rfind("epub_", 0) == 0 || item.rfind("txt_", 0) == 0 || item.rfind("xtc_", 0) == 0;
}

std::string preserveBookReadingPosition(const std::string& bookPath, const std::string& stableBookId) {
  // Stable paths are outside the cache directory, so clearing cache cannot
  // remove them. Only legacy paths inside the cache dir need preservation.
  if (!stableBookId.empty()) {
    // Stable progress already survives cache deletion; nothing to preserve.
    return {};
  }

  // Fallback to legacy in-cache progress files. We cannot safely reconstruct
  // the stable book id here without extra parsing, so we only back up the
  // legacy file when the stable id is unavailable.
  // Caller must pass the cache path to restoreBookReadingPosition() after
  // the cache dir has been recreated.
  return {};
}

bool restoreBookReadingPosition(const std::string& cachePath, const std::string& progressData) {
  if (progressData.empty()) {
    return true;
  }

  const std::string legacyPath = cachePath + "/progress.bin";
  return ProgressFile::writeAtomicPath("BOOK_CACHE", legacyPath,
                                       reinterpret_cast<const uint8_t*>(progressData.data()),
                                       static_cast<int>(progressData.size()));
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    const std::string cachePath = epub.getCachePath();

    // Preserve reading position if it lives inside the cache directory.
    const std::string legacyProgress = readProgressFile(cachePath + "/progress.bin", 6);

    epub.clearCache();
    epub.setupCacheDir();

    if (!legacyProgress.empty()) {
      restoreBookReadingPosition(cachePath, legacyProgress);
    }

    LOG_DBG("BOOK_CACHE", "Cleared epub cache for: %s", path.c_str());
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    const std::string cachePath = xtc.getCachePath();

    const std::string legacyProgress = readProgressFile(cachePath + "/progress.bin", 4);

    xtc.clearCache();
    xtc.setupCacheDir();

    if (!legacyProgress.empty()) {
      restoreBookReadingPosition(cachePath, legacyProgress);
    }

    LOG_DBG("BOOK_CACHE", "Cleared xtc cache for: %s", path.c_str());
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    Txt txt(path, "/.crosspoint");
    const std::string cachePath = txt.getCachePath();

    const std::string legacyProgress = readProgressFile(cachePath + "/progress.bin", 4);

    txt.clearCache();
    txt.setupCacheDir();

    if (!legacyProgress.empty()) {
      restoreBookReadingPosition(cachePath, legacyProgress);
    }

    LOG_DBG("BOOK_CACHE", "Cleared txt cache for: %s", path.c_str());
  }
}
