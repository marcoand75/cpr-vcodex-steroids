#include "WikitextToMarkdown.h"

#include <Logging.h>

#include <cctype>
#include <cstring>
#include <string>

namespace {

constexpr size_t MAX_LINE = 4096;  // max wikitext line kept in RAM during conversion

// -----------------------------------------------------------------
// Entity decoding
// -----------------------------------------------------------------
// Returns number of UTF-8 bytes written to out, or -1 if unknown.
int decodeEntity(const std::string& code, char* out) {
  static const struct {
    const char* name;
    const char* utf8;
  } named[] = {
      {"amp", "&"},      {"lt", "<"},       {"gt", ">"},      {"quot", "\""},
      {"apos", "'"},     {"nbsp", " "},     {"agrave", "\xC3\xA0"}, {"egrave", "\xC3\xA8"},
      {"eacute", "\xC3\xA9"}, {"igrave", "\xC3\xAC"}, {"ograve", "\xC3\xB2"}, {"ugrave", "\xC3\xB9"},
      {"ccedil", "\xC3\xA7"}, {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
      {"hellip", "\xE2\x80\xA6"},
  };
  for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
    if (code == named[i].name) {
      const size_t len = strlen(named[i].utf8);
      std::memcpy(out, named[i].utf8, len);
      return static_cast<int>(len);
    }
  }

  // Numeric references &#123; or &#x7B;
  int val = -1;
  if (code.size() > 1 && code[0] == '#') {
    const size_t base = (code.size() > 2 && (code[1] == 'x' || code[1] == 'X')) ? 2 : 1;
    const bool hex = base == 2;
    val = 0;
    bool ok = code.size() > base;
    for (size_t k = base; k < code.size() && ok; k++) {
      char d = code[k];
      if (hex) {
        if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
        else if ((d | 0x20) >= 'a' && (d | 0x20) <= 'f') val = val * 16 + ((d | 0x20) - 'a' + 10);
        else ok = false;
      } else if (d >= '0' && d <= '9') {
        val = val * 10 + (d - '0');
      } else {
        ok = false;
      }
    }
    if (!ok || val < 0) val = -1;
    else if (val == 0x27) { out[0] = '\''; return 1; }  // &#39;
  }

  if (val < 0) return -1;
  if (val < 0x80) {
    out[0] = (char)val;
    return 1;
  }
  if (val < 0x800) {
    out[0] = (char)(0xC0 | (val >> 6));
    out[1] = (char)(0x80 | (val & 0x3F));
    return 2;
  }
  out[0] = (char)(0xE0 | (val >> 12));
  out[1] = (char)(0x80 | ((val >> 6) & 0x3F));
  out[2] = (char)(0x80 | (val & 0x3F));
  return 3;
}

bool beginsCI(const std::string& s, const char* prefix) {
  const size_t plen = strlen(prefix);
  if (s.size() < plen) return false;
  for (size_t i = 0; i < plen; i++) {
    if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
  }
  return true;
}

// -----------------------------------------------------------------
// Inline wikitext -> markdown (bold/italic/links preserved)
// -----------------------------------------------------------------
void convertInlineWikitext(const std::string& src, std::string& out) {
  const size_t n = src.size();
  std::string buff;
  bool bold = false;
  bool italic = false;

  auto flush = [&]() {
    if (!buff.empty()) {
      out += buff;
      buff.clear();
    }
  };

  size_t i = 0;
  while (i < n) {
    const char c = src[i];

    // Wiki link [[Target|Display]] or [[Target]]
    if (c == '[' && i + 1 < n && src[i + 1] == '[') {
      const size_t close = src.find("]]", i + 2);
      if (close != std::string::npos) {
        const std::string inside = src.substr(i + 2, close - i - 2);
        size_t contentStart = 0;
        // Skip namespace prefixes for files/images/categories etc.
        if (beginsCI(inside, "File:") || beginsCI(inside, "Image:") || beginsCI(inside, "Category:") ||
            beginsCI(inside, "Categoria:") || beginsCI(inside, "Template:") || beginsCI(inside, "Aiuto:") ||
            beginsCI(inside, "Portale:") || beginsCI(inside, "Wikipedia:") || beginsCI(inside, "Progetto:") ||
            beginsCI(inside, "Speciale:") || beginsCI(inside, "MediaWiki:")) {
          contentStart = 0;  // Will skip below
        }
        if (!inside.empty() && inside[0] == ':' &&
            (beginsCI(inside, "File:") || beginsCI(inside, "Image:"))) {
          contentStart = 0;
        }
        // For File/Image links, the display caption is often after a '|'.
        const size_t pipe = inside.find('|');
        if (beginsCI(inside, "File:") || beginsCI(inside, "Image:")) {
          // File:name|thumb|caption — take the last pipe segment as caption,
          // but only if there is a pipe (otherwise emit nothing for the image).
          const size_t lastPipe = inside.rfind('|');
          if (lastPipe != std::string::npos) {
            const std::string afterLastPipe = inside.substr(lastPipe + 1);
            flush();
            buff += afterLastPipe;
          }
        } else {
          // Regular link: use text after the first pipe if present.
          const size_t sel = pipe != std::string::npos ? pipe : inside.size();
          flush();
          buff += inside.substr(contentStart, sel - contentStart);
        }
        i = close + 2;
        continue;
      }
      buff += '[';
      i++;
      continue;
    }

    // External link [url text] -> text
    if (c == '[' && (i + 1 >= n || src[i + 1] != '[')) {
      const size_t close = src.find(']', i + 1);
      if (close != std::string::npos) {
        const std::string inside = src.substr(i + 1, close - i - 1);
        const size_t space = inside.find(' ');
        if (space != std::string::npos) {
          flush();
          buff += inside.substr(space + 1);
        } else if (inside.find("://") != std::string::npos) {
          // Bare URL, show just the domain-ish part as text.
          const std::string url = inside;
          if (beginsCI(url, "http://") || beginsCI(url, "https://")) {
            // keep as-is (plain text) — acceptable.
            flush();
            buff += url;
          }
        } else {
          buff += inside;
        }
        i = close + 1;
        continue;
      }
      buff += '[';
      i++;
      continue;
    }

    // Bold/italic markers (sequences of apostrophes)
    if (c == '\'') {
      size_t run = 0;
      while (i + run < n && src[i + run] == '\'') run++;
      if (run >= 3) {
        flush();
        out += bold ? "**" : "**";
        bold = !bold;
        i += run;
        continue;
      }
      if (run == 2) {
        flush();
        out += italic ? "*" : "*";
        italic = !italic;
        i += run;
        continue;
      }
      buff += '\'';
      i += run;
      continue;
    }

    // Entities
    if (c == '&') {
      const size_t semi = src.find(';', i);
      if (semi != std::string::npos && semi - i <= 12) {
        const std::string code = src.substr(i + 1, semi - i - 1);
        char dec[4];
        const int len = decodeEntity(code, dec);
        if (len > 0 && len <= 4) {
          buff.append(dec, static_cast<size_t>(len));
          i = semi + 1;
          continue;
        }
      }
      buff += '&';
      i++;
      continue;
    }

    // Residual HTML tags: strip <...> keeping inner text.
    if (c == '<') {
      const size_t gt = src.find('>', i);
      if (gt != std::string::npos) {
        i = gt + 1;
        continue;
      }
      buff += '<';
      i++;
      continue;
    }

    buff += c;
    i++;
  }
  flush();
}

// -----------------------------------------------------------------
// Sanitize a wikitext line: strip templates, refs, comments, HTML tags and
// table structure, keeping visible text.
// -----------------------------------------------------------------
std::string sanitizeLine(const std::string& l) {
  const size_t n = l.size();
  std::string out;
  out.reserve(n > 0 ? n : 16);
  size_t i = 0;

  while (i < n) {
    const char c = l[i];

    // Template {{...}}
    if (c == '{' && i + 1 < n && l[i + 1] == '{') {
      int depth = 1;
      i += 2;
      while (i + 1 < n && depth > 0) {
        if (l[i] == '{' && l[i + 1] == '{') { depth++; i += 2; }
        else if (l[i] == '}' && l[i + 1] == '}') { depth--; i += 2; }
        else i++;
      }
      continue;
    }

    // Comment <!-- ... -->
    if (c == '<' && i + 4 <= n && strncmp(l.c_str() + i, "<!--", 4) == 0) {
      const size_t end = l.find("-->", i + 4);
      i = (end == std::string::npos) ? n : end + 3;
      continue;
    }

    // <ref ...>...</ref>
    if (c == '<' && i + 4 <= n && strncmp(l.c_str() + i, "<ref", 4) == 0) {
      const size_t gt = l.find('>', i);
      if (gt != std::string::npos && gt > 0 && l[gt - 1] == '/') {
        i = gt + 1;  // self-closing
        continue;
      }
      if (gt != std::string::npos) {
        const size_t close = l.find("</ref>", gt + 1);
        i = (close == std::string::npos) ? n : close + 6;
        continue;
      }
      i = n;
      continue;
    }
    // Closing </ref> or other closing tags
    if (c == '<' && i + 1 < n && l[i + 1] == '/') {
      const size_t gt = l.find('>', i);
      i = (gt == std::string::npos) ? n : gt + 1;
      continue;
    }
    // Other HTML tags <name ...> (not ref/comment/table)
    if (c == '<') {
      const size_t gt = l.find('>', i);
      if (gt != std::string::npos) {
        const std::string tag = l.substr(i + 1, gt - i - 1);
        // If block-level, add a space separator.
        if (tag[0] != '!' && tag[0] != '/') {
          if (!out.empty() && out.back() != ' ') out += ' ';
        }
        i = gt + 1;
        continue;
      }
      out += c;
      i++;
      continue;
    }

    out += c;
    i++;
  }
  return out;
}

// -----------------------------------------------------------------
// Per-line wikitext -> markdown
// -----------------------------------------------------------------
void convertWikiLine(const std::string& raw, std::string& out) {
  out.clear();
  std::string l = raw;
  // Trim leading spaces.
  size_t s = 0;
  while (s < l.size() && l[s] == ' ') s++;
  l = l.substr(s);

  if (l.empty()) return;
  l = sanitizeLine(l);
  if (l.empty()) return;

  // Headings: =Text= -> ## Text
  {
    size_t eq = 0;
    while (eq < l.size() && l[eq] == '=') eq++;
    if (eq >= 1 && eq < l.size() && l[eq] != '=' ) {
      size_t teq = l.size();
      while (teq > eq && l[teq - 1] == '=') teq--;
      if (teq > eq) {
        const std::string content = l.substr(eq, teq - eq);
        // Trim inner spaces
        size_t b = 0, e2 = content.size();
        while (b < e2 && content[b] == ' ') b++;
        while (e2 > b && content[e2 - 1] == ' ') e2--;
        const int level = eq > 4 ? 4 : static_cast<int>(eq);
        out.append(static_cast<size_t>(level), '#');
        out += ' ';
        convertInlineWikitext(content.substr(b, e2 - b), out);
        return;
      }
    }
  }

  // Horizontal rule: ---- or ___ etc.
  if (l.size() >= 3 && l.find_first_not_of('-') == std::string::npos) {
    out = "---";
    return;
  }

  // Definition list
  if (l.size() >= 1 && l[0] == ';') {
    const size_t colon = l.find(':');
    if (colon != std::string::npos) {
      out = "**";
      convertInlineWikitext(l.substr(1, colon - 1), out);
      out += "**: ";
      convertInlineWikitext(l.substr(colon + 1), out);
      return;
    }
    out = "**";
    convertInlineWikitext(l.substr(1), out);
    out += "**";
    return;
  }

  // Indented text
  if (l.size() >= 1 && l[0] == ':') {
    convertInlineWikitext(l.substr(1), out);
    return;
  }

  // Table structure: strip leading cells markers to "cell | cell"
  if (l[0] == '|' || l[0] == '!' || l[0] == '{' || l[0] == '}') {
    std::string c2;
    c2.reserve(l.size());
    size_t p = 0;
    while (p < l.size()) {
      if (l[p] == '-' && p + 1 < l.size() && (l[p+1]=='|')) { p += 2; continue; }  // |-
      if (p + 1 < l.size() && ((l[p] == '|' && l[p+1] == '|') || (l[p] == '!' && l[p+1] == '!'))) {
        c2 += " | ";
        p += 2;
      } else if (l[p] == '|' || l[p] == '!' || l[p] == '{' || l[p] == '}') {
        p++;
      } else {
        c2 += l[p];
        p++;
      }
    }
    out = c2;
    return;
  }

  // Unordered list: * item -> "- item"; ** -> "  * "
  if (l.size() >= 1 && l[0] == '*') {
    size_t starRun = 0;
    while (starRun < l.size() && l[starRun] == '*') starRun++;
    if (starRun >= 2) {
      out = "  - ";
      convertInlineWikitext(l.substr(starRun), out);
    } else {
      out = "- ";
      convertInlineWikitext(l.substr(1), out);
    }
    return;
  }

  // Ordered list: # item -> "1. "; ## -> "  1. "
  if (l.size() >= 1 && l[0] == '#') {
    size_t hashRun = 0;
    while (hashRun < l.size() && l[hashRun] == '#') hashRun++;
    if (hashRun >= 2) {
      out = " 1. ";
      convertInlineWikitext(l.substr(hashRun), out);
    } else {
      out = "1. ";
      convertInlineWikitext(l.substr(1), out);
    }
    return;
  }

  // Plain paragraph / link-with-visible-text etc.
  convertInlineWikitext(l, out);
}

}  // namespace

