#include "SdCardFont.h"

#include "EpdFontFamily.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <esp_heap_caps.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>

static_assert(sizeof(EpdGlyph) == 16, "EpdGlyph must be 16 bytes to match .cpfont file layout");
static_assert(sizeof(EpdUnicodeInterval) == 12, "EpdUnicodeInterval must be 12 bytes to match .cpfont file layout");
static_assert(sizeof(EpdKernClassEntry) == 3, "EpdKernClassEntry must be 3 bytes to match .cpfont file layout");
static_assert(sizeof(EpdLigaturePair) == 8, "EpdLigaturePair must be 8 bytes to match .cpfont file layout");

// FNV-1a hash for content-based font ID generation
static constexpr uint32_t FNV_OFFSET = 2166136261u;
static constexpr uint32_t FNV_PRIME = 16777619u;

static uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t hash = FNV_OFFSET) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

// .cpfont magic bytes
static constexpr char CPFONT_MAGIC[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
// CPFONT_VERSION is defined as a #define in SdCardFont.h so it can be
// stringified into FONT_MANIFEST_URL.
static constexpr uint32_t HEADER_SIZE = 32;
static constexpr uint32_t STYLE_TOC_ENTRY_SIZE = 32;

// Helper to read little-endian values from byte buffer
static inline uint16_t readU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline int16_t readI16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }
static inline uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

// Walk a null-terminated UTF-8 string and append each unique codepoint to
// codepoints[0..cpCount-1]. Returns true when the bounded buffer is full.
static bool collectUniqueCodepoints(const char* text, uint32_t* codepoints, uint32_t& cpCount, uint32_t maxCount) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    bool found = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == cp) {
        found = true;
        break;
      }
    }
    if (!found) {
      if (cpCount >= maxCount) return true;
      codepoints[cpCount++] = cp;
    }
  }
  return false;
}

static const char* asCStr(const std::string& s) { return s.c_str(); }
static const char* asCStr(const char* s) { return s; }

SdCardFont::~SdCardFont() { freeAll(); }

// --- Per-style free/cleanup ---

void SdCardFont::freeStyleMiniData(PerStyle& s, bool freeMemory) {
  if (freeMemory) {
    delete[] s.miniIntervals;
    s.miniIntervals = nullptr;
    s.miniIntervalsCap = 0;
    delete[] s.miniGlyphs;
    s.miniGlyphs = nullptr;
    s.miniGlyphsCap = 0;
    delete[] s.miniBitmap;
    s.miniBitmap = nullptr;
    s.miniBitmapCap = 0;
  }
  s.miniIntervalCount = 0;
  s.miniGlyphCount = 0;
  freeStyleMiniKern(s, freeMemory);
  memset(&s.miniData, 0, sizeof(s.miniData));
  s.epdFont.data = &s.stubData;
}

void SdCardFont::freeStyleKernLigatureData(PerStyle& s) {
  delete[] s.kernLeftClasses;
  s.kernLeftClasses = nullptr;
  delete[] s.kernRightClasses;
  s.kernRightClasses = nullptr;
  delete[] s.ligaturePairs;
  s.ligaturePairs = nullptr;
  s.kernLigLoaded = false;
}

void SdCardFont::freeStyleMiniKern(PerStyle& s, bool freeMemory) {
  if (freeMemory) {
    delete[] s.miniKernLeftClasses;
    s.miniKernLeftClasses = nullptr;
    s.miniKernLeftClassesCap = 0;
    delete[] s.miniKernRightClasses;
    s.miniKernRightClasses = nullptr;
    s.miniKernRightClassesCap = 0;
    delete[] s.miniKernMatrix;
    s.miniKernMatrix = nullptr;
    s.miniKernMatrixCap = 0;
  }
  s.miniKernLeftEntryCount = 0;
  s.miniKernRightEntryCount = 0;
  s.miniKernLeftClassCount = 0;
  s.miniKernRightClassCount = 0;
}

void SdCardFont::freeStyleAll(PerStyle& s) {
  freeStyleMiniData(s, true);
  delete[] s.fullIntervals;
  s.fullIntervals = nullptr;
  freeStyleKernLigatureData(s);
  s.present = false;
}

// --- Global free/cleanup ---

void SdCardFont::freeAll() {
  clearOverflow();
  clearPersistentCache();
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    freeStyleAll(styles_[i]);
  }
  
  delete[] tmpCodepoints;
  tmpCodepoints = nullptr;
  tmpCodepointsCap = 0;
  delete[] tmpMappings;
  tmpMappings = nullptr;
  tmpMappingsCap = 0;
  delete[] tmpReadOrder;
  tmpReadOrder = nullptr;
  tmpReadOrderCap = 0;
  delete[] tmpAdvStaged;
  tmpAdvStaged = nullptr;
  tmpAdvStagedCap = 0;
  heap_caps_free(tmpAdvMerge);
  tmpAdvMerge = nullptr;
  tmpAdvMergeCap = 0;
  delete[] tmpKernRowBuf;
  tmpKernRowBuf = nullptr;
  tmpKernRowBufCap = 0;
  
  styleCount_ = 0;
  contentHash_ = 0;
  loaded_ = false;
}

void SdCardFont::clearOverflow() {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    delete[] overflow_[i].bitmap;
    overflow_[i].bitmap = nullptr;
    overflow_[i].bitmapCap = 0;
    overflow_[i].codepoint = 0;
  }
  overflowCount_ = 0;
  overflowNext_ = 0;
}

// --- Per-style kern/ligature ---

void SdCardFont::applyKernLigaturePointers(PerStyle& s, EpdFontData& data) const {
  data.kernLeftClasses = s.miniKernLeftClasses;
  data.kernRightClasses = s.miniKernRightClasses;
  data.kernMatrix = s.miniKernMatrix;
  data.kernLeftEntryCount = s.miniKernLeftEntryCount;
  data.kernRightEntryCount = s.miniKernRightEntryCount;
  data.kernLeftClassCount = s.miniKernLeftClassCount;
  data.kernRightClassCount = s.miniKernRightClassCount;
  data.ligaturePairs = s.ligaturePairs;
  data.ligaturePairCount = s.header.ligaturePairCount;
}

