#include "WikipediaCacheUtils.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "activities/apps/WikipediaActivity.h"

namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

std::vector<std::string> WikipediaCacheUtils::listWikiDirectories() {
  std::vector<std::string> directories;
  auto d = Storage.open("/.crosspoint/wikipedia-cache");
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return directories;
  }
  d.rewindDirectory();
  char nb[128];
  for (auto f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) {
      f.getName(nb, sizeof(nb));
      const std::string name{nb};
      if (name.size() > 5 && name.compare(0, 5, "wiki_") == 0) {
        const std::string articlePath = std::string("/.crosspoint/wikipedia-cache") + "/" + name + "/" + "article.md";
        if (Storage.exists(articlePath.c_str())) {
          directories.push_back(std::string("/.crosspoint/wikipedia-cache") + "/" + name);
        }
      }
    }
    f.close();
  }
  d.close();
  std::sort(directories.begin(), directories.end(),
            [](const std::string& left, const std::string& right) { return toLower(left) < toLower(right); });
  return directories;
}

std::string WikipediaCacheUtils::getDirectoryLabel(const std::string& directoryPath) {
  if (directoryPath.empty()) return "";
  const std::string titlePath = directoryPath + "/" + "title.txt";
  const String titleFile = Storage.readFile(titlePath.c_str());
  if (titleFile.length() > 0) {
    return std::string(titleFile.c_str(), titleFile.length());
  }
  // Fallback: legacy flat .wiki file.
  const std::string legacyName = directoryPath.substr(directoryPath.find_last_of('/') + 1);
  if (legacyName.size() >= 4 && legacyName.compare(legacyName.size() - 4, 4, ".wiki") == 0) {
    std::string title = legacyName.substr(0, legacyName.size() - 4);
    for (char& c : title) {
      if (c == '_') c = ' ';
    }
    return title;
  }
  return directoryPath;
}
