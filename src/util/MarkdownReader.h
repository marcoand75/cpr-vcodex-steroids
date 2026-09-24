#pragma once

#include <EpdFontFamily.h>

#include <string>
#include <vector>

/**
 * Minimal markdown line/span model and parser used to render cached Wikipedia
 * article markdown with bold/italic/heading style preserved on e-ink.
 *
 * Kept free of renderer and storage dependencies so it can live next to the
 * rest of the WikipediaActivity conversion pipeline.
 */
class MarkdownReader {
 public:
  struct TextLine {
    struct TextSpan {
      std::string text;
      uint8_t style = 0;  // EpdFontFamily::Style (REGULAR/BOLD/ITALIC/...)
    };
    std::string text;
    std::vector<TextSpan> spans;
    uint8_t style = 0;
    uint8_t indent = 0;  // 0 = none, 1 = list item, 2 = nested list
  };

  // Parse a single raw line of markdown into a styled TextLine.
  static TextLine parseLine(const std::string& rawLine);

  // Parse inline formatting (**bold**, *italic*, `code`, [label](url)) into spans.
  static void parseInline(TextLine& line, const std::string& text,
                          EpdFontFamily::Style baseStyle = EpdFontFamily::REGULAR);
};
