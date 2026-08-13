#include "LanguageRegistry.h"

#include <algorithm>

#include "HyphenationCommon.h"
#include "generated/hyph-en.trie.h"
#include "generated/hyph-it.trie.h"
#include "generated/hyph-pt.trie.h"

#if CPR_ENABLE_GERMAN_HYPHENATION
#include "generated/hyph-de.trie.h"
#endif
#if CPR_ENABLE_SPANISH_HYPHENATION
#include "generated/hyph-es.trie.h"
#endif
#if CPR_ENABLE_FRENCH_HYPHENATION
#include "generated/hyph-fr.trie.h"
#endif
#if CPR_ENABLE_POLISH_HYPHENATION
#include "generated/hyph-pl.trie.h"
#endif
#if CPR_ENABLE_RUSSIAN_HYPHENATION
#include "generated/hyph-ru.trie.h"
#endif
#if CPR_ENABLE_SWEDISH_HYPHENATION
#include "generated/hyph-sv.trie.h"
#endif
#if CPR_ENABLE_UKRAINIAN_HYPHENATION
#include "generated/hyph-uk.trie.h"
#endif

namespace {

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
#if CPR_ENABLE_FRENCH_HYPHENATION
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#endif
#if CPR_ENABLE_GERMAN_HYPHENATION
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
#if CPR_ENABLE_RUSSIAN_HYPHENATION
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if CPR_ENABLE_SPANISH_HYPHENATION
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
#endif
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
#if CPR_ENABLE_POLISH_HYPHENATION
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
#endif
LanguageHyphenator portugueseHyphenator(pt_patterns, isLatinLetter, toLowerLatin);
#if CPR_ENABLE_SWEDISH_HYPHENATION
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
#endif
#if CPR_ENABLE_UKRAINIAN_HYPHENATION
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
#endif

static const LanguageEntry kEntries[] = {
    {"english", "en", &englishHyphenator},
#if CPR_ENABLE_FRENCH_HYPHENATION
    {"french", "fr", &frenchHyphenator},
#endif
#if CPR_ENABLE_GERMAN_HYPHENATION
    {"german", "de", &germanHyphenator},
#endif
#if CPR_ENABLE_RUSSIAN_HYPHENATION
    {"russian", "ru", &russianHyphenator},
#endif
#if CPR_ENABLE_SPANISH_HYPHENATION
    {"spanish", "es", &spanishHyphenator},
#endif
    {"italian", "it", &italianHyphenator},
#if CPR_ENABLE_POLISH_HYPHENATION
    {"polish", "pl", &polishHyphenator},
#endif
    {"portuguese", "pt", &portugueseHyphenator},
#if CPR_ENABLE_SWEDISH_HYPHENATION
    {"swedish", "sv", &swedishHyphenator},
#endif
#if CPR_ENABLE_UKRAINIAN_HYPHENATION
    {"ukrainian", "uk", &ukrainianHyphenator},
#endif
};

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto it = std::find_if(std::begin(kEntries), std::end(kEntries),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != std::end(kEntries)) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  return LanguageEntryView{kEntries, sizeof(kEntries) / sizeof(kEntries[0])};
}
