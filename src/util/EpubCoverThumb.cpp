#include "EpubCoverThumb.h"

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include "components/LibraryIndex.h"

namespace epub_cover_thumb {

namespace {
// Stream src -> dst so no large buffer is needed (library thumbs are small).
bool copyFile(const std::string& src, const std::string& dst) {
  HalFile in;
  if (!Storage.openFileForRead("LIB", src.c_str(), in)) {
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("LIB", dst.c_str(), out)) {
    in.close();
    return false;
  }
  uint8_t buf[512];
  int n = 0;
  while ((n = in.read(buf, sizeof(buf))) > 0) {
    out.write(buf, static_cast<size_t>(n));
  }
  in.close();
  out.close();
  return true;
}
}  // namespace

bool generate(Epub& epub, const std::string& bookPath, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const std::string dst = LibraryIndex::thumbPathFor(bookPath, width, height);
  if (dst.empty()) {
    return false;
  }
  if (Storage.exists(dst.c_str())) {
    return true;
  }
  // Public upstream API: generates (and caches) the 1-bit thumbnail at the real
  // Epub cache path. The Library reads its own path, so mirror the file there.
  if (!epub.generateThumbBmp(width, height)) {
    return false;
  }
  const std::string src = epub.getThumbBmpPath(width, height);
  if (!Storage.exists(src.c_str())) {
    return false;
  }
  const auto slash = dst.find_last_of('/');
  if (slash != std::string::npos) {
    Storage.mkdir(dst.substr(0, slash).c_str());
  }
  if (!copyFile(src, dst)) {
    LOG_ERR("LIB", "Cover thumb copy failed: %s -> %s", src.c_str(), dst.c_str());
    Storage.remove(dst.c_str());
    return false;
  }
  return true;
}

}  // namespace epub_cover_thumb
