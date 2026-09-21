#include "MarkdownReader.h"

#include <cctype>
#include <cstring>

namespace {

bool startsWithAt(const std::string& text, const size_t pos, const char* marker) {
  const size_t markerLen = strlen(marker);
  return pos + markerLen <= text.length() && text.compare(pos, markerLen, marker) == 0;
}

std::string trimWhitespace(const std::string& text) {
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

EpdFontFamily::Style combineStyle(const EpdFontFamily::Style baseStyle, const bool bold, const bool italic) {
  uint8_t style = static_cast<uint8_t>(baseStyle);
  if (bold) style |= EpdFontFamily::BOLD;
  if (italic) style |= EpdFontFamily::ITALIC;
  return static_cast<EpdFontFamily::Style>(style & EpdFontFamily::BOLD_ITALIC);
}

}  // namespace

void MarkdownReader::parseInline(TextLine& line, const std::string& text,
                                 const EpdFontFamily::Style baseStyle) {
  std::string buffer;
  bool bold = false;
  bool italic = false;
  bool code = false;

  const auto flush = [&]() {
    if (buffer.empty()) return;
    line.text += buffer;
    const uint8_t rawStyle = static_cast<uint8_t>(code ? baseStyle : combineStyle(baseStyle, bold, italic));
    if (!line.spans.empty() && line.spans.back().style == rawStyle) {
      line.spans.back().text += buffer;
    } else {
      line.spans.push_back({buffer, rawStyle});
    }
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
          parseInline(line, text.substr(i + 1, labelEnd - i - 1), combineStyle(baseStyle, bold, italic));
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

MarkdownReader::TextLine MarkdownReader::parseLine(const std::string& rawLine) {
  TextLine line;
  std::string text = rawLine;

  size_t pos = 0;
  while (pos < text.length() && pos < 3 && text[pos] == ' ') {
    pos++;
  }
  text = text.substr(pos);

  if (text.empty()) {
    return line;
  }

  // Headings: ### Text -> BOLD, centered.
  int headerLevel = 0;
  while (headerLevel < 6 && headerLevel < static_cast<int>(text.length()) && text[headerLevel] == '#') {
    headerLevel++;
  }
  if (headerLevel > 0 && headerLevel < static_cast<int>(text.length()) &&
      std::isspace(static_cast<unsigned char>(text[headerLevel]))) {
    line.style = EpdFontFamily::BOLD;
    parseInline(line, trimWhitespace(text.substr(headerLevel + 1)), EpdFontFamily::BOLD);
    return line;
  }

  // Block quote: > text  -> ITALIC, indented.
  if (startsWithAt(text, 0, ">")) {
    size_t quotePos = 1;
    if (quotePos < text.length() && text[quotePos] == ' ') {
      quotePos++;
    }
    line.style = EpdFontFamily::ITALIC;
    line.indent = 1;
    parseInline(line, trimWhitespace(text.substr(quotePos)), EpdFontFamily::ITALIC);
    return line;
  }

  // Unordered list: - item / * item / + item
  if (startsWithAt(text, 0, "- ") || startsWithAt(text, 0, "* ") || startsWithAt(text, 0, "+ ")) {
    line.indent = 1;
    if (text[0] == '-') {
      line.text = "- ";
      line.spans.push_back({"- ", EpdFontFamily::REGULAR});
    } else {
      line.text = "\xE2\x80\xA2 ";  // •
      line.spans.push_back({"\xE2\x80\xA2 ", EpdFontFamily::REGULAR});
    }
    parseInline(line, trimWhitespace(text.substr(2)), EpdFontFamily::REGULAR);
    return line;
  }

  // Nested list: ** item or * item (two leading markers)
  if (startsWithAt(text, 0, "** ")) {
    line.indent = 2;
    line.text = "\xE2\x80\xA2 ";
    line.spans.push_back({"\xE2\x80\xA2 ", EpdFontFamily::REGULAR});
    parseInline(line, trimWhitespace(text.substr(3)), EpdFontFamily::REGULAR);
    return line;
  }
  if (startsWithAt(text, 0, "**") || (startsWithAt(text, 0, "*") && text[1] != ' ')) {
    line.indent = 2;
    parseInline(line, trimWhitespace(text.substr(2)), EpdFontFamily::REGULAR);
    return line;
  }

  // Ordered list: 1. item
  size_t numberPos = 0;
  while (numberPos < text.length() && std::isdigit(static_cast<unsigned char>(text[numberPos]))) {
    numberPos++;
  }
  if (numberPos > 0 && numberPos + 1 < text.length() && text[numberPos] == '.' && text[numberPos + 1] == ' ') {
    line.indent = 1;
    const std::string prefix = text.substr(0, numberPos + 2);
    line.text += prefix;
    line.spans.push_back({prefix, EpdFontFamily::REGULAR});
    parseInline(line, trimWhitespace(text.substr(numberPos + 2)), EpdFontFamily::REGULAR);
    return line;
  }

  // Code fence markers: emit as-is (empty so page layout skips delimiter lines).
  if (startsWithAt(text, 0, "```") || startsWithAt(text, 0, "~~~")) {
    line.text.clear();
    return line;
  }

  // Horizontal rule
  if (text.size() >= 3 && text.find_first_not_of('-') == std::string::npos) {
    line.text = "---";
    line.spans.push_back({"---", EpdFontFamily::REGULAR});
    return line;
  }

  // Plain paragraph
  parseInline(line, text, EpdFontFamily::REGULAR);
  return line;
}
