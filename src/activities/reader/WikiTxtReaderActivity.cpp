#include "WikiTxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <cstdlib> // Aggiunto per malloc/free
#include <cstring>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SdCardFontGlobals.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version (WikiTxt = "WIKI")
constexpr uint32_t CACHE_MAGIC = 0x5749494B;  // "WIKK"
constexpr uint8_t CACHE_VERSION = 1;
constexpr uint8_t MARKDOWN_QUOTE_INDENT = 1;
constexpr uint8_t MARKDOWN_LIST_INDENT = 1;

bool startsWithAt(const std::string& text, const size_t pos, const char* marker) {
  const size_t markerLen = strlen(marker);
  return pos + markerLen <= text.length() && text.compare(pos, markerLen, marker) == 0;
}

std::string trimMarkdownWhitespace(const std::string& text) {
  size_t begin = 0;
  while (begin < text.length() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    begin++;
  }
  size_t end = text.length();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end--;
  }
  return text.substr(begin, end - begin);
}

EpdFontFamily::Style combineMarkdownStyle(const EpdFontFamily::Style baseStyle, const bool bold, const bool italic) {
  uint8_t style = static_cast<uint8_t>(baseStyle);
  if (bold) {
    style |= EpdFontFamily::BOLD;
  }
  if (italic) {
    style |= EpdFontFamily::ITALIC;
  }
  return static_cast<EpdFontFamily::Style>(style & EpdFontFamily::BOLD_ITALIC);
}

void appendMarkdownSpan(WikiTxtReaderActivity::TextLine& line, const std::string& text,
                        const EpdFontFamily::Style style) {
  if (text.empty()) {
    return;
  }
  line.text += text;
  const uint8_t rawStyle = static_cast<uint8_t>(style);
  if (!line.spans.empty() && line.spans.back().style == rawStyle) {
    line.spans.back().text += text;
    return;
  }
  line.spans.push_back({text, rawStyle});
}

void parseMarkdownInlineSpans(WikiTxtReaderActivity::TextLine& line, const std::string& text,
                              const EpdFontFamily::Style baseStyle) {
  std::string buffer;
  bool bold = false;
  bool italic = false;
  bool code = false;

  const auto flush = [&]() {
    appendMarkdownSpan(line, buffer, code ? baseStyle : combineMarkdownStyle(baseStyle, bold, italic));
    buffer.clear();
  };

  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '\\' && i + 1 < text.length()) {
      buffer += text[++i];
      continue;
    }

    if (c == '`') {
      flush();
      code = !code;
      continue;
    }

    if (!code && c == '!' && i + 1 < text.length() && text[i + 1] == '[') {
      i++;
      continue;
    }

    if (!code && text[i] == '[') {
      const size_t labelEnd = text.find(']', i + 1);
      if (labelEnd != std::string::npos && labelEnd + 1 < text.length() && text[labelEnd + 1] == '(') {
        const size_t urlEnd = text.find(')', labelEnd + 2);
        if (urlEnd != std::string::npos) {
          flush();
          parseMarkdownInlineSpans(line, text.substr(i + 1, labelEnd - i - 1),
                                   combineMarkdownStyle(baseStyle, bold, italic));
          i = urlEnd;
          continue;
        }
      }
    }

    if (!code && (c == '*' || c == '_')) {
      const bool triple = i + 2 < text.length() && text[i + 1] == c && text[i + 2] == c;
      const bool doubleMarker = i + 1 < text.length() && text[i + 1] == c;
      if (triple || doubleMarker) {
        flush();
        if (triple) {
          bold = !bold;
          italic = !italic;
          i += 2;
        } else {
          bold = !bold;
          i++;
        }
        continue;
      }
      if (text.find(c, i + 1) != std::string::npos) {
        flush();
        italic = !italic;
        continue;
      }
    }

    buffer += c;
  }

  flush();
}

