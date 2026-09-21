#include "HtmlToTxt.h"

#include <HalStorage.h>
#include <cstdio>
#include <cstring>
#include <cctype>

void HtmlToTxt::flushOut() {
  if (outFile && outBufLen > 0) {
    outFile->write(outBuf, outBufLen);
    outBufLen = 0;
  }
}

void HtmlToTxt::writeOut(char c) {
  if (outBufLen >= OUT_BUF_SIZE) flushOut();
  outBuf[outBufLen++] = c;
}

void HtmlToTxt::writeStrOut(const char* s) {
  for (const char* p = s; *p; p++) writeOut(*p);
}

void HtmlToTxt::reset() {
  inTag = closingTag = invalidTag = inPre = false;
  skipStack = 0;
  tagLen = 0;
  lastWasSpace = lastWasNewline = true;
  pendingBlankLines = 0;
  listDepth = 0;
  blockquoteDepth = 0;
  inTableRow = false;
  firstCellInRow = true;
  outBufLen = 0;
  outFile = nullptr;
}

bool HtmlToTxt::isSkipping() { return skipStack > 0; }

void HtmlToTxt::newline(int maxBlank) {
  if (lastWasNewline) {
    if (pendingBlankLines < maxBlank) { writeOut('\n'); pendingBlankLines++; }
    return;
  }
  writeOut('\n');
  lastWasNewline = true;
  lastWasSpace = true;
  pendingBlankLines = 0;
}

void HtmlToTxt::writeIndent() {
  for (int i = 0; i < blockquoteDepth; i++) writeOut('>');
  for (int i = 0; i < listDepth; i++) { writeOut(' '); writeOut(' '); }
}

bool HtmlToTxt::isSkipTag(const char* tag) {
  const char* skipTags[] = {"script","style","head","nav","footer","noscript","svg","aside","meta","link"};
  for (auto st : skipTags) { if (strcmp(tag, st) == 0) return true; }
  return false;
}

void HtmlToTxt::handleTag() {
  const char* tag = tagBuf;
  bool closing = closingTag;
  if (tagLen == 0) return; // empty tag, skip

  // Skip tags
  if (!closing && isSkipTag(tag)) { skipStack++; return; }
  if (closing && isSkipTag(tag)) { if (skipStack > 0) skipStack--; return; }
  if (isSkipping()) return;

  // <pre>, <code>
  if (strcmp(tag, "pre") == 0) { inPre = !closing; return; }
  if (strcmp(tag, "code") == 0) { return; }

  // Headings: markdown-style # markers
  if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
    if (!closing) {
      newline(2); writeIndent();
      int level = tag[1] - '0';
      for (int i = 0; i < level; i++) writeOut('#');
      writeOut(' ');
      lastWasSpace = true; lastWasNewline = false;
    } else {
      newline(2);
    }
    return;
  }

  // Block elements
  if (strcmp(tag, "p") == 0 || strcmp(tag, "div") == 0) {
    newline(2);
    if (!closing) writeIndent();
    return;
  }

  if (strcmp(tag, "br") == 0) { newline(); return; }
  if (strcmp(tag, "hr") == 0) {
    newline(); writeIndent();
    writeStrOut("----------------------------------------");
    newline(); return;
  }

  // Inline formatting
  if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) { writeStrOut("**"); return; }
  if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0) { writeStrOut("_"); return; }
  if (strcmp(tag, "a") == 0) { return; } // links: output text only

  // Lists
  if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
    if (!closing && listDepth < MAX_LIST_DEPTH) {
      listStack[listDepth].ordered = (strcmp(tag, "ol") == 0);
      listStack[listDepth].counter = 0;
      listDepth++;
    } else if (closing && listDepth > 0) { listDepth--; }
    newline(); return;
  }
  if (strcmp(tag, "li") == 0) {
    if (!closing) {
      newline(); writeIndent();
      if (listDepth > 0) {
        auto& ls = listStack[listDepth - 1];
        if (ls.ordered) {
          ls.counter++;
          char buf[12]; snprintf(buf, sizeof(buf), "%d. ", ls.counter);
          writeStrOut(buf);
        } else { writeStrOut("- "); }
      }
      lastWasSpace = true; lastWasNewline = false;
    } else { newline(); }
    return;
  }

  // Blockquote
  if (strcmp(tag, "blockquote") == 0) {
    if (!closing && blockquoteDepth < MAX_BLOCKQUOTE) blockquoteDepth++;
    else if (closing && blockquoteDepth > 0) blockquoteDepth--;
    newline(); writeIndent(); return;
  }

  // Tables
  if (strcmp(tag, "tr") == 0) {
    if (!closing) { inTableRow = true; firstCellInRow = true; newline(); writeIndent(); }
    else { inTableRow = false; newline(); }
    return;
  }
  if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
    if (!closing && !firstCellInRow) writeOut('\t');
    if (!closing) firstCellInRow = false;
    return;
  }

  // All other tags silently ignored
}

