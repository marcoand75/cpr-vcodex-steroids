#pragma once

#include <string>
#include <vector>

namespace WikipediaCacheUtils {

// Elenca le directory valide della cache wiki (nome prefisso "wiki_" con
// article.md e title.txt presenti). Ordinate in modo case-insensitive.
std::vector<std::string> listWikiDirectories();

// Etichetta leggibile per una directory wiki: legge title.txt o ricava dal
// nome del file .wiki legacy.
std::string getDirectoryLabel(const std::string& directoryPath);

}  // namespace WikipediaCacheUtils