WikiTxtReaderActivity::TextLine parseMarkdownLine(const std::string& rawLine,
                                                  const uint8_t fallbackAlignment = CrossPointSettings::LEFT_ALIGN) {
  WikiTxtReaderActivity::TextLine line;
  line.alignment = fallbackAlignment;

  std::string text = rawLine;
  size_t pos = 0;
  while (pos < text.length() && pos < 3 && text[pos] == ' ') {
    pos++;
  }
  text = text.substr(pos);

  if (text.empty()) {
    return line;
  }

  int headerLevel = 0;
  while (headerLevel < 6 && headerLevel < static_cast<int>(text.length()) && text[headerLevel] == '#') {
    headerLevel++;
  }
  if (headerLevel > 0 && headerLevel < static_cast<int>(text.length()) &&
      std::isspace(static_cast<unsigned char>(text[headerLevel]))) {
    line.style = EpdFontFamily::BOLD;
    line.alignment = CrossPointSettings::CENTER_ALIGN;
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(headerLevel + 1)), EpdFontFamily::BOLD);
    return line;
  }

  if (startsWithAt(text, 0, ">")) {
    size_t quotePos = 1;
    if (quotePos < text.length() && text[quotePos] == ' ') {
      quotePos++;
    }
    line.style = EpdFontFamily::ITALIC;
    line.indent = MARKDOWN_QUOTE_INDENT;
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(quotePos)), EpdFontFamily::ITALIC);
    return line;
  }

  if ((startsWithAt(text, 0, "- ") || startsWithAt(text, 0, "* ") || startsWithAt(text, 0, "+ "))) {
    line.indent = MARKDOWN_LIST_INDENT;
    appendMarkdownSpan(line, "- ", EpdFontFamily::REGULAR);
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(2)), EpdFontFamily::REGULAR);
    return line;
  }

  size_t numberPos = 0;
  while (numberPos < text.length() && std::isdigit(static_cast<unsigned char>(text[numberPos]))) {
    numberPos++;
  }
  if (numberPos > 0 && numberPos + 1 < text.length() && text[numberPos] == '.' && text[numberPos + 1] == ' ') {
    line.indent = MARKDOWN_LIST_INDENT;
    appendMarkdownSpan(line, text.substr(0, numberPos + 2), EpdFontFamily::REGULAR);
    parseMarkdownInlineSpans(line, trimMarkdownWhitespace(text.substr(numberPos + 2)), EpdFontFamily::REGULAR);
    return line;
  }

  if ((startsWithAt(text, 0, "```") || startsWithAt(text, 0, "~~~"))) {
    line.text.clear();
    return line;
  }

  parseMarkdownInlineSpans(line, text, EpdFontFamily::REGULAR);
  return line;
}

int getTextLineWidth(GfxRenderer& renderer, const int fontId, const WikiTxtReaderActivity::TextLine& line) {
  if (line.spans.empty()) {
    return renderer.getTextAdvanceX(fontId, line.text.c_str(), static_cast<EpdFontFamily::Style>(line.style));
  }
  int width = 0;
  for (const auto& span : line.spans) {
    width += renderer.getTextAdvanceX(fontId, span.text.c_str(), static_cast<EpdFontFamily::Style>(span.style));
  }
  return width;
}

WikiTxtReaderActivity::TextLine sliceTextLine(const WikiTxtReaderActivity::TextLine& source, const size_t begin,
                                              const size_t length) {
  WikiTxtReaderActivity::TextLine out;
  out.style = source.style;
  out.alignment = source.alignment;
  out.indent = source.indent;

  const size_t end = begin + length;
  size_t spanBegin = 0;
  for (const auto& span : source.spans) {
    const size_t spanEnd = spanBegin + span.text.length();
    if (spanEnd > begin && spanBegin < end) {
      const size_t localBegin = begin > spanBegin ? begin - spanBegin : 0;
      const size_t localEnd = std::min(span.text.length(), end - spanBegin);
      std::string part = span.text.substr(localBegin, localEnd - localBegin);
      out.text += part;
      out.spans.push_back({std::move(part), span.style});
    }
    spanBegin = spanEnd;
  }

  if (out.spans.empty() && !source.text.empty()) {
    out.text = source.text.substr(begin, length);
    out.spans.push_back({out.text, source.style});
  }

  return out;
}
}  // namespace

