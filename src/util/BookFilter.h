#pragma once

#include <cctype>
#include <string>
#include <utility>

// Shared book / filter utilities. Originally inline helpers in
// LibraryActivity.cpp's anonymous namespace; extracted so the cover service
// and any future library-rework has a single source of truth.
//
// All functions are allocation-aware: normalize / compare use std::string
// returns; the filter helper takes a const reference and a filter enum.

namespace book_filter {

// Filename normalization: strip the directory and the extension from a
// path. Used to derive a display title when the book has no metadata.
//
// Example: "/books/The Hobbit - 1.epub" -> "The Hobbit - 1"
inline std::string filenameWithoutExtension(const std::string& path) {
  std::string name = path;
  const size_t lastSlash = name.find_last_of('/');
  if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);
  const size_t lastDot = name.find_last_of('.');
  if (lastDot != std::string::npos && lastDot > 0) name = name.substr(0, lastDot);
  return name;
}

// Map an UTF-8 accented character to its ASCII base letter for sort/search.
// Unknown characters fall through to std::tolower (which on a signed char
// would be UB; the cast through unsigned char is the only safe path).
inline char normalizeChar(unsigned char c) {
  switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'a';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'e';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'i';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'o';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'u';
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD1: case 0xF1: return 'n';
    case 0xC7: case 0xE7: return 'c';
    default: break;
  }
  return static_cast<char>(std::tolower(c));
}

// Apply normalizeChar to a whole string. The output has the same length as
// the input; safe to use as a sort key.
inline std::string normalizeForSort(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    out.push_back(normalizeChar(static_cast<unsigned char>(s[i])));
  }
  return out;
}

// Compare two strings using normalized characters. Returns -1, 0, or +1
// in the usual strcmp convention.
inline int compareNormalized(const std::string& a, const std::string& b) {
  const size_t na = a.size();
  const size_t nb = b.size();
  const size_t n = std::min(na, nb);
  for (size_t i = 0; i < n; ++i) {
    const char ca = normalizeChar(static_cast<unsigned char>(a[i]));
    const char cb = normalizeChar(static_cast<unsigned char>(b[i]));
    if (ca != cb) return (ca < cb) ? -1 : 1;
  }
  if (na != nb) return (na < nb) ? -1 : 1;
  return 0;
}

}  // namespace book_filter
