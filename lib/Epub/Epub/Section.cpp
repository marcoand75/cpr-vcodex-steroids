#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <Serialization.h>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v44: magic bytes + version bump with cache-busting fields for Bionic Reading,
// Guide Dots, and EPUB Render Mode. Flat TextBlock word arena deferred to B2.
constexpr uint32_t SECTION_CACHE_MAGIC = 0x535843FF;  // bytes: 0xFF, "CXS"
constexpr uint8_t SECTION_FILE_VERSION = 44;
// Size of all fields in the section file header BEFORE the patch area (pageCount + offsets).
// Matches the write order in writeSectionFileHeader up to (but not including) pageCount.
constexpr uint32_t HEADER_FIELDS_SIZE = sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(int) +
                                         sizeof(float) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) +
                                         sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(bool) +
                                         sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t);
// v44: magic(4) + version(1) + fontId(4) + lineCompression(4) + extraParagraphSpacing(1) +
//      forceParagraphIndents(1) + paragraphAlignment(1) + viewportWidth(2) + viewportHeight(2) +
//      hyphenationEnabled(1) + focusReadingEnabled(1) + embeddedStyle(1) + imageRendering(1) +
//      bionicReadingEnabled(1) + guideDotMinGap(1) + renderMode(1) = 28
static_assert(HEADER_FIELDS_SIZE == 27, "Header fields size should be 27");

constexpr uint32_t HEADER_SIZE = HEADER_FIELDS_SIZE + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                  sizeof(uint32_t) + sizeof(uint32_t);
// 28 + pageCount(2) + lutOffset(4) + anchorMapOffset(4) + paragraphLutOffset(4) + liLutOffset(4) = 46

struct PageLutEntry {
  uint32_t fileOffset;
  uint32_t xhtmlByteOffset;
  uint16_t paragraphIndex;
  uint16_t listItemIndex;
};

