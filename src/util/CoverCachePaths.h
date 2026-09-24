#pragma once

// Steroids-only helper: single source of truth for cover cache locations.
//
// It mirrors the upstream (cpr-vcodex) cache-dir naming used by the Epub, Xtc
// and Txt constructors so that library covers and home-screen covers share the
// exact same files. Keep this in sync with upstream:
//   lib/Epub/Epub.h     -> cacheDir + "/epub_" + std::hash<std::string>(path)
//   lib/Xtc/Xtc.h       -> cacheDir + "/xtc_"  + std::hash<std::string>(path)
//   lib/Txt/Txt.cpp     -> cacheDir + "/txt_"  + std::hash<std::string>(path)
// Upstream thumb names: "thumb_[HEIGHT].bmp" | "thumb_<H>.bmp" | "thumb_<W>x<H>.bmp"
// (TXT has no thumbnails: "cover.bmp").
//
// Keeping this mapping in one steroids utility (instead of editing upstream
// files) keeps future upstream merges trivial.

#include <cstdio>
#include <functional>
#include <string>

#include <FsHelpers.h>

namespace cover_cache_paths {

// Cache directory for a book path, matching the upstream Epub/Xtc/Txt
// constructors exactly (std::hash on the path).
inline std::string cacheDirForBookPath(const std::string& bookPath) {
  const auto hash = static_cast<unsigned long long>(std::hash<std::string>{}(bookPath));
  if (FsHelpers::hasXtcExtension(bookPath)) {
    return "/.crosspoint/xtc_" + std::to_string(hash);
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    return "/.crosspoint/txt_" + std::to_string(hash);
  }
  return "/.crosspoint/epub_" + std::to_string(hash);
}

// Thumbnail BMP for a book path with explicit dimensions. Matches
// Epub::getThumbBmpPath(w, h) / Xtc::getThumbBmpPath(w, h). TXT has no scaled
// thumbnails: it only ever renders a full "cover.bmp".
inline std::string thumbPathForBookPath(const std::string& bookPath, int coverW, int coverH) {
  char buf[96];
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    std::snprintf(buf, sizeof(buf), "%s/cover.bmp", cacheDirForBookPath(bookPath).c_str());
    return buf;
  }
  std::snprintf(buf, sizeof(buf), "%s/thumb_%dx%d.bmp", cacheDirForBookPath(bookPath).c_str(), coverW, coverH);
  return buf;
}

// Older steroids dev builds stored thumbs with an extra "_fit" suffix that
// upstream does not have. Map any persisted legacy path back to the upstream
// name so stored metadata keeps resolving after this change.
inline std::string migrateLegacyThumbPath(std::string coverBmpPath) {
  if (coverBmpPath.empty()) {
    return coverBmpPath;
  }
  const std::string legacyToken = "thumb_[HEIGHT]_fit.bmp";
  const size_t legacyPos = coverBmpPath.find(legacyToken);
  if (legacyPos != std::string::npos) {
    coverBmpPath.replace(legacyPos, legacyToken.length(), "thumb_[HEIGHT].bmp");
    return coverBmpPath;
  }
  const std::string legacySuffix = "_fit.bmp";
  const size_t suffixPos = coverBmpPath.find(legacySuffix);
  if (suffixPos != std::string::npos) {
    coverBmpPath.replace(suffixPos, legacySuffix.length(), ".bmp");
  }
  return coverBmpPath;
}

}  // namespace cover_cache_paths
