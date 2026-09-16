#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
struct Rect;

namespace LibraryCache {

struct Entry {
  std::string path;
  std::string title;
  std::string author;
};

// Legacy cover path helper (delegates to LibraryIndex::thumbPathFor)
std::string thumbPathFor(const std::string& path, int coverW, int coverH);

// Index persistence delegates to LibraryIndex (/.crosspoint/LIBRARY/)
bool exists();
bool load(std::vector<Entry>& out);
bool save(const std::vector<Entry>& entries);
void invalidate();
bool removeBook(const std::string& path);

// SD scanning delegates to LibraryIndex::scan / LibraryIndex::sync
bool sync(std::vector<Entry>& out, const char* rootDir = "/", int maxBooks = 10000);
bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out,
          const char* rootDir = "/", int maxBooks = 10000);

}  // namespace LibraryCache