constexpr uint32_t PARAGRAPH_LUT_ENTRY_SIZE = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
}  // namespace

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }
  if (!page) {
    LOG_ERR("SCT", "Null page for page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                                     const uint16_t viewportWidth, const uint16_t viewportHeight,
                                     const bool hyphenationEnabled, const bool focusReadingEnabled,
                                     const bool embeddedStyle, const uint8_t imageRendering,
                                     const bool bionicReadingEnabled, const uint8_t guideDotMinGap,
                                     const uint8_t renderMode) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(fontId) +
                                   sizeof(lineCompression) + sizeof(extraParagraphSpacing) +
                                   sizeof(forceParagraphIndents) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(pageCount) + sizeof(hyphenationEnabled) +
                                   sizeof(focusReadingEnabled) + sizeof(embeddedStyle) + sizeof(imageRendering) +
                                   sizeof(bionicReadingEnabled) + sizeof(guideDotMinGap) + sizeof(uint8_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                 "Header size mismatch");
  serialization::writePod(file, SECTION_CACHE_MAGIC);
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, forceParagraphIndents);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, focusReadingEnabled);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, imageRendering);
  serialization::writePod(file, bionicReadingEnabled);
  serialization::writePod(file, guideDotMinGap);
  serialization::writePod(file, renderMode);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for li LUT offset (patched later)
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                              const uint16_t viewportWidth, const uint16_t viewportHeight,
                              const bool hyphenationEnabled, const bool focusReadingEnabled, const bool embeddedStyle,
                              const uint8_t imageRendering, const bool bionicReadingEnabled,
                              const uint8_t guideDotMinGap, const uint8_t renderMode) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint32_t magic;
    serialization::readPod(file, magic);
    if (magic != SECTION_CACHE_MAGIC) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: unknown cache magic 0x%08X", magic);
      clearCache();
      return false;
    }

    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    bool fileForceParagraphIndents;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileFocusReadingEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileBionicReadingEnabled;
    uint8_t fileGuideDotMinGap;
    uint8_t fileRenderMode;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileForceParagraphIndents);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileFocusReadingEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileBionicReadingEnabled);
    serialization::readPod(file, fileGuideDotMinGap);
    serialization::readPod(file, fileRenderMode);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || forceParagraphIndents != fileForceParagraphIndents ||
        paragraphAlignment != fileParagraphAlignment || viewportWidth != fileViewportWidth ||
        viewportHeight != fileViewportHeight || hyphenationEnabled != fileHyphenationEnabled ||
        focusReadingEnabled != fileFocusReadingEnabled || embeddedStyle != fileEmbeddedStyle ||
        imageRendering != fileImageRendering || bionicReadingEnabled != fileBionicReadingEnabled ||
        guideDotMinGap != fileGuideDotMinGap || renderMode != fileRenderMode) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages", pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                                const uint16_t viewportWidth, const uint16_t viewportHeight,
                                const bool hyphenationEnabled, const bool focusReadingEnabled, const bool embeddedStyle,
                                const uint8_t imageRendering, const std::function<void()>& popupFn,
                                const bool bionicReadingEnabled, const uint8_t guideDotMinGap,
                                const uint8_t renderMode) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
    }

    FsFile tmpHtml;
    if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
    fileSize = tmpHtml.size();
    // Explicitly close() file before calling Storage.remove()
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
      LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
    }
  }

  if (!success) {
    LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
    return false;
  }

  LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

  if (!Storage.openFileForWrite("SCT", filePath, file)) {
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents, paragraphAlignment,
                         viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, embeddedStyle,
                         imageRendering, bionicReadingEnabled, guideDotMinGap, renderMode);
  std::vector<PageLutEntry> lut = {};

  // Derive the content base directory and image cache path prefix for the parser
  size_t lastSlash = localPath.find_last_of('/');
  std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  std::string imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  CssParser* cssParser = nullptr;
  if (embeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  ChapterHtmlSlimParser visitor(
      epub, tmpHtmlPath, renderer, fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents,
      paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, guideDotMinGap,
      [this, &lut](std::unique_ptr<Page> page, const ChapterHtmlSlimParser::ParagraphLutEntry syncEntry) {
        lut.push_back({this->onPageComplete(std::move(page)), syncEntry.xhtmlByteOffset, syncEntry.paragraphIndex,
                       syncEntry.listItemIndex});
      },
      embeddedStyle, contentBase, imageBasePath, imageRendering, std::move(tocAnchors), popupFn, cssParser);
  Hyphenator::setPreferredLanguage(epub->getLanguage());
  success = visitor.parseAndBuildPages();

  Storage.remove(tmpHtmlPath.c_str());
  if (!success) {
    const auto heap = MemoryBudget::snapshot();
    LOG_ERR("SCT", "Failed to parse XML and build pages (lowMemoryAbort=%u imageFallback=%u free=%u maxAlloc=%u)",
            visitor.wasLowMemoryAbortTriggered() ? 1U : 0U, visitor.wasLowMemoryFallbackTriggered() ? 1U : 0U,
            heap.freeHeap, heap.maxAllocHeap);
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove(filePath.c_str());
    if (cssParser) {
      cssParser->clear();
    }
    return false;
  }
  if (visitor.wasLowMemoryFallbackTriggered()) {
    const auto heap = MemoryBudget::snapshot();
    LOG_DBG("SCT", "Section built with low-memory image fallback (free=%u maxAlloc=%u)", heap.freeHeap,
            heap.maxAllocHeap);
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  // Write LUT
  for (const auto& entry : lut) {
    if (entry.fileOffset == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, entry.fileOffset);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets)
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = visitor.getAnchors();
  serialization::writePod(file, static_cast<uint16_t>(anchors.size()));
  for (const auto& [anchor, page] : anchors) {
    serialization::writeString(file, anchor);
    serialization::writePod(file, page);
  }

  const uint32_t paragraphLutOffset = file.position();
  serialization::writePod(file, static_cast<uint16_t>(lut.size()));
  for (const auto& entry : lut) {
    serialization::writePod(file, entry.xhtmlByteOffset);
    serialization::writePod(file, entry.paragraphIndex);
    serialization::writePod(file, entry.listItemIndex);
  }

  // v44: li LUT offset placeholder (reserved for future use, same as paragraph LUT)
  const uint32_t liLutOffset = file.position();
  serialization::writePod(file, static_cast<uint16_t>(0));

  // Patch header with final pageCount, lutOffset, anchorMapOffset, paragraphLutOffset, liLutOffset
  // v44 header: magic(4) + version(1) + ... fields ... + pageCount(4) + lutOffset(4) + anchorMapOffset(4) +
  //             paragraphLutOffset(4) + liLutOffset(4)
  // HEADER_SIZE accounts for all of the above.
  // To patch: seek to offset after the renderMode field and before pageCount
  const uint32_t patchStart = sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(fontId) +
                              sizeof(lineCompression) + sizeof(extraParagraphSpacing) +
                              sizeof(forceParagraphIndents) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                              sizeof(viewportHeight) + sizeof(hyphenationEnabled) + sizeof(focusReadingEnabled) +
                              sizeof(embeddedStyle) + sizeof(imageRendering) + sizeof(bionicReadingEnabled) +
                              sizeof(guideDotMinGap) + sizeof(uint8_t);
  file.seek(patchStart);
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, anchorMapOffset);
  serialization::writePod(file, paragraphLutOffset);
  serialization::writePod(file, liLutOffset);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  if (cssParser) {
    cssParser->clear();
  }
  return true;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  if (currentPage < 0 || currentPage >= pageCount) {
    LOG_ERR("SCT", "Page load out of bounds: %d/%u", currentPage, pageCount);
    file.close();
    return nullptr;
  }

  const uint32_t fileSize = file.size();
  file.seek(HEADER_FIELDS_SIZE + sizeof(uint16_t));  // skip fields, then pageCount(uint16_t)
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  if (lutOffset == 0 || lutOffset >= fileSize) {
    LOG_ERR("SCT", "Invalid LUT offset: %u size=%u", lutOffset, fileSize);
    file.close();
    return nullptr;
  }

  const uint32_t lutEntryOffset = lutOffset + sizeof(uint32_t) * static_cast<uint32_t>(currentPage);
  if (lutEntryOffset + sizeof(uint32_t) > fileSize) {
    LOG_ERR("SCT", "Invalid LUT entry offset: %u size=%u", lutEntryOffset, fileSize);
    file.close();
    return nullptr;
  }

  file.seek(lutEntryOffset);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  if (pagePos < HEADER_SIZE || pagePos >= lutOffset || pagePos >= fileSize) {
    LOG_ERR("SCT", "Invalid page offset: %u lut=%u size=%u", pagePos, lutOffset, fileSize);
    file.close();
    return nullptr;
  }

  file.seek(pagePos);

  auto page = Page::deserialize(file);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  return page;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_FIELDS_SIZE + sizeof(uint16_t) + sizeof(uint32_t));  // skip fields, pageCount, lutOffset
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  uint16_t count;
  serialization::readPod(f, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(f, key);
    serialization::readPod(f, page);
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_FIELDS_SIZE + sizeof(uint16_t) + sizeof(uint32_t) * 2);  // skip fields, pageCount, lutOffset, anchorMapOffset
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * PARAGRAPH_LUT_ENTRY_SIZE;
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t xhtmlByteOffset;
    uint16_t pagePIdx;
    uint16_t listItemIndex;
    serialization::readPod(f, xhtmlByteOffset);
    serialization::readPod(f, pagePIdx);
    serialization::readPod(f, listItemIndex);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  // v44: paragraphLutOffset is after pageCount, lutOffset, anchorMapOffset
  const uint32_t paragraphLutFieldPos = sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(int) +
                                        sizeof(float) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint16_t) +
                                        sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(bool) +
                                        sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t);
  f.seek(paragraphLutFieldPos + sizeof(uint16_t) + sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * PARAGRAPH_LUT_ENTRY_SIZE;
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t xhtmlByteOffset;
    uint16_t paragraphIndex;
    uint16_t pageLiIdx;
    serialization::readPod(f, xhtmlByteOffset);
    serialization::readPod(f, paragraphIndex);
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  // v44: paragraphLutOffset is after pageCount, lutOffset, anchorMapOffset
  const uint32_t paragraphLutFieldPos = sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(int) +
                                        sizeof(float) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint16_t) +
                                        sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(bool) +
                                        sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t);
  f.seek(paragraphLutFieldPos + sizeof(uint16_t) + sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * PARAGRAPH_LUT_ENTRY_SIZE;
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * PARAGRAPH_LUT_ENTRY_SIZE + sizeof(uint32_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  return pIdx;
}

std::optional<uint32_t> Section::getXhtmlByteOffsetForPage(const uint16_t page) const {
  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  // v44: paragraphLutOffset is after pageCount, lutOffset, anchorMapOffset
  const uint32_t paragraphLutFieldPos = sizeof(SECTION_CACHE_MAGIC) + sizeof(SECTION_FILE_VERSION) + sizeof(int) +
                                        sizeof(float) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint16_t) +
                                        sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(bool) +
                                        sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t);
  f.seek(paragraphLutFieldPos + sizeof(uint16_t) + sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * PARAGRAPH_LUT_ENTRY_SIZE;
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * PARAGRAPH_LUT_ENTRY_SIZE);
  uint32_t xhtmlByteOffset;
  serialization::readPod(f, xhtmlByteOffset);
  if (xhtmlByteOffset == 0) {
    return std::nullopt;
  }
  return xhtmlByteOffset;
}
