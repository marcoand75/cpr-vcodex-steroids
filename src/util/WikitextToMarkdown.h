#pragma once

#include <HalStorage.h>

/**
 * Streaming converter that turns a MediaWiki "action=parse&prop=wikitext" JSON
 * response stored on SD into a plain-text markdown file on SD.
 *
 * It scans for the "wikitext" -> "*" string value, decodes JSON escapes on the
 * fly (no full RAM copy of the wikitext), and applies a line-oriented wikitext
 * to markdown transformation, writing the result straight to the output file.
 *
 * Deliberately independent of HtmlToTxt: Wikipedia content is now downloaded as
 * wikitext, not HTML, so the old HTML parser is preserved but not used here.
 */
class WikitextToMarkdown {
 public:
  /**
   * Convert a raw wikitext JSON file into a markdown file.
   * \param in    Open (for read) HalFile positioned at the start of the JSON.
   *              The file is read in its entirety; the stream position is not
   *              guaranteed to be restored.
   * \param outPath  Destination path for the markdown output.
   * \return true on success, false on IO errors or malformed JSON.
   */
  bool convert(HalFile& in, const char* outPath);

 private:
  static constexpr int OUT_BUF_SIZE = 512;
  char outBuf_[OUT_BUF_SIZE];
  int outBufLen_ = 0;
  HalFile* outFile_ = nullptr;

  void flush();
  void putChar(char c);
  void putStr(const char* s);
};