bool SdCardFont::loadStyleKernLigatureData(PerStyle& s) {
  if (s.kernLigLoaded) return true;
  bool hasKern = s.header.kernLeftEntryCount > 0;
  bool hasLig = s.header.ligaturePairCount > 0;
  if (!hasKern && !hasLig) {
    s.kernLigLoaded = true;
    return true;
  }

  FsFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont for kern/lig: %s", filePath_);
    return false;
  }

  if (hasKern) {
    s.kernLeftClasses = new (std::nothrow) EpdKernClassEntry[s.header.kernLeftEntryCount];
    s.kernRightClasses = new (std::nothrow) EpdKernClassEntry[s.header.kernRightEntryCount];

    if (!s.kernLeftClasses || !s.kernRightClasses) {
      LOG_ERR("SDCF", "Failed to allocate kern classes (%u+%u bytes)", s.header.kernLeftEntryCount * 3u,
              s.header.kernRightEntryCount * 3u);
      freeStyleKernLigatureData(s);
      return false;
    }

    if (!file.seekSet(s.kernLeftFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek to kern data");
      freeStyleKernLigatureData(s);
      return false;
    }
    size_t leftSz = s.header.kernLeftEntryCount * sizeof(EpdKernClassEntry);
    size_t rightSz = s.header.kernRightEntryCount * sizeof(EpdKernClassEntry);
    if (file.read(reinterpret_cast<uint8_t*>(s.kernLeftClasses), leftSz) != static_cast<int>(leftSz) ||
        file.read(reinterpret_cast<uint8_t*>(s.kernRightClasses), rightSz) != static_cast<int>(rightSz)) {
      LOG_ERR("SDCF", "Failed to read kern classes");
      freeStyleKernLigatureData(s);
      return false;
    }
  }

  if (hasLig) {
    s.ligaturePairs = new (std::nothrow) EpdLigaturePair[s.header.ligaturePairCount];
    if (!s.ligaturePairs) {
      LOG_ERR("SDCF", "Failed to allocate ligature pairs");
      freeStyleKernLigatureData(s);
      return false;
    }
    if (!file.seekSet(s.ligatureFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek to ligature data");
      freeStyleKernLigatureData(s);
      return false;
    }
    size_t sz = s.header.ligaturePairCount * sizeof(EpdLigaturePair);
    if (file.read(reinterpret_cast<uint8_t*>(s.ligaturePairs), sz) != static_cast<int>(sz)) {
      LOG_ERR("SDCF", "Failed to read ligature pairs");
      freeStyleKernLigatureData(s);
      return false;
    }
  }

  s.kernLigLoaded = true;

  s.stubData.ligaturePairs = s.ligaturePairs;
  s.stubData.ligaturePairCount = s.header.ligaturePairCount;

  LOG_DBG("SDCF", "Kern classes + lig loaded: kernL=%u, kernR=%u, ligs=%u", s.header.kernLeftEntryCount,
          s.header.kernRightEntryCount, s.header.ligaturePairCount);
  return true;
}

// --- Per-page mini kern matrix ---

static uint8_t miniLookupKernClass(const EpdKernClassEntry* entries, uint16_t count, uint32_t cp) {
  if (!entries || count == 0 || cp > 0xFFFF) return 0;
  const auto target = static_cast<uint16_t>(cp);
  const auto* end = entries + count;
  const auto it =
      std::lower_bound(entries, end, target, [](const EpdKernClassEntry& e, uint16_t v) { return e.codepoint < v; });
  return (it != end && it->codepoint == target) ? it->classId : 0;
}

bool SdCardFont::buildMiniKernMatrix(PerStyle& s, const uint32_t* codepoints, uint32_t cpCount) {
  freeStyleMiniKern(s, false);
  if (!s.kernLeftClasses || !s.kernRightClasses || s.header.kernLeftEntryCount == 0 ||
      s.header.kernRightEntryCount == 0) {
    return true;  // font has no kern classes — nothing to build
  }

  bool usedLeft[256] = {};
  bool usedRight[256] = {};
  for (uint32_t i = 0; i < cpCount; i++) {
    uint8_t lc = miniLookupKernClass(s.kernLeftClasses, s.header.kernLeftEntryCount, codepoints[i]);
    if (lc) usedLeft[lc] = true;
    uint8_t rc = miniLookupKernClass(s.kernRightClasses, s.header.kernRightEntryCount, codepoints[i]);
    if (rc) usedRight[rc] = true;
  }

  uint8_t leftRenumber[256] = {};
  uint8_t rightRenumber[256] = {};
  uint8_t newToOldLeft[256] = {};
  uint8_t newToOldRight[256] = {};
  uint8_t numLeft = 0, numRight = 0;
  for (int i = 1; i < 256; i++) {
    if (usedLeft[i]) {
      numLeft++;
      leftRenumber[i] = numLeft;
      newToOldLeft[numLeft] = static_cast<uint8_t>(i);
    }
    if (usedRight[i]) {
      numRight++;
      rightRenumber[i] = numRight;
      newToOldRight[numRight] = static_cast<uint8_t>(i);
    }
  }
  if (numLeft == 0 || numRight == 0) {
    return true;
  }

  uint16_t miniLeftCount = 0;
  uint16_t miniRightCount = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    if (miniLookupKernClass(s.kernLeftClasses, s.header.kernLeftEntryCount, codepoints[i]) != 0) miniLeftCount++;
    if (miniLookupKernClass(s.kernRightClasses, s.header.kernRightEntryCount, codepoints[i]) != 0) miniRightCount++;
  }

  const uint32_t matrixBytes = static_cast<uint32_t>(numLeft) * numRight;
  
  if (miniLeftCount > s.miniKernLeftClassesCap) {
    delete[] s.miniKernLeftClasses;
    s.miniKernLeftClasses = new (std::nothrow) EpdKernClassEntry[miniLeftCount];
    if (!s.miniKernLeftClasses) {
      LOG_ERR("SDCF", "Failed to allocate mini kern left classes (%u bytes)", miniLeftCount * 3u);
      s.miniKernLeftClassesCap = 0;
      freeStyleMiniKern(s, true);
      return false;
    }
    s.miniKernLeftClassesCap = miniLeftCount;
  }
  if (miniRightCount > s.miniKernRightClassesCap) {
    delete[] s.miniKernRightClasses;
    s.miniKernRightClasses = new (std::nothrow) EpdKernClassEntry[miniRightCount];
    if (!s.miniKernRightClasses) {
      LOG_ERR("SDCF", "Failed to allocate mini kern right classes (%u bytes)", miniRightCount * 3u);
      s.miniKernRightClassesCap = 0;
      freeStyleMiniKern(s, true);
      return false;
    }
    s.miniKernRightClassesCap = miniRightCount;
  }
  if (matrixBytes > s.miniKernMatrixCap) {
    delete[] s.miniKernMatrix;
    s.miniKernMatrix = new (std::nothrow) int8_t[matrixBytes];
    if (!s.miniKernMatrix) {
      LOG_ERR("SDCF", "Failed to allocate mini kern matrix (%u bytes)", matrixBytes);
      s.miniKernMatrixCap = 0;
      freeStyleMiniKern(s, true);
      return false;
    }
    s.miniKernMatrixCap = matrixBytes;
  }

  uint16_t lIdx = 0, rIdx = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    uint32_t cp = codepoints[i];
    if (cp > 0xFFFF) continue;
    uint8_t lc = miniLookupKernClass(s.kernLeftClasses, s.header.kernLeftEntryCount, cp);
    if (lc) {
      s.miniKernLeftClasses[lIdx].codepoint = static_cast<uint16_t>(cp);
      s.miniKernLeftClasses[lIdx].classId = leftRenumber[lc];
      lIdx++;
    }
    uint8_t rc = miniLookupKernClass(s.kernRightClasses, s.header.kernRightEntryCount, cp);
    if (rc) {
      s.miniKernRightClasses[rIdx].codepoint = static_cast<uint16_t>(cp);
      s.miniKernRightClasses[rIdx].classId = rightRenumber[rc];
      rIdx++;
    }
  }

  if (s.header.kernRightClassCount > tmpKernRowBufCap) {
    delete[] tmpKernRowBuf;
    tmpKernRowBuf = new (std::nothrow) int8_t[s.header.kernRightClassCount];
    if (!tmpKernRowBuf) {
      LOG_ERR("SDCF", "Failed to allocate row buffer (%u bytes)", s.header.kernRightClassCount);
      tmpKernRowBufCap = 0;
      freeStyleMiniKern(s, true);
      return false;
    }
    tmpKernRowBufCap = s.header.kernRightClassCount;
  }
  int8_t* rowBuf = tmpKernRowBuf;

  FsFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont for mini kern: %s", filePath_);
    freeStyleMiniKern(s, true);
    return false;
  }

  for (uint8_t newL = 1; newL <= numLeft; newL++) {
    const uint8_t oldL = newToOldLeft[newL];
    const uint32_t rowFileOff = s.kernMatrixFileOffset + (oldL - 1u) * s.header.kernRightClassCount;
    if (!file.seekSet(rowFileOff)) {
      LOG_ERR("SDCF", "Failed to seek to kern row %u", oldL);
      freeStyleMiniKern(s, true);
      return false;
    }
    if (file.read(reinterpret_cast<uint8_t*>(rowBuf), s.header.kernRightClassCount) !=
        static_cast<int>(s.header.kernRightClassCount)) {
      LOG_ERR("SDCF", "Failed to read kern row %u", oldL);
      freeStyleMiniKern(s, true);
      return false;
    }
    int8_t* miniRow = s.miniKernMatrix + (newL - 1u) * numRight;
    for (uint8_t newR = 1; newR <= numRight; newR++) {
      miniRow[newR - 1] = rowBuf[newToOldRight[newR] - 1u];
    }
  }
  file.close();

  s.miniKernLeftEntryCount = lIdx;
  s.miniKernRightEntryCount = rIdx;
  s.miniKernLeftClassCount = numLeft;
  s.miniKernRightClassCount = numRight;

  LOG_DBG("SDCF", "Built mini kern: %u×%u matrix (%u bytes, full was %u×%u = %u bytes)", numLeft, numRight, matrixBytes,
          s.header.kernLeftClassCount, s.header.kernRightClassCount,
          static_cast<uint32_t>(s.header.kernLeftClassCount) * s.header.kernRightClassCount);
  return true;
}

