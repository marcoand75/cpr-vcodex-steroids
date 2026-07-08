#include "FavoritesStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "util/BookIdentity.h"
#include "util/BookStoreUtils.h"

namespace {
constexpr char FAVORITES_FILE_JSON[] = "/.crosspoint/favorites.json";
}  // namespace

FavoritesStore FavoritesStore::instance;

int FavoritesStore::findBookIndex(const std::string& path, const std::string& bookId) const {
  return BookStoreUtils::findBookIndex(favoriteBooks, path, bookId);
}

void FavoritesStore::normalizeBook(FavoriteBook& book) { BookStoreUtils::normalizeBook(book); }

void FavoritesStore::normalizeBooks() { BookStoreUtils::normalizeBooks(favoriteBooks); }

bool FavoritesStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                             const std::string& coverBmpPath, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) {
    return false;
  }

  const std::string resolvedBookId =
      !bookId.empty() ? bookId : BookIdentity::resolveStableBookId(normalizedPath);
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    auto& existing = favoriteBooks[existingIndex];
    existing.bookId = resolvedBookId;
    existing.path = normalizedPath;
    if (!title.empty()) {
      existing.title = title;
    }
    if (!author.empty()) {
      existing.author = author;
    }
    if (!coverBmpPath.empty()) {
      existing.coverBmpPath = coverBmpPath;
    }
    saveToFile();
    return true;
  }

  favoriteBooks.push_back({resolvedBookId, normalizedPath, title, author, coverBmpPath});
  saveToFile();
  return true;
}

void FavoritesStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                const std::string& coverBmpPath, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex < 0) {
    return;
  }

  FavoriteBook& book = favoriteBooks[existingIndex];
  if (!resolvedBookId.empty()) {
    book.bookId = resolvedBookId;
  }
  if (!normalizedPath.empty()) {
    book.path = normalizedPath;
  }
  book.title = title;
  book.author = author;
  book.coverBmpPath = coverBmpPath;
  saveToFile();
}

bool FavoritesStore::updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title,
                                    const std::string& author, const std::string& coverBmpPath,
                                    const std::string& bookId) {
  const std::string normalizedNewPath = BookIdentity::normalizePath(newPath);
  if (normalizedNewPath.empty()) {
    return false;
  }

  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedNewPath.empty() ? BookIdentity::resolveStableBookId(normalizedNewPath) : "");
  const int existingIndex = findBookIndex(oldKey, resolvedBookId);
  if (existingIndex < 0) {
    return false;
  }

  FavoriteBook& book = favoriteBooks[existingIndex];
  if (!resolvedBookId.empty()) {
    book.bookId = resolvedBookId;
  }
  book.path = normalizedNewPath;
  if (!title.empty()) {
    book.title = title;
  }
  if (!author.empty()) {
    book.author = author;
  }
  if (!coverBmpPath.empty()) {
    book.coverBmpPath = coverBmpPath;
  }
  saveToFile();
  return true;
}

bool FavoritesStore::removeBook(const std::string& key) {
  const int existingIndex = findBookIndex(key, key);
  if (existingIndex < 0) {
    return false;
  }

  favoriteBooks.erase(favoriteBooks.begin() + existingIndex);
  saveToFile();
  return true;
}

bool FavoritesStore::toggleBook(const std::string& path) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) {
    return false;
  }

  const std::string resolvedBookId = BookIdentity::resolveStableBookId(normalizedPath);
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    favoriteBooks.erase(favoriteBooks.begin() + existingIndex);
    saveToFile();
    return false;
  }

  const FavoriteBook book = getDataFromBook(normalizedPath);
  addBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
  return true;
}

bool FavoritesStore::isFavorite(const std::string& key) const { return findBookIndex(key, key) >= 0; }

bool FavoritesStore::moveBook(const int fromIndex, const int toIndex) {
  if (fromIndex < 0 || toIndex < 0 || fromIndex >= static_cast<int>(favoriteBooks.size()) ||
      toIndex >= static_cast<int>(favoriteBooks.size()) || fromIndex == toIndex) {
    return false;
  }

  std::swap(favoriteBooks[fromIndex], favoriteBooks[toIndex]);
  saveToFile();
  return true;
}

bool FavoritesStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveFavorites(*this, FAVORITES_FILE_JSON);
}

FavoriteBook FavoritesStore::getDataFromBook(std::string path) const {
  const RecentBook recentBook = RECENT_BOOKS.getDataFromBook(std::move(path));
  FavoriteBook favoriteBook{recentBook.bookId, recentBook.path, recentBook.title, recentBook.author,
                            recentBook.coverBmpPath};
  if (favoriteBook.title.empty()) {
    favoriteBook.title = BookStoreUtils::fallbackTitleFromPath(favoriteBook.path);
  }
  return favoriteBook;
}

bool FavoritesStore::loadFromFile() {
  const std::string tempPath = std::string(FAVORITES_FILE_JSON) + ".tmp";
  if (!Storage.exists(FAVORITES_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), FAVORITES_FILE_JSON)) {
      LOG_DBG("FAV", "Recovered favorites.json from interrupted temp file");
    }
  }

  if (!Storage.exists(FAVORITES_FILE_JSON)) {
    return false;
  }

  const String json = Storage.readFile(FAVORITES_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  return JsonSettingsIO::loadFavorites(*this, json.c_str());
}
