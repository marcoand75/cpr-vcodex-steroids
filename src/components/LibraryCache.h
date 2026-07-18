#pragma once

#include <string>
#include <vector>

class GfxRenderer;
struct Rect; // Forward declare from BaseTheme.h

namespace LibraryCache {

struct Entry {
  std::string path;
  std::string title;
  std::string author;
};

std::string thumbPathFor(const std::string& path, int coverW, int coverH);
bool generateCoverForBook(const std::string& path, int coverW, int coverH);
bool exists();
bool load(std::vector<Entry>& out);
bool save(const std::vector<Entry>& entries);
void invalidate();
bool removeBook(const std::string& path);
bool sync(std::vector<Entry>& out, const char* rootDir = "/", int maxBooks = 1000);
bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out, const char* rootDir = "/", int maxBooks = 1000);

} // namespace LibraryCache