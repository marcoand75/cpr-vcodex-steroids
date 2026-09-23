#include "LibraryCoverHelper.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Xtc.h>
#include <ZipFile.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "../../home/BookContextMenuActivity.h"
#include "LibraryDrawHelpers.h"
#include "SdCardFontGlobals.h"
#include "components/LibraryCache.h"
#include "components/LibraryIndex.h"
#include "fontIds.h"
#include "util/BookFilter.h"
#include "util/EpubCoverThumb.h"
#include "util/FsFileCompat.h"

void LibraryCoverHelper::deleteLibraryCovers(const std::string& bookPath, int coverWidth, int coverHeight) {
  std::string thumbPath = LibraryCache::thumbPathFor(bookPath, coverWidth, coverHeight);
  LOG_DBG("LIB", "DelCovers: single path=%s thumb=%s exists=%d", bookPath.c_str(), thumbPath.c_str(),
          !thumbPath.empty() && Storage.exists(thumbPath.c_str()));
  if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
    LOG_DBG("LIB", "DelCovers: removed %s", thumbPath.c_str());
  }
}

void LibraryCoverHelper::deletePageCovers(int gridsPerPage, const LibraryIndex::BookRef* pageCache, int coverWidth,
                                          int coverHeight) {
  int slotCount = gridsPerPage;
  LOG_DBG("LIB", "DelCovers: page range [0..%d) total=%d grids=%d", slotCount, gridsPerPage, coverWidth);
  for (int i = 0; i < slotCount && pageCache[i].id != 0; ++i) {
    std::string thumbPath = LibraryIndex::thumbPathFor(std::string(pageCache[i].path), coverWidth, coverHeight);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      Storage.remove(thumbPath.c_str());
      LOG_DBG("LIB", "DelCovers: removed [%d] %s -> %s", i, pageCache[i].path, thumbPath.c_str());
    }
  }
}

void LibraryCoverHelper::deleteAllLibraryCovers(int coverWidth, int coverHeight) {
  LOG_DBG("LIB", "DelCovers: ALL");
  int pg = 0;
  LibraryIndex::BookRef buf[32];
  while (true) {
    int n = LibraryIndex::queryPage(buf, pg, 32, LibraryIndex::SortMode::TITLE_ASC);
    if (n == 0) break;
    for (int i = 0; i < n; ++i) {
      std::string thumbPath = LibraryIndex::thumbPathFor(std::string(buf[i].path), coverWidth, coverHeight);
      if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
        Storage.remove(thumbPath.c_str());
      }
    }
    ++pg;
  }
}

