#pragma once

#include <expat.h>

#include <cstdint>

// Resolves a footnote-preview anchor id to the element the preview should start rendering at.
//
// Thee target of a cross-reference is sometimes put on an empty inline element in
// the middle of a note paragraph (`(p. <span id="aEUZ"></span><a>81</a>)`). Rendering from that
// element drops the paragraph's leading text, so the preview starts mid-sentence. This locator
// walks the note file and reports the innermost enclosing block-level element instead.
//
// Elements are identified by ordinal (1-based, counting every start tag) so the render pass can
// find the same element without re-reading attributes: it counts start tags the same way and
// begins rendering when the ordinals match.
//
// The caller streams the file in through feed(); file access is deliberately kept outside so this
// stays host-testable.
class PreviewBlockLocator {
 public:
  // Returns true for tags that open a block-level box. Injected so the locator does not need the
  // renderer's tag tables, and so tests can supply their own.
  using IsBlockTagFn = bool (*)(const char* tagName);

  // anchorId must outlive the locator.
  PreviewBlockLocator(const char* anchorId, IsBlockTagFn isBlockTag);
  ~PreviewBlockLocator();

  PreviewBlockLocator(const PreviewBlockLocator&) = delete;
  PreviewBlockLocator& operator=(const PreviewBlockLocator&) = delete;

  // False if the parser could not be created; feed() is a no-op in that case.
  bool ok() const { return parser_ != nullptr; }

  // True once the anchor has been resolved. Stop feeding.
  bool done() const { return startOrdinal_ != 0; }

  // Feeds one chunk. Returns false on XML error, which the caller should treat as "not located".
  bool feed(const char* data, int len, bool isFinal);

  // Ordinal of the element to start rendering at, or 0 if the anchor was never found.
  uint32_t startOrdinal() const { return startOrdinal_; }

 private:
  // Block nesting deeper than this is not tracked; an anchor below it resolves to the innermost
  // block that is still tracked, which still beats starting at the anchor itself. Mirrors the
  // block-style stack depth in ChapterHtmlSlimParser.
  static constexpr uint8_t MAX_BLOCK_NESTING = 16;

  static void XMLCALL handleStartElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL handleEndElement(void* userData, const XML_Char* name);

  const char* anchorId_;
  IsBlockTagFn isBlockTag_;
  XML_Parser parser_ = nullptr;
  int depth_ = 0;
  uint32_t ordinal_ = 0;
  uint32_t startOrdinal_ = 0;

  // Ordinal and depth of each currently open block-level element, innermost last.
  uint32_t openBlockOrdinals_[MAX_BLOCK_NESTING] = {};
  int openBlockDepths_[MAX_BLOCK_NESTING] = {};
  uint8_t openBlockCount_ = 0;
};
