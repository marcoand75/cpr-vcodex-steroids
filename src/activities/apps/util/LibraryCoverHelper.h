#ifndef LIBRARY_COVER_HELPER_H
#define LIBRARY_COVER_HELPER_H

#include <string>

#include "components/LibraryIndex.h"

class GfxRenderer;

class LibraryCoverHelper {
 public:
  static void deleteLibraryCovers(const std::string& bookPath, int coverWidth, int coverHeight);
  static void deletePageCovers(int gridsPerPage, const LibraryIndex::BookRef* pageCache, int coverWidth,
                               int coverHeight);
  static void deleteAllLibraryCovers(int coverWidth, int coverHeight);
  // Removes zero-byte .bmp cover thumbnails from every book cache directory
  // under /.crosspoint. Returns how many files were removed.
  static int deleteCorruptCovers();
  static bool writeTextFallbackCover(GfxRenderer& renderer, const std::string& path, int coverWidth, int coverHeight);
  static bool generatePageCover(GfxRenderer& renderer, const std::string& path, int coverWidth, int coverHeight);
};

#endif  // LIBRARY_COVER_HELPER_H