WikiTxtReaderActivity::WikiTxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string wikiDir, std::string title)
    : Activity("WikiTxtReader", renderer, mappedInput),
      wikiDir(std::move(wikiDir)),
      title(std::move(title)) {}

bool WikiTxtReaderActivity::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  const std::string articlePath = wikiDir + "/article.md";
  HalFile file; // Corretto: FsFile -> HalFile per coerenza con HalStorage
  if (!Storage.openFileForRead("WIKITXT", articlePath.c_str(), file)) {
    return false;
  }
  if (!file.seek(offset)) {
    return false;
  }
  size_t bytesRead = file.read(buffer, length);
  return bytesRead > 0;
}

void WikiTxtReaderActivity::onEnter() {
  Activity::onEnter();
  ensureSdFontLoaded();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  const std::string articlePath = wikiDir + "/article.md";
  HalFile f; // Corretto: FsFile -> HalFile
  if (Storage.openFileForRead("WIKITXT", articlePath.c_str(), f)) {
    fileSize = f.size();
    f.close();
  } else {
    fileSize = 0;
  }

  requestUpdate();
}

void WikiTxtReaderActivity::onExit() {
  Activity::onExit();
  ReaderUtils::requestReaderUiTransitionRefresh(renderer);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  saveProgress(currentPage, pageOffsets.empty() ? 0 : pageOffsets[currentPage]);
  pageOffsets.clear();
  currentPageLines.clear();
}

void WikiTxtReaderActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt, fromFrontButton,
        upBtn, downBtn, leftBtn, rightBtn] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered && currentPage < totalPages - 1) {
    currentPage++;
    requestUpdate();
  }
}

void WikiTxtReaderActivity::render(RenderLock&&) {
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  size_t offset = pageOffsets[currentPage];
  size_t nextOffset = 0;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  saveProgress(currentPage, pageOffsets.empty() ? 0 : pageOffsets[currentPage]);
}

void WikiTxtReaderActivity::renderPage() {
  const int lineHeight = std::max(1, static_cast<int>(renderer.getLineHeight(cachedFontId) * SETTINGS.getReaderLineCompression() + 0.5f));
  const int contentWidth = viewportWidth;

  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.text.empty()) {
        const auto lineStyle = static_cast<EpdFontFamily::Style>(line.style);
        const int indentPx = line.indent * renderer.getSpaceWidth(cachedFontId, lineStyle) * 2;
        int x = cachedOrientedMarginLeft;

        switch (line.alignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = getTextLineWidth(renderer, cachedFontId, line);
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = getTextLineWidth(renderer, cachedFontId, line);
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }
        if (line.alignment == CrossPointSettings::LEFT_ALIGN || line.alignment == CrossPointSettings::JUSTIFIED) {
          x += indentPx;
        }

        if (line.spans.empty()) {
          renderer.drawText(cachedFontId, x, y, line.text.c_str(), true, lineStyle);
        } else {
          int spanX = x;
          for (const auto& span : line.spans) {
            const auto spanStyle = static_cast<EpdFontFamily::Style>(span.style);
            renderer.drawText(cachedFontId, spanX, y, span.text.c_str(), true, spanStyle);
            spanX += renderer.getTextAdvanceX(cachedFontId, span.text.c_str(), spanStyle);
          }
        }
      }
      y += lineHeight;
    }
  };

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();
  scope.endScanAndPrewarm();

  renderLines();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
}

void WikiTxtReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title.c_str());
}

ScreenshotInfo WikiTxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}

void WikiTxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  cachedOrientedMarginBottom += std::max(static_cast<int>(cachedScreenMargin), static_cast<int>(statusBarHeight));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = std::max(1, static_cast<int>(renderer.getLineHeight(cachedFontId) * SETTINGS.getReaderLineCompression() + 0.5f));

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  if (!loadPageIndexCache()) {
    buildPageIndex();
    savePageIndexCache();
  }

  loadProgress();

  initialized = true;
}

void WikiTxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  // Preallocare evita le continue riallocazioni del vector durante la scansione,
  // riducendo la frammentazione dell'heap (particolarmente critica sull'X4/C3).
  if (fileSize > 0) {
    const size_t estPages = fileSize / 512 + 4;
    pageOffsets.reserve(std::max(size_t{2}, estPages));
  }
  pageOffsets.push_back(0);

  size_t offset = 0;
  LOG_DBG("WIKITXT", "Building page index for %zu bytes...", fileSize);
  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<TextLine> tempLines;
    size_t nextOffset = offset;
    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = static_cast<int>(pageOffsets.size());
  LOG_DBG("WIKITXT", "Built page index: %d pages", totalPages);
}

bool WikiTxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<TextLine>& outLines, size_t& nextOffset) {
  outLines.clear();

  if (offset >= fileSize) {
    return false;
  }

  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("WIKITXT", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x0F);
  }

  size_t pos = 0;
  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);
    if (!lineComplete && !outLines.empty()) {
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    const std::string sourceLine(reinterpret_cast<char*>(buffer + pos), displayLen);
    TextLine lineInfo = parseMarkdownLine(sourceLine, cachedParagraphAlignment);
    if (lineInfo.text.empty() && trimMarkdownWhitespace(sourceLine).empty() &&
        static_cast<int>(outLines.size()) < linesPerPage) {
      outLines.push_back(std::move(lineInfo));
      pos = lineEnd + 1;
      continue;
    }

    size_t wrappedLineStart = 0;
    size_t lineBytePos = 0;
    while (wrappedLineStart < lineInfo.text.length() && static_cast<int>(outLines.size()) < linesPerPage) {
      const std::string line = lineInfo.text.substr(wrappedLineStart);
      const int indentPx = lineInfo.indent * renderer.getSpaceWidth(cachedFontId, EpdFontFamily::REGULAR) * 2;
      const int lineViewportWidth = std::max(1, viewportWidth - indentPx);
      const size_t displayLen2 = line.length();

      if (lineBytePos == 0 && static_cast<int>(outLines.size()) == linesPerPage - 1) {
        lineBytePos += displayLen2;
      }

      int lineWidth = getTextLineWidth(renderer, cachedFontId, sliceTextLine(lineInfo, wrappedLineStart, line.length()));
      if (lineWidth <= lineViewportWidth) {
        TextLine displayLine = sliceTextLine(lineInfo, wrappedLineStart, line.length());
        outLines.push_back(std::move(displayLine));
        lineBytePos = displayLen2;
        wrappedLineStart = lineInfo.text.length();
        break;
      }

      size_t breakPos = line.length();
      while (breakPos > 0 &&
             getTextLineWidth(renderer, cachedFontId, sliceTextLine(lineInfo, wrappedLineStart, breakPos)) >
                 lineViewportWidth) {
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          breakPos--;
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      TextLine displayLine = sliceTextLine(lineInfo, wrappedLineStart, breakPos);
      outLines.push_back(std::move(displayLine));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      wrappedLineStart += skipChars;
    }

    if (wrappedLineStart >= lineInfo.text.length()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);
  return !outLines.empty();
}

bool WikiTxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = wikiDir + "/index.bin";
  HalFile f; // Corretto: FsFile -> HalFile
  if (!Storage.openFileForRead("WIKITXT", cachePath, f)) {
    return false;
  }

  uint32_t magic = 0;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    return false;
  }

  uint8_t version = 0;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    return false;
  }

  uint32_t cachedSize = 0;
  serialization::readPod(f, cachedSize);
  if (cachedSize != fileSize) {
    return false;
  }

  int32_t cachedWidth = 0;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    return false;
  }

  int32_t cachedLines = 0;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    return false;
  }

  int32_t fontId = 0;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    return false;
  }

  int32_t margin = 0;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    return false;
  }

  uint8_t alignment = 0;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    return false;
  }

  uint32_t numPages = 0;
  serialization::readPod(f, numPages);
  pageOffsets.clear();
  pageOffsets.reserve(numPages);
  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offsetVal = 0;
    serialization::readPod(f, offsetVal);
    pageOffsets.push_back(offsetVal);
  }

  totalPages = static_cast<int>(pageOffsets.size());
  return true;
}

void WikiTxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = wikiDir + "/index.bin";
  HalFile f; // Corretto: FsFile -> HalFile
  if (!Storage.openFileForWrite("WIKITXT", cachePath, f)) {
    LOG_ERR("WIKITXT", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(fileSize));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("WIKITXT", "Saved page index cache: %d pages", totalPages);
}

bool WikiTxtReaderActivity::saveProgress(int page, size_t offset) const {
  if (!initialized) return false;
  const std::string progressPath = wikiDir + "/progress.bin";
  HalFile f;
  if (!Storage.openFileForWrite("WIKITXT", progressPath, f)) {
    return false;
  }
  uint8_t data[6];
  data[0] = static_cast<uint8_t>(page & 0xFF);
  data[1] = static_cast<uint8_t>((page >> 8) & 0xFF);
  data[2] = static_cast<uint8_t>(offset & 0xFF);
  data[3] = static_cast<uint8_t>((offset >> 8) & 0xFF);
  data[4] = static_cast<uint8_t>((offset >> 16) & 0xFF);
  data[5] = static_cast<uint8_t>((offset >> 24) & 0xFF);
  const bool written = f.write(data, sizeof(data)) == sizeof(data);
  f.close();
  if (!written) {
    LOG_ERR("WIKITXT", "Short write saving reader progress");
  }
  return written;
}

bool WikiTxtReaderActivity::loadProgress() {
  const std::string progressPath = wikiDir + "/progress.bin";
  HalFile f;
  if (Storage.openFileForRead("WIKITXT", progressPath, f)) {
    uint8_t data[6] = {0};
    const int n = f.read(data, sizeof(data));
    f.close();
    if (n >= 2) {
      currentPage = static_cast<int>((static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)));
      if (currentPage >= totalPages && totalPages > 0) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) currentPage = 0;
      LOG_DBG("WIKITXT", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
  return true;
}

bool WikiTxtReaderActivity::drawCurrentPageToBuffer(const std::string& wikiDir, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  WikiTxtReaderActivity reader(renderer, mappedInput, wikiDir, "");
  reader.initializeReader();
  if (reader.pageOffsets.empty()) {
    return false;
  }

  size_t offset = reader.pageOffsets[reader.currentPage];
  size_t nextOffset = 0;
  std::vector<WikiTxtReaderActivity::TextLine> pageLines;
  reader.loadPageAtOffset(offset, pageLines, nextOffset);
  if (pageLines.empty()) return false;

  const int lineHeight = std::max(1, static_cast<int>(renderer.getLineHeight(reader.cachedFontId) * SETTINGS.getReaderLineCompression() + 0.5f));
  const int contentWidth = reader.viewportWidth;

  renderer.clearScreen();
  int y = reader.cachedOrientedMarginTop;
  for (const auto& line : pageLines) {
    if (!line.text.empty()) {
      int x = reader.cachedOrientedMarginLeft;
      const bool lineIsRtl = BidiUtils::startsWithRtl(line.text.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
      uint8_t effectiveAlignment = reader.cachedParagraphAlignment;
      if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                        effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
        effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
      }
      const int textWidth = renderer.getTextAdvanceX(reader.cachedFontId, line.text.c_str(), EpdFontFamily::REGULAR);

      switch (effectiveAlignment) {
        case CrossPointSettings::CENTER_ALIGN:
          x = reader.cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
          break;
        case CrossPointSettings::RIGHT_ALIGN:
          x = reader.cachedOrientedMarginLeft + contentWidth - textWidth;
          break;
        default:
          break;
      }

      if (line.spans.empty()) {
        renderer.drawText(reader.cachedFontId, x, y, line.text.c_str(), true);
      } else {
        int spanX = x;
        for (const auto& span : line.spans) {
          const auto spanStyle = static_cast<EpdFontFamily::Style>(span.style);
          renderer.drawText(reader.cachedFontId, spanX, y, span.text.c_str(), true, spanStyle);
          spanX += renderer.getTextAdvanceX(reader.cachedFontId, span.text.c_str(), spanStyle);
        }
      }
    }
    y += lineHeight;
  }
  return true;
}