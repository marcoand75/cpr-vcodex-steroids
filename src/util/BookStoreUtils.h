#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "util/BookIdentity.h"
#include <HalStorage.h>
#include "ReadingStatsStore.h"

namespace BookStoreUtils {

// Unified fallback title derived from a file path.
inline std::string fallbackTitleFromPath(const std::string& path) {
  const size_t slashPos = path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? path : path.substr(slashPos + 1);
  const size_t dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}

// Find the index of a book by bookId or normalized path.
template <typename Book>
int findBookIndex(const std::vector<Book>& books, const std::string& path, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    const auto& book = books[index];
    if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
      return index;
    }
    if (!normalizedPath.empty() && book.path == normalizedPath) {
      return index;
    }
  }
  return -1;
}

// Normalize a single book entry: path, bookId, metadata fallbacks.
template <typename Book>
void normalizeBook(Book& book) {
  book.path = BookIdentity::normalizePath(book.path);
  if (!book.bookId.empty()) {
    return;
  }

  if (!book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
    return;
  }

  if (const auto* statsBook = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author)) {
    book.bookId = statsBook->bookId;
  }
}

// Deduplicate and merge book entries by bookId/path, filling empty metadata
// from newer entries. If enforceMaxSize is true, truncate to maxSize after merge.
template <typename Book>
void normalizeBooks(std::vector<Book>& books, bool enforceMaxSize = false, size_t maxSize = 0) {
  for (auto& book : books) {
    normalizeBook(book);
  }

  std::vector<Book> normalized;
  normalized.reserve(books.size());
  for (const auto& book : books) {
    const int existingIndex = [&normalized, &book]() {
      for (int index = 0; index < static_cast<int>(normalized.size()); ++index) {
        const auto& existing = normalized[index];
        if (!book.bookId.empty() && !existing.bookId.empty() && book.bookId == existing.bookId) {
          return index;
        }
        if (!book.path.empty() && existing.path == book.path) {
          return index;
        }
      }
      return -1;
    }();

    if (existingIndex < 0) {
      normalized.push_back(book);
      continue;
    }

    auto& existing = normalized[existingIndex];
    if (existing.bookId.empty()) {
      existing.bookId = book.bookId;
    }
    if (existing.path.empty() || (!book.path.empty() && Storage.exists(book.path.c_str()))) {
      existing.path = book.path;
    }
    if (existing.title.empty() && !book.title.empty()) {
      existing.title = book.title;
    }
    if (existing.author.empty() && !book.author.empty()) {
      existing.author = book.author;
    }
    if (existing.coverBmpPath.empty() && !book.coverBmpPath.empty()) {
      existing.coverBmpPath = book.coverBmpPath;
    }
  }

  books = std::move(normalized);
  if (enforceMaxSize && maxSize > 0 && books.size() > maxSize) {
    books.resize(maxSize);
  }
}

}  // namespace BookStoreUtils