// --- Glyph miss callback ---

void SdCardFont::applyGlyphMissCallback(uint8_t styleIdx) {
  overflowCtx_[styleIdx].self = this;
  overflowCtx_[styleIdx].styleIdx = styleIdx;

  auto& s = styles_[styleIdx];
  s.stubData.glyphMissHandler = &SdCardFont::onGlyphMiss;
  s.stubData.glyphMissCtx = &overflowCtx_[styleIdx];
}

// --- Compute per-style file offsets from a base data offset ---

void SdCardFont::computeStyleFileOffsets(PerStyle& s, uint32_t baseOffset) {
  s.intervalsFileOffset = baseOffset;
  s.glyphsFileOffset = s.intervalsFileOffset + s.header.intervalCount * sizeof(EpdUnicodeInterval);
  s.kernLeftFileOffset = s.glyphsFileOffset + s.header.glyphCount * sizeof(EpdGlyph);
  s.kernRightFileOffset = s.kernLeftFileOffset + s.header.kernLeftEntryCount * sizeof(EpdKernClassEntry);
  s.kernMatrixFileOffset = s.kernRightFileOffset + s.header.kernRightEntryCount * sizeof(EpdKernClassEntry);
  s.ligatureFileOffset =
      s.kernMatrixFileOffset + static_cast<uint32_t>(s.header.kernLeftClassCount) * s.header.kernRightClassCount;
  s.bitmapFileOffset = s.ligatureFileOffset + s.header.ligaturePairCount * sizeof(EpdLigaturePair);
}

// --- Load ---