// ======================================================================
//  WikitextToMarkdown::convert
// ======================================================================
bool WikitextToMarkdown::convert(HalFile& in, const char* outPath) {
  HalFile out;
  if (!Storage.openFileForWrite("WIKI2MD", outPath, out)) {
    LOG_ERR("WIKI2MD", "Failed to open output file %s", outPath);
    return false;
  }
  outFile_ = &out;
  outBufLen_ = 0;

  // State machine to locate and extract the "wikitext": {"*": "<value>"}.
  // Sequence:  }"wikitext" : { "*" : "  <value>  "
  enum ScanState {
    SEE_WIKITEXT_KEY,  // matching the literal "wikitext"
    AFTER_KEY,         // skip to '{'
    SEE_STAR_QUOTE,    // skip to the '"' preceding '*'
    SEE_STAR,          // match '*'
    AFTER_STAR,        // skip to ':' then '"'
    SEE_VALUE_OPEN,    // skip whitespace to opening '"'
    READ_VALUE,        // reading the wikitext string (with JSON unescaping)
    GOT_VALUE          // closing quote reached
  } state = SEE_WIKITEXT_KEY;
  const char want[] = "\"wikitext\"";
  size_t wantPos = 0;
  bool found = false;

  std::string valBuf;
  valBuf.reserve(4096);

  // Track template {{...}} that spans multiple lines (e.g. infoboxes). Lines
  // that are inside such a span are dropped entirely to avoid leaking the raw
  // template parameters into the readable article.
  bool inTemplate = false;

  auto writeLine = [&](std::string& l) {
    // Trim.
    size_t b = 0;
    while (b < l.size() && l[b] == ' ') b++;
    const std::string trimmed = l.substr(b);

    // Manage multi-line template span.
    const bool hasOpen = trimmed.find("{{") != std::string::npos;
    const bool hasClose = trimmed.rfind("}}") != std::string::npos;
    if (!inTemplate && hasOpen && !hasClose) {
      // Opens a template that continues on following lines: drop this line.
      inTemplate = true;
      l.clear();
      return;
    }
    if (inTemplate) {
      if (hasClose) {
        // Template ends on this line.
        inTemplate = false;
      }
      l.clear();  // always drop template interior content
      return;
    }
    if (hasOpen && hasClose && trimmed.find("{{") < trimmed.rfind("}}")) {
      // Fully inline template: stripped by convertWikiLine -> sanitizeLine.
      // Keep the line so surrounding text around the template is preserved.
    }

    std::string md;
    convertWikiLine(l, md);
    if (!md.empty()) {
      putStr(md.c_str());
    }
    putChar('\n');
    l.clear();
  };

  int b;
  while (state != GOT_VALUE && (b = in.read()) >= 0) {
    const char c = static_cast<char>(b);

    switch (state) {
      case SEE_WIKITEXT_KEY:
        if (want[wantPos] == '\0') {
          state = AFTER_KEY;
        } else if (c == want[wantPos]) {
          wantPos++;
        } else {
          wantPos = (c == '"') ? 1 : 0;
        }
        continue;

      case AFTER_KEY:
        // Skip to the '{' that opens the {"*": ...} object.
        if (c == '{') state = SEE_STAR_QUOTE;
        continue;

      case SEE_STAR_QUOTE:
        // Skip to the '"' that starts the "*" member name.
        if (c == '"') state = SEE_STAR;
        continue;

      case SEE_STAR:
        // Match the '*' character.
        if (c == '*') {
          state = AFTER_STAR;
        } else {
          state = SEE_STAR_QUOTE;  // not '*'; keep looking for the quote
        }
        continue;

      case AFTER_STAR:
        // Skip to the ':' then the opening '"' of the value.
        if (c == ':') state = SEE_VALUE_OPEN;
        continue;

      case SEE_VALUE_OPEN:
        if (c == '"') state = READ_VALUE;
        continue;

      case READ_VALUE:
        if (c == '\\') {
          const int e = in.read();
          if (e < 0) break;
          const char ec = static_cast<char>(e);
          if (ec == 'n') {
            writeLine(valBuf);
          } else if (ec == 't') {
            valBuf += ' ';
          } else if (ec == 'r') {
            // ignore
          } else if (ec == '"' || ec == '\\' || ec == '/') {
            valBuf += ec;
          } else if (ec == 'u') {
            unsigned int v = 0;
            for (int k = 0; k < 4; k++) {
              const int hb = in.read();
              if (hb < 0) break;
              const char h = static_cast<char>(hb);
              if (h <= '9') v = v * 16 + (h - '0');
              else if ((h | 0x20) >= 'a' && (h | 0x20) <= 'f') v = v * 16 + ((h | 0x20) - 'a' + 10);
              else break;
            }
            if (v < 0x80) {
              valBuf += (char)v;
            } else if (v < 0x800) {
              valBuf += (char)(0xC0 | (v >> 6));
              valBuf += (char)(0x80 | (v & 0x3F));
            } else if (v < 0xD800 || v > 0xDFFF) {
              valBuf += (char)(0xE0 | (v >> 12));
              valBuf += (char)(0x80 | ((v >> 6) & 0x3F));
              valBuf += (char)(0x80 | (v & 0x3F));
            } else {
              valBuf += "\xEF\xBF\xBD";  // U+FFFD replacement (surrogates)
            }
          } else {
            valBuf += ec;
          }
          continue;
        }

        if (c == '"') {
          found = true;
          state = GOT_VALUE;
          break;
        }

        valBuf += c;
        if (valBuf.size() > MAX_LINE && valBuf.find('\n') == std::string::npos) {
          writeLine(valBuf);  // bound memory for pathologically long single lines
        }
        continue;

      case GOT_VALUE:
        break;
    }
    break;
  }

  // Flush any remaining buffered line.
  if (found && !valBuf.empty()) {
    writeLine(valBuf);
  }

  flush();
  out.close();
  return found;
}

// ======================================================================
//  Buffered output
// ======================================================================
void WikitextToMarkdown::putChar(char c) {
  if (outBufLen_ >= OUT_BUF_SIZE) flush();
  outBuf_[outBufLen_++] = c;
}

void WikitextToMarkdown::putStr(const char* s) {
  if (outBufLen_ + static_cast<int>(strlen(s)) >= OUT_BUF_SIZE) flush();
  for (const char* p = s; *p; p++) putChar(*p);
}

void WikitextToMarkdown::flush() {
  if (outFile_ && outBufLen_ > 0) {
    outFile_->write((uint8_t*)outBuf_, outBufLen_);
    outBufLen_ = 0;
  }
}