bool LibraryCoverHelper::writeTextFallbackCover(GfxRenderer& renderer, const std::string& path, int coverWidth,
                                                int coverHeight) {
  if (path.empty() || coverWidth <= 0 || coverHeight <= 0) return false;
  if (ESP.getMaxAllocHeap() < 24 * 1024 || ESP.getFreeHeap() < 28 * 1024) {
    LOG_DBG("LIB", "CovGen: text cover SKIP low heap free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }

  const std::string thumbPath = LibraryIndex::thumbPathFor(path, coverWidth, coverHeight);
  if (thumbPath.empty()) return false;
  const size_t slash = thumbPath.find_last_of('/');
  if (slash != std::string::npos && !Storage.exists(thumbPath.substr(0, slash).c_str())) {
    Storage.mkdir(thumbPath.substr(0, slash).c_str());
  }
  std::string title = book_filter::filenameWithoutExtension(path);

  const int w = coverWidth;
  const int h = coverHeight;
  const int rowBytesFb = renderer.getDisplayWidthBytes();
  const size_t scratchBytes = static_cast<size_t>(rowBytesFb) * static_cast<size_t>(h);
  std::vector<uint8_t> scratch(scratchBytes);
  if (scratch.size() < scratchBytes) return false;

  {
    GfxStripTargetScope stripScope(renderer, scratch.data(), 0, h);
    renderer.fillRect(0, 0, w, h, true);
    renderer.drawRect(1, 1, w - 2, h - 2, false);

    constexpr int kPad = 6;
    constexpr int kMaxLines = 4;
    std::string t = title;
    const int maxLineW = w - 2 * kPad;

    int titleFont = SMALL_FONT_ID;
    bool hasNonLatin = false;
    for (unsigned char ch : title) {
      if (ch >= 0x80) {
        hasNonLatin = true;
        break;
      }
    }
    if (hasNonLatin) {
      const int cjkId = sdFontSystem.ensureCjkFontLoaded(renderer, title.c_str());
      if (cjkId > 0) titleFont = cjkId;
      const auto& fontMap = renderer.getFontMap();
      auto it = fontMap.find(titleFont);
      if (it == fontMap.end()) {
        LOG_DBG("LIB", "CovGen: text cover skipped - font %d not found for %s", titleFont, path.c_str());
        return false;
      }
      const uint8_t* p = reinterpret_cast<const uint8_t*>(title.c_str());
      bool missing = false;
      while (*p && !missing) {
        uint32_t cp = 0;
        if (*p < 0x80) {
          cp = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
          cp = (*p++ & 0x1F) << 6;
          cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
          cp = (*p++ & 0x0F) << 12;
          cp |= (*p++ & 0x3F) << 6;
          cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
          cp = (*p++ & 0x07) << 18;
          cp |= (*p++ & 0x3F) << 12;
          cp |= (*p++ & 0x3F) << 6;
          cp |= (*p++ & 0x3F);
        } else {
          ++p;
          continue;
        }
        if (cp >= 0x80 && !it->second.hasCodepoint(cp, EpdFontFamily::BOLD) &&
            !it->second.hasCodepoint(cp, EpdFontFamily::REGULAR)) {
          missing = true;
        }
      }
      if (missing) {
        LOG_DBG("LIB", "CovGen: text cover skipped - font id %d lacks glyphs for %s", titleFont, path.c_str());
        return false;
      }
    }

    const int lh = renderer.getLineHeight(titleFont);
    const auto lines = renderer.wrappedText(titleFont, t.c_str(), w - 2 * kPad, kMaxLines, EpdFontFamily::BOLD);
    const int blockH = static_cast<int>(lines.size()) * lh;
    int ty = (h - blockH) / 2;
    if (ty < 4) ty = 4;
    for (const auto& ln : lines) {
      const int tw = renderer.getTextWidth(titleFont, ln.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(titleFont, (w - tw) / 2, ty, ln.c_str(), false, EpdFontFamily::BOLD);
      ty += lh;
    }
  }

  const int bmpRow = (w + 7) / 8;
  const int padRow = (bmpRow + 3) & ~3;
  const int imageSize = padRow * h;
  const uint32_t fileSize = 62u + static_cast<uint32_t>(imageSize);

  FsFile out;
  if (!Storage.openFileForWrite("LIB", thumbPath, out)) return false;

  auto write32 = [&out](uint32_t v) {
    out.write(static_cast<uint8_t>(v & 0xFF));
    out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.write(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.write(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto write16 = [&out](uint16_t v) {
    out.write(static_cast<uint8_t>(v & 0xFF));
    out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
  };

  out.write('B');
  out.write('M');
  write32(fileSize);
  write32(0);
  write32(62);
  write32(40);
  write32(static_cast<uint32_t>(w));
  write32(static_cast<uint32_t>(0xFFFFFFFFu - h + 1));
  write16(1);
  write16(1);
  write32(0);
  write32(static_cast<uint32_t>(imageSize));
  write32(2835);
  write32(2835);
  write32(2);
  write32(2);
  const uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (uint8_t p : palette) out.write(p);

  std::vector<uint8_t> rowBuf(padRow, 0);
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = scratch.data() + static_cast<size_t>(y) * rowBytesFb;
    std::fill(rowBuf.begin(), rowBuf.end(), 0);
    std::memcpy(rowBuf.data(), src, bmpRow);
    out.write(rowBuf.data(), padRow);
  }
  out.close();

  if (!Storage.exists(thumbPath.c_str())) {
    Storage.remove(thumbPath.c_str());
    return false;
  }
  LOG_DBG("LIB", "CovGen: text cover OK %s (%dx%d)", path.c_str(), w, h);
  return true;
}

bool LibraryCoverHelper::generatePageCover(GfxRenderer& renderer, const std::string& path, int coverWidth,
                                           int coverHeight) {
  const std::string thumbPath = LibraryIndex::thumbPathFor(path, coverWidth, coverHeight);
  if (thumbPath.empty()) return false;

  char cacheDir[64] = {};
  if (FsHelpers::hasEpubExtension(path)) {
    const uint64_t hash = ZipFile::fnvHash64(path.c_str(), path.size());
    snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", static_cast<unsigned long long>(hash));
  } else if (FsHelpers::hasXtcExtension(path)) {
    const unsigned long long hash = static_cast<unsigned long long>(std::hash<std::string>{}(path));
    snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/xtc_%llu", hash);
  } else if (!FsHelpers::hasTxtExtension(path) && !FsHelpers::hasMarkdownExtension(path)) {
    LOG_DBG("LIB", "CovGen: unsupported extension, cover skipped: %s", path.c_str());
    return false;
  }
  if (cacheDir[0] && !Storage.exists(cacheDir)) Storage.mkdir(cacheDir);

  if (FsHelpers::hasEpubExtension(path)) {
    if (ESP.getMaxAllocHeap() < 32 * 1024) {
      LOG_DBG("LIB", "CovGen: EPUB SKIP low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    Epub epub(path, "/.crosspoint");
    if (!epub.load(true, true)) {
      LOG_DBG("LIB", "CovGen: EPUB load FAIL %s", path.c_str());
      return false;
    }
    if (ESP.getMaxAllocHeap() < 28 * 1024) {
      LOG_DBG("LIB", "CovGen: EPUB SKIP post-load low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    const bool ok = epub_cover_thumb::generate(epub, path, coverWidth, coverHeight);
    if (ok) {
      LOG_DBG("LIB", "CovGen: EPUB thumb gen=1 path=%s heap=%u maxA=%u", path.c_str(), ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      return true;
    }
    LOG_DBG("LIB", "CovGen: EPUB no cover -> text fallback path=%s heap=%u maxA=%u", path.c_str(), ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return writeTextFallbackCover(renderer, path, coverWidth, coverHeight);
  }

  if (FsHelpers::hasXtcExtension(path)) {
    if (ESP.getFreeHeap() < 20000) return false;
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) return false;
    const bool ok = xtc.generateThumbBmp(coverWidth, coverHeight);
    LOG_DBG("LIB", "CovGen: XTC thumb gen=%d path=%s heap=%u maxA=%u", ok ? 1 : 0, path.c_str(), ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return ok;
  }

  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    if (ESP.getMaxAllocHeap() < 24 * 1024 || ESP.getFreeHeap() < 28 * 1024) return false;
    const bool fb = writeTextFallbackCover(renderer, path, coverWidth, coverHeight);
    LOG_DBG("LIB", "CovGen: TXT text cover gen=%d path=%s", fb ? 1 : 0, path.c_str());
    return fb;
  }

  return false;
}