bool SdCardFont::load(const char* path) {
  LOG_DBG("HCR-FRAG", "SDCF load start: free=%u maxA=%u frag=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
  freeAll();
  if (strlen(path) >= sizeof(filePath_)) {
    LOG_ERR("SDCF", "Path too long (%zu bytes, max %zu)", strlen(path), sizeof(filePath_) - 1);
    return false;
  }
  strncpy(filePath_, path, sizeof(filePath_) - 1);
  filePath_[sizeof(filePath_) - 1] = '\0';

  FsFile file;
  if (!Storage.openFileForRead("SDCF", path, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont: %s", path);
    return false;
  }

  uint8_t headerBuf[HEADER_SIZE];
  if (file.read(headerBuf, HEADER_SIZE) != HEADER_SIZE) {
    LOG_ERR("SDCF", "Failed to read header");
    return false;
  }

  if (memcmp(headerBuf, CPFONT_MAGIC, 8) != 0) {
    LOG_ERR("SDCF", "Invalid magic bytes");
    return false;
  }

  uint16_t fileVersion = readU16(headerBuf + 8);
  if (fileVersion != CPFONT_VERSION) {
    LOG_ERR("SDCF", "Unsupported version: %u (expected %u)", fileVersion, CPFONT_VERSION);
    return false;
  }

  uint32_t hash = fnv1a(headerBuf, HEADER_SIZE);

  bool is2Bit = (readU16(headerBuf + 10) & 1) != 0;

  uint8_t styleCount = headerBuf[12];
  if (styleCount == 0 || styleCount > MAX_STYLES) {
    LOG_ERR("SDCF", "Invalid style count: %u", styleCount);
    return false;
  }

  for (uint8_t i = 0; i < styleCount; i++) {
    uint8_t tocBuf[STYLE_TOC_ENTRY_SIZE];
    if (file.read(tocBuf, STYLE_TOC_ENTRY_SIZE) != STYLE_TOC_ENTRY_SIZE) {
      LOG_ERR("SDCF", "Failed to read style TOC entry %u", i);
      freeAll();
      return false;
    }

    hash = fnv1a(tocBuf, STYLE_TOC_ENTRY_SIZE, hash);

    uint8_t styleId = tocBuf[0];
    if (styleId >= MAX_STYLES) {
      LOG_ERR("SDCF", "Invalid styleId %u in TOC", styleId);
      file.close();
      freeAll();
      return false;
    }

    auto& s = styles_[styleId];
    s.present = true;
    s.header.intervalCount = readU32(tocBuf + 4);
    s.header.glyphCount = readU32(tocBuf + 8);
    s.header.advanceY = tocBuf[12];
    s.header.ascender = readI16(tocBuf + 13);
    s.header.descender = readI16(tocBuf + 15);
    s.header.kernLeftEntryCount = readU16(tocBuf + 17);
    s.header.kernRightEntryCount = readU16(tocBuf + 19);
    s.header.kernLeftClassCount = tocBuf[21];
    s.header.kernRightClassCount = tocBuf[22];
    s.header.ligaturePairCount = tocBuf[23];
    s.header.is2Bit = is2Bit;

    static constexpr uint32_t MAX_INTERVALS = 4096;
    static constexpr uint32_t MAX_GLYPHS = 65536;
    static constexpr uint32_t MAX_KERN_ENTRIES = 4096;
    if (s.header.intervalCount > MAX_INTERVALS || s.header.glyphCount > MAX_GLYPHS ||
        s.header.kernLeftEntryCount > MAX_KERN_ENTRIES || s.header.kernRightEntryCount > MAX_KERN_ENTRIES) {
      LOG_ERR("SDCF", "Style %u: unreasonable counts (iv=%u, gl=%u, kL=%u, kR=%u)", styleId, s.header.intervalCount,
              s.header.glyphCount, s.header.kernLeftEntryCount, s.header.kernRightEntryCount);
      file.close();
      freeAll();
      return false;
    }

    uint32_t dataOffset = readU32(tocBuf + 24);
    computeStyleFileOffsets(s, dataOffset);
  }

  styleCount_ = styleCount;
  contentHash_ = hash;

  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    auto& s = styles_[i];
    if (!s.present) continue;

    s.fullIntervals = new (std::nothrow) EpdUnicodeInterval[s.header.intervalCount];
    if (!s.fullIntervals) {
      LOG_ERR("SDCF", "Failed to allocate %u intervals for style %u", s.header.intervalCount, i);
      freeAll();
      return false;
    }

    if (!file.seekSet(s.intervalsFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek to intervals for style %u", i);
      freeAll();
      return false;
    }
    size_t intervalsBytes = s.header.intervalCount * sizeof(EpdUnicodeInterval);
    if (file.read(reinterpret_cast<uint8_t*>(s.fullIntervals), intervalsBytes) != static_cast<int>(intervalsBytes)) {
      LOG_ERR("SDCF", "Failed to read intervals for style %u", i);
      freeAll();
      return false;
    }

    {
      uint32_t expectedOffset = 0;
      uint32_t prevLast = 0;
      for (uint32_t j = 0; j < s.header.intervalCount; ++j) {
        const auto& iv = s.fullIntervals[j];
        if (iv.first > iv.last) {
          LOG_ERR("SDCF", "Style %u: invalid interval %u (first 0x%lX > last 0x%lX)", i, j,
                  static_cast<unsigned long>(iv.first), static_cast<unsigned long>(iv.last));
          file.close();
          freeAll();
          return false;
        }
        const uint32_t span = iv.last - iv.first + 1;
        const bool overlapsPrev = (j > 0 && iv.first <= prevLast);
        const bool spanTooBig = (span > s.header.glyphCount);
        const bool offsetMismatch = (iv.offset != expectedOffset);
        const bool offsetOverruns = (iv.offset > s.header.glyphCount - span);
        if (overlapsPrev || spanTooBig || offsetMismatch || offsetOverruns) {
          LOG_ERR("SDCF", "Style %u: invalid interval layout at %u (overlap=%d span=%u offMis=%d offOver=%d)", i, j,
                  overlapsPrev, span, offsetMismatch, offsetOverruns);
          file.close();
          freeAll();
          return false;
        }
        expectedOffset += span;
        prevLast = iv.last;
      }
    }

    memset(&s.stubData, 0, sizeof(s.stubData));
    s.stubData.advanceY = s.header.advanceY;
    s.stubData.ascender = s.header.ascender;
    s.stubData.descender = s.header.descender;
    s.stubData.is2Bit = s.header.is2Bit;

    s.epdFont.data = &s.stubData;
    applyGlyphMissCallback(i);
  }

  loaded_ = true;

  LOG_DBG("SDCF", "Loaded: %s (v%u, %u styles)", path, CPFONT_VERSION, styleCount_);
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!styles_[i].present) continue;
    const auto& h = styles_[i].header;
    LOG_DBG("SDCF", "  style[%u]: %u intervals, %u glyphs, advY=%u, asc=%d, desc=%d, kernL=%u, kernR=%u, ligs=%u", i,
            h.intervalCount, h.glyphCount, h.advanceY, h.ascender, h.descender, h.kernLeftEntryCount,
            h.kernRightEntryCount, h.ligaturePairCount);
  }
  LOG_DBG("HCR-FRAG", "SDCF load done: free=%u maxA=%u frag=%d", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
  return true;
}

// --- Codepoint lookup ---

int32_t SdCardFont::findGlobalGlyphIndex(const PerStyle& s, uint32_t codepoint) const {
  int left = 0;
  int right = static_cast<int>(s.header.intervalCount) - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    const auto& interval = s.fullIntervals[mid];
    if (codepoint < interval.first) {
      right = mid - 1;
    } else if (codepoint > interval.last) {
      left = mid + 1;
    } else {
      return static_cast<int32_t>(interval.offset + (codepoint - interval.first));
    }
  }
  return -1;
}

// --- Prewarm ---

int SdCardFont::prewarm(const char* utf8Text, uint8_t styleMask, bool metadataOnly) {
  if (!loaded_) return -1;

  styleMask = resolveStyleMask(styleMask);
  if (styleMask == 0) return 0;

  unsigned long startMs = millis();

  if (MAX_PAGE_GLYPHS > tmpCodepointsCap) {
    delete[] tmpCodepoints;
    tmpCodepoints = new (std::nothrow) uint32_t[MAX_PAGE_GLYPHS];
    if (!tmpCodepoints) {
      LOG_ERR("SDCF", "Failed to allocate codepoint buffer (%u bytes)", MAX_PAGE_GLYPHS * 4);
      tmpCodepointsCap = 0;
      return -1;
    }
    tmpCodepointsCap = MAX_PAGE_GLYPHS;
  }
  uint32_t* codepoints = tmpCodepoints;
  uint32_t cpCount = 0;

  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Text);
  while (*p && cpCount < MAX_PAGE_GLYPHS) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    bool found = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == cp) {
        found = true;
        break;
      }
    }
    if (!found) {
      codepoints[cpCount++] = cp;
    }
  }

  {
    bool hasReplacement = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == REPLACEMENT_GLYPH) {
        hasReplacement = true;
        break;
      }
    }
    if (!hasReplacement && cpCount < MAX_PAGE_GLYPHS) {
      codepoints[cpCount++] = REPLACEMENT_GLYPH;
    }
  }

  if (!metadataOnly) {
    for (uint8_t si = 0; si < MAX_STYLES; si++) {
      if (!(styleMask & (1 << si)) || !styles_[si].present) continue;
      auto& s = styles_[si];

      loadStyleKernLigatureData(s);
      if (s.ligaturePairs && s.header.ligaturePairCount > 0) {
        for (uint8_t li = 0; li < s.header.ligaturePairCount && cpCount < MAX_PAGE_GLYPHS; li++) {
          uint32_t leftCp = s.ligaturePairs[li].pair >> 16;
          uint32_t rightCp = s.ligaturePairs[li].pair & 0xFFFF;
          uint32_t outCp = s.ligaturePairs[li].ligatureCp;

          bool hasLeft = false, hasRight = false;
          for (uint32_t i = 0; i < cpCount; i++) {
            if (codepoints[i] == leftCp) hasLeft = true;
            if (codepoints[i] == rightCp) hasRight = true;
            if (hasLeft && hasRight) break;
          }
          if (!hasLeft || !hasRight) continue;

          bool hasOut = false;
          for (uint32_t i = 0; i < cpCount; i++) {
            if (codepoints[i] == outCp) {
              hasOut = true;
              break;
            }
          }
          if (!hasOut) {
            codepoints[cpCount++] = outCp;
          }
        }
      }
    }
  }

  std::sort(codepoints, codepoints + cpCount);

  int totalMissed = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(styleMask & (1 << si)) || !styles_[si].present) continue;
    totalMissed += prewarmStyle(si, codepoints, cpCount, metadataOnly);
  }

  stats_.prewarmTotalMs = millis() - startMs;
  return totalMissed;
}

