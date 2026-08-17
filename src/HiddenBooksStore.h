#pragma once

#include <string>
#include <vector>

// Minimal book reference for hidden books — only bookId and normalized path.
struct HiddenBookEntry {
  std::string bookId;
  std::string path;
};

class HiddenBooksStore {
 public:
  static HiddenBooksStore& getInstance();

  bool loadFromFile();
  bool saveToFile() const;
  bool isHidden(const std::string& bookIdOrPath) const;
  bool addBook(const std::string& path);
  bool removeBook(const std::string& path);
  // Toggle: if hidden -> unhide, if not -> hide. Returns new state (true=hidden).
  bool toggleBook(const std::string& path);
  const std::vector<HiddenBookEntry>& getBooks() const { return hiddenBooks; }
  bool isLoaded() const { return loaded_; }
  bool ensureLoaded();

 private:
  HiddenBooksStore() = default;
  HiddenBooksStore(const HiddenBooksStore&) = delete;
  HiddenBooksStore& operator=(const HiddenBooksStore&) = delete;

  int findBookIndex(const std::string& key) const;
  void normalizeAndDeduplicate();

  std::vector<HiddenBookEntry> hiddenBooks;
  mutable bool loaded_ = false;
};

#define HIDDEN_BOOKS HiddenBooksStore::getInstance()
