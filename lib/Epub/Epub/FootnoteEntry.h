#pragma once

#include <cstdint>
#include <cstring>

#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 96
inline constexpr uint16_t EPUB_MAX_FOOTNOTES_PER_PAGE = 16;

struct FootnoteEntry {
  char number[FOOTNOTE_NUMBER_LEN];
  char href[FOOTNOTE_HREF_LEN];
  // Matches this destination to the laid-out words that display the link.
  // Zero is reserved for older/non-interactive entries.
  uint8_t linkId = 0;

  FootnoteEntry() {
    number[0] = '\0';
    href[0] = '\0';
  }
};