int SdCardFont::prewarmStyle(uint8_t styleIdx, const uint32_t* codepoints, uint32_t cpCount, bool metadataOnly) {
  auto& s = styles_[styleIdx];

  if (cpCount > tmpMappingsCap) {
    delete[] tmpMappings;
    tmpMappings = new (std::nothrow) TmpMapping[cpCount];
    if (!tmpMappings) {
      LOG_ERR("SDCF", "Failed to allocate mapping array for style %u", styleIdx);
      tmpMappingsCap = 0;
      return static_cast<int>(cpCount);
    }
    tmpMappingsCap = cpCount;
  }
  TmpMapping* mappings = tmpMappings;

  uint32_t validCount = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    int32_t idx = findGlobalGlyphIndex(s, codepoints[i]);
    if (idx >= 0) {
      mappings[validCount].codepoint = codepoints[i];
      mappings[validCount].globalIndex = idx;
      validCount++;
    }
  }
  int missed = static_cast<int>(cpCount - validCount);

  if (validCount == 0) {
    freeStyleMiniData(s, false);
    s.epdFont.data = &s.stubData;
    return missed;
  }

  freeStyleMiniData(s, false);

  uint32_t intervalCapacity = validCount;
  if (intervalCapacity > s.miniIntervalsCap) {
    delete[] s.miniIntervals;
    s.miniIntervals = new (std::nothrow) EpdUnicodeInterval[intervalCapacity];
    if (!s.miniIntervals) {
      LOG_ERR("SDCF", "Failed to allocate mini intervals for style %u", styleIdx);
      s.miniIntervalsCap = 0;
      return static_cast<int>(cpCount);
    }
    s.miniIntervalsCap = intervalCapacity;
  }

  s.miniIntervalCount = 0;
  uint32_t rangeStart = 0;
  for (uint32_t i = 1; i <= validCount; i++) {
    if (i == validCount || mappings[i].codepoint != mappings[i - 1].codepoint + 1) {
      s.miniIntervals[s.miniIntervalCount].first = mappings[rangeStart].codepoint;
      s.miniIntervals[s.miniIntervalCount].last = mappings[i - 1].codepoint;
      s.miniIntervals[s.miniIntervalCount].offset = rangeStart;
      s.miniIntervalCount++;
      rangeStart = i;
    }
  }

  s.miniGlyphCount = validCount;
  if (s.miniGlyphCount > s.miniGlyphsCap) {
    delete[] s.miniGlyphs;
    s.miniGlyphs = new (std::nothrow) EpdGlyph[s.miniGlyphCount];
    if (!s.miniGlyphs) {
      LOG_ERR("SDCF", "Failed to allocate mini glyphs for style %u", styleIdx);
      s.miniGlyphsCap = 0;
      return static_cast<int>(cpCount);
    }
    s.miniGlyphsCap = s.miniGlyphCount;
  }

  if (validCount > tmpReadOrderCap) {
    delete[] tmpReadOrder;
    tmpReadOrder = new (std::nothrow) uint32_t[validCount];
    if (!tmpReadOrder) {
      LOG_ERR("SDCF", "Failed to allocate read order for style %u", styleIdx);
      tmpReadOrderCap = 0;
      return static_cast<int>(cpCount);
    }
    tmpReadOrderCap = validCount;
  }
  uint32_t* readOrder = tmpReadOrder;

  for (uint32_t i = 0; i < validCount; i++) readOrder[i] = i;
  std::sort(readOrder, readOrder + validCount,
            [&](uint32_t a, uint32_t b) { return mappings[a].globalIndex < mappings[b].globalIndex; });

  FsFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to reopen .cpfont for prewarm (style %u)", styleIdx);
    return static_cast<int>(cpCount);
  }

  unsigned long sdStart = millis();
  uint32_t seekCount = 0;

  int32_t lastReadIndex = INT32_MIN;
  for (uint32_t i = 0; i < validCount; i++) {
    uint32_t mapIdx = readOrder[i];
    int32_t gIdx = mappings[mapIdx].globalIndex;

    uint32_t fileOff = s.glyphsFileOffset + static_cast<uint32_t>(gIdx) * sizeof(EpdGlyph);
    if (gIdx != lastReadIndex + 1) {
      if (!file.seekSet(fileOff)) {
        LOG_ERR("SDCF", "Prewarm: failed to seek to glyph %d (style %u)", gIdx, styleIdx);
        file.close();
        return static_cast<int>(cpCount);
      }
      seekCount++;
    }
    if (file.read(reinterpret_cast<uint8_t*>(&s.miniGlyphs[mapIdx]), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
      LOG_ERR("SDCF", "Prewarm: short glyph read (style %u, glyph %d)", styleIdx, gIdx);
      file.close();
      return static_cast<int>(cpCount);
    }
    lastReadIndex = gIdx;
  }

  uint32_t totalBitmapSize = 0;

  if (!metadataOnly) {
    for (uint32_t i = 0; i < validCount; i++) {
      totalBitmapSize += s.miniGlyphs[i].dataLength;
    }

    uint32_t bitmapAllocSize = totalBitmapSize > 0 ? totalBitmapSize : 1;
    if (bitmapAllocSize > s.miniBitmapCap) {
      delete[] s.miniBitmap;
      s.miniBitmap = new (std::nothrow) uint8_t[bitmapAllocSize];
      if (!s.miniBitmap) {
        LOG_ERR("SDCF", "Failed to allocate mini bitmap (%u bytes) for style %u", totalBitmapSize, styleIdx);
        s.miniBitmapCap = 0;
        file.close();
        return static_cast<int>(cpCount);
      }
      s.miniBitmapCap = bitmapAllocSize;
    }

    std::sort(readOrder, readOrder + validCount,
              [&](uint32_t a, uint32_t b) { return s.miniGlyphs[a].dataOffset < s.miniGlyphs[b].dataOffset; });

    uint32_t miniBitmapOffset = 0;
    uint32_t lastBitmapEnd = UINT32_MAX;
    for (uint32_t i = 0; i < validCount; i++) {
      uint32_t mapIdx = readOrder[i];
      EpdGlyph& glyph = s.miniGlyphs[mapIdx];

      if (glyph.dataLength == 0) {
        glyph.dataOffset = miniBitmapOffset;
        continue;
      }

      uint32_t fileOff = s.bitmapFileOffset + glyph.dataOffset;
      if (fileOff != lastBitmapEnd) {
        if (!file.seekSet(fileOff)) {
          LOG_ERR("SDCF", "Prewarm: failed to seek to bitmap (style %u)", styleIdx);
          file.close();
          return static_cast<int>(cpCount);
        }
        seekCount++;
      }
      if (file.read(s.miniBitmap + miniBitmapOffset, glyph.dataLength) != static_cast<int>(glyph.dataLength)) {
        LOG_ERR("SDCF", "Prewarm: short bitmap read (style %u)", styleIdx);
        file.close();
        return static_cast<int>(cpCount);
      }
      lastBitmapEnd = fileOff + glyph.dataLength;

      glyph.dataOffset = miniBitmapOffset;
      miniBitmapOffset += glyph.dataLength;
    }
  }

  uint32_t sdTime = millis() - sdStart;
  file.close();

  bool kernLigOk = false;
  if (!metadataOnly) {
    if (loadStyleKernLigatureData(s)) {
      kernLigOk = buildMiniKernMatrix(s, codepoints, cpCount);
    }
  }

  memset(&s.miniData, 0, sizeof(s.miniData));
  s.miniData.bitmap = s.miniBitmap;
  s.miniData.glyph = s.miniGlyphs;
  s.miniData.intervals = s.miniIntervals;
  s.miniData.intervalCount = s.miniIntervalCount;
  s.miniData.advanceY = s.header.advanceY;
  s.miniData.ascender = s.header.ascender;
  s.miniData.descender = s.header.descender;
  s.miniData.is2Bit = s.header.is2Bit;
  if (kernLigOk) {
    applyKernLigaturePointers(s, s.miniData);
  }
  s.miniData.glyphMissHandler = &SdCardFont::onGlyphMiss;
  s.miniData.glyphMissCtx = &overflowCtx_[styleIdx];

  s.epdFont.data = &s.miniData;

  stats_.sdReadTimeMs += sdTime;
  stats_.seekCount += seekCount;
  stats_.uniqueGlyphs += validCount;
  stats_.bitmapBytes += totalBitmapSize;

  return missed;
}

