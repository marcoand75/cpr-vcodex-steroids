#include "EpubCoverThumb.h"

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include "components/LibraryIndex.h"

namespace epub_cover_thumb {

// With the steroids cover-cache alignment, LibraryIndex::thumbPathFor and
// Epub::getThumbBmpPath(w, h) point at the exact same upstream file, so there
// is nothing to mirror anymore. The only extra work kept here is dropping a
// stale/blank upstream thumbnail first: Epub::generateThumbBmp() early-returns
// when the file exists, so a zero-byte sentinel from a past failed decode
// would otherwise be served forever.
bool generate(Epub& epub, const std::string& bookPath, int width, int height) {
  (void)bookPath;
  if (width <= 0 || height <= 0) {
    return false;
  }
  const std::string dst = epub.getThumbBmpPath(width, height);
  if (dst.empty()) {
    return false;
  }
  if (Storage.exists(dst.c_str())) {
    HalFile probe;
    if (Storage.openFileForRead("LIB", dst.c_str(), probe)) {
      const bool empty = probe.fileSize() == 0;
      probe.close();
      if (!empty) {
        return true;
      }
    } else {
      return true;
    }
    // Zero-byte sentinel from a past failure: drop it so generation re-runs.
    Storage.remove(dst.c_str());
  }
  if (!epub.generateThumbBmp(width, height)) {
    return false;
  }
  if (!Storage.exists(dst.c_str())) {
    return false;
  }
  HalFile probe;
  if (!Storage.openFileForRead("LIB", dst.c_str(), probe)) {
    return false;
  }
  const bool empty = probe.fileSize() == 0;
  probe.close();
  return !empty;
}

}  // namespace epub_cover_thumb
