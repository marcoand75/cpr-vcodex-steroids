#pragma once

#include <string>

class Epub;

// Adaptive EPUB cover thumbnail for the Library grid and batch cover builder.
//
// Steroids implemented this by reworking Epub::generateAdaptiveThumbBmp and the
// whole thumbnail pipeline inside lib/Epub. To keep upstream lib/Epub untouched
// (cheap upstream merges) this util instead composes the public upstream API:
//   Epub::generateThumbBmp(w, h)  -> writes <epub cache>/thumb_WxH.bmp
// and copies the result to the Library's own thumbnail path.
namespace epub_cover_thumb {

// Generates the cover thumbnail for the EPUB at |bookPath| at width x height and
// stores it at LibraryIndex::thumbPathFor(bookPath, width, height).
// Returns true when the thumbnail exists (generated or already cached).
bool generate(Epub& epub, const std::string& bookPath, int width, int height);

}  // namespace epub_cover_thumb