// --- Cache management ---

void SdCardFont::clearCache() {
  clearOverflow();
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!styles_[i].present) continue;
    freeStyleMiniData(styles_[i], false);
    applyGlyphMissCallback(i);
  }
}

void SdCardFont::releaseForLowMemory() {
  clearOverflow();
  clearPersistentCache();

  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!styles_[i].present) continue;
    freeStyleMiniData(styles_[i], true);
    freeStyleKernLigatureData(styles_[i]);
    applyGlyphMissCallback(i);
  }
}

// --- Advance table ---

void SdCardFont::clearPersistentCache() {
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (advanceTable_[i]) {
      heap_caps_free(advanceTable_[i]);
      advanceTable_[i] = nullptr;
      advanceTableSize_[i] = 0;
    }
  }
}

bool SdCardFont::advanceTableLookup(uint8_t styleIdx, uint32_t codepoint, uint16_t* outAdvance) const {
  const AdvanceEntry* table = advanceTable_[styleIdx];
  const uint32_t size = advanceTableSize_[styleIdx];
  if (!table || size == 0) return false;
  uint32_t lo = 0, hi = size;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (table[mid].codepoint < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < size && table[lo].codepoint == codepoint) {
    if (outAdvance) *outAdvance = table[lo].advanceX;
    return true;
  }
  return false;
}

void SdCardFont::mergeIntoAdvanceTable(uint8_t styleIdx, const AdvanceEntry* sortedNew, uint32_t newCount) {
  if (newCount == 0) return;
  const uint32_t oldSize = advanceTableSize_[styleIdx];
  if (oldSize >= ADVANCE_CACHE_LIMIT) return;

  uint32_t mergedCap = oldSize + newCount;
  if (mergedCap > ADVANCE_CACHE_LIMIT) mergedCap = ADVANCE_CACHE_LIMIT;

  const uint32_t freeBefore = ESP.getFreeHeap();
  const uint32_t maxAllocBefore = ESP.getMaxAllocHeap();

  // Merge into a reused scratch buffer (never aliases the advance table), then
  // transfer the result into the advance table. Growing the table via
  // heap_caps_realloc keeps a single stable allocation that can expand in
  // place, avoiding the new[]+delete[] churn that fragmented the heap across
  // the hundreds of small merges per style during indexing.
  if (tmpAdvMergeCap < mergedCap) {
    AdvanceEntry* grown = (AdvanceEntry*)heap_caps_realloc(tmpAdvMerge, mergedCap * sizeof(AdvanceEntry),
                                                           MALLOC_CAP_DEFAULT);
    if (!grown) {
      LOG_ERR("SDCF", "mergeIntoAdvanceTable: scratch realloc failed (%u -> %u entries) style %u", tmpAdvMergeCap,
              mergedCap, styleIdx);
      return;
    }
    tmpAdvMerge = grown;
    tmpAdvMergeCap = mergedCap;
  }

  const AdvanceEntry* a = advanceTable_[styleIdx];
  const AdvanceEntry* b = sortedNew;
  AdvanceEntry* out = tmpAdvMerge;
  uint32_t i = 0, j = 0, k = 0;
  while (k < mergedCap && (i < oldSize || j < newCount)) {
    if (i < oldSize && (j >= newCount || a[i].codepoint <= b[j].codepoint)) {
      out[k++] = a[i++];
    } else {
      out[k++] = b[j++];
    }
  }

  // Grow the advance table via realloc so a single allocation expands in place
  // when trailing free memory is available. Falls back to a fresh malloc if
  // realloc must move but cannot find a larger block; in that case the old
  // buffer is still live and must be freed explicitly.
  AdvanceEntry* oldPtr = advanceTable_[styleIdx];
  AdvanceEntry* table = (AdvanceEntry*)heap_caps_realloc(oldPtr, mergedCap * sizeof(AdvanceEntry),
                                                         MALLOC_CAP_DEFAULT);
  bool freshAlloc = false;
  if (!table) {
    table = (AdvanceEntry*)heap_caps_malloc(mergedCap * sizeof(AdvanceEntry), MALLOC_CAP_DEFAULT);
    if (!table) {
      LOG_ERR("SDCF", "mergeIntoAdvanceTable: table alloc failed (%u entries) style %u; keeping old table", mergedCap,
              styleIdx);
      return;
    }
    freshAlloc = true;
  }

  if (k > 0) {
    memcpy(table, out, k * sizeof(AdvanceEntry));
  }

  // realloc already freed oldPtr when it moved/succeeded; free it only when we
  // took the fresh-alloc fallback and oldPtr is still a live allocation.
  if (freshAlloc && oldPtr) {
    heap_caps_free(oldPtr);
  }

  advanceTable_[styleIdx] = table;
  advanceTableSize_[styleIdx] = k;

  const uint32_t freeAfter = ESP.getFreeHeap();
  const uint32_t maxAllocAfter = ESP.getMaxAllocHeap();
  LOG_DBG("SDCF",
          "Advance table style %u merge: +%u (old=%u -> total=%u/%u) free=%u->%u maxAlloc=%u->%u",
          styleIdx, newCount, oldSize, k, ADVANCE_CACHE_LIMIT,
          freeBefore, freeAfter, maxAllocBefore, maxAllocAfter);
}

bool SdCardFont::hasAdvanceTable() const {
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (advanceTable_[i]) return true;
  }
  return false;
}

uint16_t SdCardFont::getAdvance(uint32_t codepoint, uint8_t style) const {
  style &= (MAX_STYLES - 1);
  uint16_t advance = 0;
  return advanceTableLookup(style, codepoint, &advance) ? advance : 0;
}

bool SdCardFont::getAdvance(uint32_t codepoint, uint8_t style, uint16_t* outAdvance) const {
  style &= (MAX_STYLES - 1);
  return advanceTableLookup(style, codepoint, outAdvance);
}