bool HtmlToTxt::convert(HalFile& in, const char* outPath) {
  HalFile out;
  if (!Storage.openFileForWrite("HTML2TXT", outPath, out)) return false;
  reset();
  outFile = &out;

  int b;
  while ((b = in.read()) >= 0) {
    char c = static_cast<char>(b);

    if (inTag) {
      if (c == '>') {
        tagBuf[tagLen] = '\0';
        inTag = false;
        if (!invalidTag) handleTag();
        tagLen = 0;
        closingTag = false;
        invalidTag = false;
      } else {
        // Only capture the tag name (first word). Ignore attributes and spaces.
        if (tagLen < static_cast<int>(sizeof(tagBuf) - 1)) {
          if (isalpha(static_cast<unsigned char>(c))) {
            tagBuf[tagLen++] = tolower(static_cast<unsigned char>(c));
          } else if (tagLen == 0 && c == '/') {
            closingTag = true;
          } else if (tagLen == 0 && !isalnum(c)) {
            // Not a valid tag name char (like ! or ?)
            invalidTag = true;
          }
        }
      }
      continue;
    }

    if (c == '<') {
      int c1 = in.read();
      if (c1 == '!') {
        int c2 = in.read();
        if (c2 == '-') {
          int c3 = in.read();
          if (c3 == '-') {
            // It's a comment, skip until -->
            int p1 = 0, p2 = 0;
            while ((b = in.read()) >= 0) {
              if (p1 == '-' && p2 == '-' && b == '>') break;
              p1 = p2; p2 = b;
            }
            continue;
          } else {
            // <!something>
            while ((b = in.read()) >= 0) { if (b == '>') break; }
            continue;
          }
        } else {
          // <!>
          while ((b = in.read()) >= 0) { if (b == '>') break; }
          continue;
        }
      } else if (c1 == '/') {
        inTag = true; tagLen = 0; closingTag = true; invalidTag = false;
        continue;
      } else if (c1 >= 0 && isalpha(c1)) {
        inTag = true; tagLen = 0; closingTag = false; invalidTag = false;
        tagBuf[tagLen++] = tolower(static_cast<unsigned char>(c1));
        continue;
      } else {
        // Not a valid tag, maybe just a '<' in text
        writeOut('<');
        lastWasSpace = false; lastWasNewline = false;
        if (c1 >= 0) c = static_cast<char>(c1);
        else break;
      }
    }

    if (isSkipping()) continue;

    // HTML entity
    if (c == '&') {
      char entBuf[16];
      int entLen = 0;
      entBuf[entLen++] = '&';
      bool broken = false;
      while (entLen < 15 && (b = in.read()) >= 0) {
        char ec = static_cast<char>(b);
        if (ec == ';') {
          entBuf[entLen++] = ec;
          break;
        }
        if (!isalnum(ec) && ec != '#') {
          broken = true;
          break;
        }
        entBuf[entLen++] = ec;
      }
      entBuf[entLen] = '\0';
      
      if (broken) {
        // Just a standalone '&'
        writeOut('&');
        lastWasSpace = false; lastWasNewline = false;
        c = static_cast<char>(b); // process the broken char
      } else {
        if (strcmp(entBuf, "&amp;") == 0) c = '&';
        else if (strcmp(entBuf, "&lt;") == 0) c = '<';
        else if (strcmp(entBuf, "&gt;") == 0) c = '>';
        else if (strcmp(entBuf, "&quot;") == 0) c = '"';
        else if (strcmp(entBuf, "&#39;") == 0 || strcmp(entBuf, "&apos;") == 0) c = '\'';
        else if (strcmp(entBuf, "&nbsp;") == 0) c = ' ';
        else {
          bool matched = false;
          if (strcmp(entBuf, "&agrave;") == 0) { writeOut((char)0xC3); writeOut((char)0xA0); matched = true; }
          else if (strcmp(entBuf, "&egrave;") == 0) { writeOut((char)0xC3); writeOut((char)0xA8); matched = true; }
          else if (strcmp(entBuf, "&eacute;") == 0) { writeOut((char)0xC3); writeOut((char)0xA9); matched = true; }
          else if (strcmp(entBuf, "&igrave;") == 0) { writeOut((char)0xC3); writeOut((char)0xAC); matched = true; }
          else if (strcmp(entBuf, "&ograve;") == 0) { writeOut((char)0xC3); writeOut((char)0xB2); matched = true; }
          else if (strcmp(entBuf, "&ugrave;") == 0) { writeOut((char)0xC3); writeOut((char)0xB9); matched = true; }
          else if (strcmp(entBuf, "&ccedil;") == 0) { writeOut((char)0xC3); writeOut((char)0xA7); matched = true; }
          
          if (matched) {
            lastWasSpace = false; lastWasNewline = false;
            continue;
          } else {
            // Unrecognized entity, output as is
            writeStrOut(entBuf);
            lastWasSpace = false; lastWasNewline = false;
            continue;
          }
        }
      }
    }

    // <pre>/<code> — preserve whitespace
    if (inPre) { writeOut(c); continue; }

    if (c == '\r') continue;
    if (c == '\n' || c == '\t') c = ' ';

    if (c == ' ') {
      if (!lastWasSpace && !lastWasNewline) { writeOut(' '); lastWasSpace = true; }
      continue;
    }

    writeOut(c);
    lastWasSpace = false;
    lastWasNewline = false;
  }

  flushOut();
  out.close();
  return true;
} 