int SdCardFont::buildAdvanceTable(const char* utf8Text, uint8_t styleMask) {
  if (!loaded_) return -1;

  styleMask = resolveStyleMask(styleMask);
  if (styleMask == 0) return 0;

  unsigned long startMs = millis();

  static constexpr uint32_t MAX_UNIQUE_CODEPOINTS = 4096;
  uint32_t neededCap = MAX_UNIQUE_CODEPOINTS + 2;
  if (neededCap > tmpCodepointsCap) {
    delete[] tmpCodepoints;
    tmpCodepoints = new (std::nothrow) uint32_t[neededCap];
    if (!tmpCodepoints) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate codepoint buffer (%u bytes)", neededCap * 4);
      tmpCodepointsCap = 0;
      return -1;
    }
    tmpCodepointsCap = neededCap;
  }
  uint32_t* codepoints = tmpCodepoints;
  
  uint32_t cpCount = 0;
  bool hitCap = false;

  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Text);
  while (*p) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    bool found = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == cp) {
        found = true;
        break;
      }
    }
    if (!found) {
      if (cpCount >= MAX_UNIQUE_CODEPOINTS) {
        hitCap = true;
        break;
      }
      codepoints[cpCount++] = cp;
    }
  }
  if (hitCap) {
    LOG_ERR("SDCF", "buildAdvanceTable: unique codepoint cap (%u) hit, layout may be approximate",
            MAX_UNIQUE_CODEPOINTS);
  }

  std::sort(codepoints, codepoints + cpCount);

  int totalMissed = fetchAdvancesForCodepoints(codepoints, cpCount, styleMask);

  stats_.prewarmTotalMs = millis() - startMs;
  return totalMissed;
}

int SdCardFont::fetchAdvancesForCodepoints(uint32_t* codepoints, uint32_t cpCount, uint8_t styleMask) {
  int totalMissed = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(styleMask & (1 << si)) || !styles_[si].present) continue;
    const auto& s = styles_[si];
    const int32_t replacementIdx = findGlobalGlyphIndex(s, REPLACEMENT_GLYPH);

    if (advanceTableSize_[si] >= ADVANCE_CACHE_LIMIT) continue;

    if (cpCount > tmpMappingsCap) {
      delete[] tmpMappings;
      tmpMappings = new (std::nothrow) TmpMapping[cpCount];
      if (!tmpMappings) {
        LOG_ERR("SDCF", "fetchAdvancesForCodepoints: failed to allocate mappings for style %u", si);
        tmpMappingsCap = 0;
        totalMissed += cpCount;
        continue;
      }
      tmpMappingsCap = cpCount;
    }
    TmpMapping* mappings = tmpMappings;

    uint32_t needCount = 0;
    uint32_t missedThisStyle = 0;
    for (uint32_t i = 0; i < cpCount; i++) {
      const uint32_t cp = codepoints[i];
      if (advanceTableLookup(si, cp, nullptr)) continue;
      int32_t idx = findGlobalGlyphIndex(s, cp);
      if (idx < 0) {
        if (replacementIdx < 0) {
          missedThisStyle++;
          continue;
        }
        idx = replacementIdx;
      }
      mappings[needCount].codepoint = cp;
      mappings[needCount].globalIndex = idx;
      needCount++;
    }
    totalMissed += static_cast<int>(missedThisStyle);

    if (needCount == 0) continue;

    std::sort(mappings, mappings + needCount,
              [](const TmpMapping& a, const TmpMapping& b) { return a.globalIndex < b.globalIndex; });

    FsFile file;
    if (!Storage.openFileForRead("SDCF", filePath_, file)) {
      LOG_ERR("SDCF", "fetchAdvancesForCodepoints: failed to open .cpfont for style %u", si);
      continue;
    }

    if (needCount > tmpAdvStagedCap) {
      delete[] tmpAdvStaged;
      tmpAdvStaged = new (std::nothrow) AdvanceEntry[needCount];
      if (!tmpAdvStaged) {
        LOG_ERR("SDCF", "fetchAdvancesForCodepoints: failed to allocate staging for style %u", si);
        tmpAdvStagedCap = 0;
        file.close();
        continue;
      }
      tmpAdvStagedCap = needCount;
    }
    AdvanceEntry* staged = tmpAdvStaged;

    uint32_t fetched = 0;
    EpdGlyph tempGlyph;
    int32_t lastReadIndex = INT32_MIN;
    for (uint32_t i = 0; i < needCount; i++) {
      int32_t gIdx = mappings[i].globalIndex;
      uint32_t fileOff = s.glyphsFileOffset + static_cast<uint32_t>(gIdx) * sizeof(EpdGlyph);
      if (gIdx != lastReadIndex + 1) {
        if (!file.seekSet(fileOff)) {
          LOG_ERR("SDCF", "fetchAdvancesForCodepoints: failed to seek to glyph %d (style %u)", gIdx, si);
          break;
        }
      }
      if (file.read(reinterpret_cast<uint8_t*>(&tempGlyph), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
        LOG_ERR("SDCF", "fetchAdvancesForCodepoints: short glyph read (style %u, glyph %d)", si, gIdx);
        break;
      }
      lastReadIndex = gIdx;
      staged[fetched].codepoint = mappings[i].codepoint;
      staged[fetched].advanceX = tempGlyph.advanceX;
      fetched++;
    }
    file.close();

    if (fetched > 0) {
      std::sort(staged, staged + fetched,
                [](const AdvanceEntry& a, const AdvanceEntry& b) { return a.codepoint < b.codepoint; });
      mergeIntoAdvanceTable(si, staged, fetched);
    }

    LOG_DBG("SDCF", "Advance table style %u: +%u from SD, total=%u/%u", si, fetched, advanceTableSize_[si],
            ADVANCE_CACHE_LIMIT);
  }

  return totalMissed;
}

template <typename Iter>
int SdCardFont::buildAdvanceTableRange(Iter begin, Iter end, bool includeSpace, bool includeHyphen, uint8_t styleMask) {
  if (!loaded_) return -1;

  styleMask = resolveStyleMask(styleMask);
  if (styleMask == 0) return 0;

  unsigned long startMs = millis();
  static constexpr uint32_t MAX_UNIQUE_CODEPOINTS = 4096;
  uint32_t neededCap = MAX_UNIQUE_CODEPOINTS + 2;
  if (neededCap > tmpCodepointsCap) {
    delete[] tmpCodepoints;
    tmpCodepoints = new (std::nothrow) uint32_t[neededCap];
    if (!tmpCodepoints) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate codepoint buffer (%u bytes)", neededCap * 4);
      tmpCodepointsCap = 0;
      return -1;
    }
    tmpCodepointsCap = neededCap;
  }
  uint32_t* codepoints = tmpCodepoints;

  uint32_t cpCount = 0;
  bool hitCap = false;
  for (auto it = begin; it != end && !hitCap; ++it) {
    hitCap = collectUniqueCodepoints(asCStr(*it), codepoints, cpCount, MAX_UNIQUE_CODEPOINTS);
  }

  if (includeSpace && std::none_of(codepoints, codepoints + cpCount, [](uint32_t c) { return c == ' '; })) {
    codepoints[cpCount++] = ' ';
  }
  if (includeHyphen && std::none_of(codepoints, codepoints + cpCount, [](uint32_t c) { return c == '-'; })) {
    codepoints[cpCount++] = '-';
  }

  if (hitCap) {
    LOG_ERR("SDCF", "buildAdvanceTable: unique codepoint cap (%u) hit, layout may be approximate",
            MAX_UNIQUE_CODEPOINTS);
  }

  std::sort(codepoints, codepoints + cpCount);
  const int totalMissed = fetchAdvancesForCodepoints(codepoints, cpCount, styleMask);
  stats_.prewarmTotalMs = millis() - startMs;
  return totalMissed;
}

int SdCardFont::buildAdvanceTable(const std::vector<std::string>& words, bool includeHyphen, uint8_t styleMask) {
  return buildAdvanceTableRange(words.begin(), words.end(), words.size() > 1, includeHyphen, styleMask);
}

// --- Stats ---

void SdCardFont::logStats(const char* label) {
  LOG_DBG("SDCF", "[%s] total=%ums sd_read=%ums seeks=%u glyphs=%u bitmap=%u bytes", label, stats_.prewarmTotalMs,
          stats_.sdReadTimeMs, stats_.seekCount, stats_.uniqueGlyphs, stats_.bitmapBytes);
}

void SdCardFont::resetStats() { stats_ = Stats{}; }

// --- Public accessors ---

EpdFont* SdCardFont::getEpdFont(uint8_t style) {
  style &= (MAX_STYLES - 1);
  if (!styles_[style].present) return nullptr;
  return &styles_[style].epdFont;
}

uint8_t SdCardFont::resolveStyle(uint8_t style) const {
  static const uint8_t kFallbacks[MAX_STYLES][MAX_STYLES] = {
      {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC},
      {EpdFontFamily::BOLD, EpdFontFamily::REGULAR, EpdFontFamily::BOLD_ITALIC, EpdFontFamily::ITALIC},
      {EpdFontFamily::ITALIC, EpdFontFamily::REGULAR, EpdFontFamily::BOLD_ITALIC, EpdFontFamily::BOLD},
      {EpdFontFamily::BOLD_ITALIC, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::REGULAR},
  };

  const uint8_t styleBits = style & (MAX_STYLES - 1);
  for (uint8_t candidate : kFallbacks[styleBits]) {
    if (styles_[candidate].present) {
      return candidate;
    }
  }
  return EpdFontFamily::REGULAR;
}

uint8_t SdCardFont::resolveStyleMask(uint8_t styleMask) const {
  uint8_t resolvedMask = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (styleMask & (1 << si)) {
      resolvedMask |= static_cast<uint8_t>(1u << resolveStyle(si));
    }
  }
  return resolvedMask;
}

bool SdCardFont::hasStyle(uint8_t style) const { return styles_[style & (MAX_STYLES - 1)].present; }

// --- On-demand glyph loading (overflow buffer) ---

const EpdGlyph* SdCardFont::onGlyphMiss(void* ctx, uint32_t codepoint) {
  auto* oc = static_cast<OverflowContext*>(ctx);
  auto* self = oc->self;
  uint8_t styleIdx = oc->styleIdx;

  if (!self->loaded_ || styleIdx >= MAX_STYLES || !self->styles_[styleIdx].present) return nullptr;
  const auto& s = self->styles_[styleIdx];
  if (!s.fullIntervals) return nullptr;

  for (uint32_t i = 0; i < self->overflowCount_; i++) {
    if (self->overflow_[i].codepoint == codepoint && self->overflow_[i].styleIdx == styleIdx) {
      return &self->overflow_[i].glyph;
    }
  }

  int32_t globalIdx = self->findGlobalGlyphIndex(s, codepoint);
  if (globalIdx < 0) return nullptr;

  uint32_t slot = self->overflowNext_;
  bool wasAtCapacity = (self->overflowCount_ == OVERFLOW_CAPACITY);

  FsFile file;
  if (!Storage.openFileForRead("SDCF", self->filePath_, file)) {
    LOG_ERR("SDCF", "Overflow: failed to open .cpfont");
    return nullptr;
  }

  EpdGlyph tempGlyph = {};
  uint32_t glyphFileOff = s.glyphsFileOffset + static_cast<uint32_t>(globalIdx) * sizeof(EpdGlyph);
  if (!file.seekSet(glyphFileOff)) {
    LOG_ERR("SDCF", "Overflow: failed to seek to glyph for U+%04X style %u", codepoint, styleIdx);
    file.close();
    return nullptr;
  }
  if (file.read(reinterpret_cast<uint8_t*>(&tempGlyph), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
    LOG_ERR("SDCF", "Overflow: failed to read glyph metadata for U+%04X style %u", codepoint, styleIdx);
    file.close();
    return nullptr;
  }

  uint8_t* tempBitmap = nullptr;
  if (tempGlyph.dataLength > 0) {
    if (tempGlyph.dataLength > self->overflow_[slot].bitmapCap) {
      delete[] self->overflow_[slot].bitmap;
      self->overflow_[slot].bitmap = nullptr;
      self->overflow_[slot].bitmapCap = 0;
      tempBitmap = new (std::nothrow) uint8_t[tempGlyph.dataLength];
      if (!tempBitmap) {
        LOG_ERR("SDCF", "Overflow: failed to allocate %u bytes for U+%04X bitmap", tempGlyph.dataLength, codepoint);
        file.close();
        return nullptr;
      }
    } else {
      tempBitmap = self->overflow_[slot].bitmap;
    }
    if (!file.seekSet(s.bitmapFileOffset + tempGlyph.dataOffset)) {
      LOG_ERR("SDCF", "Overflow: failed to seek to bitmap for U+%04X", codepoint);
      if (tempBitmap != self->overflow_[slot].bitmap) {
        delete[] tempBitmap;
      }
      file.close();
      return nullptr;
    }
    if (file.read(tempBitmap, tempGlyph.dataLength) != static_cast<int>(tempGlyph.dataLength)) {
      LOG_ERR("SDCF", "Overflow: failed to read bitmap for U+%04X", codepoint);
      if (tempBitmap != self->overflow_[slot].bitmap) {
        delete[] tempBitmap;
      }
      file.close();
      return nullptr;
    }
  } else {
    delete[] self->overflow_[slot].bitmap;
    self->overflow_[slot].bitmap = nullptr;
    self->overflow_[slot].bitmapCap = 0;
  }

  if (!wasAtCapacity) {
    self->overflowCount_++;
  }
  self->overflowNext_ = (slot + 1) % OVERFLOW_CAPACITY;
  self->overflow_[slot].glyph = tempGlyph;
  self->overflow_[slot].bitmap = tempBitmap;
  if (tempBitmap && tempGlyph.dataLength > self->overflow_[slot].bitmapCap) {
    self->overflow_[slot].bitmapCap = tempGlyph.dataLength;
  }
  self->overflow_[slot].codepoint = codepoint;
  self->overflow_[slot].styleIdx = styleIdx;

  LOG_DBG("SDCF", "Overflow: loaded U+%04X style %u on demand (slot %u/%u)", codepoint, styleIdx, slot,
          OVERFLOW_CAPACITY);

  file.close();
  return &self->overflow_[slot].glyph;
}

bool SdCardFont::isOverflowGlyph(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) return true;
  }
  return false;
}

const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) {
      return overflow_[i].bitmap;
    }
  }
  return nullptr;
}

SdCardFont* SdCardFont::fromMissCtx(void* ctx) { return static_cast<OverflowContext*>(ctx)->self; }