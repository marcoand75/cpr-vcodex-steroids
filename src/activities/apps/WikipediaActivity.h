#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <EpdFontFamily.h>
#include <HalStorage.h>

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include "util/MarkdownReader.h"

class WikipediaActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  enum class State {
    SEARCH_INPUT,       
    SEARCH_HISTORY,
    CACHED_PAGES,
    SEARCH_RESULTS,
    LOADING_ARTICLE,
    ARTICLE_DISPLAY,
    LOADING_FULL_ARTICLE,
    ERROR,
  };

  State state = State::SEARCH_INPUT;
  int selectedIndex = 0;

  std::vector<std::string> searchResults;
  std::string currentQuery;
  std::string errorMessage;
  std::string searchInput;

  // Buffer fisso di 16KB per la lettura sicura
  static constexpr size_t TEXT_BUF_SIZE = 16384;
  std::unique_ptr<char[]> textBuffer;
  size_t textLength = 0; // Lunghezza totale del testo (se in RAM) o dimensione file (se su SD)
  
  bool fromCache = false;

  std::vector<std::string> historyQueries;
  static constexpr const char* HISTORY_FILE = "/.crosspoint/wikipedia-history.txt";
  static constexpr int MAX_HISTORY = 50;

  std::vector<std::string> cachedPageTitles;
  static constexpr const char* CACHE_DIR = "/.crosspoint/wikipedia-cache";
  static constexpr const char* ARTICLE_FILE = "article.md";
  static constexpr const char* RAW_FILE = "raw.json";
  static constexpr const char* INDEX_FILE = "index.bin";
  static constexpr const char* PROGRESS_FILE = "progress.bin";
  static constexpr const char* TITLE_FILE = "title.txt";

  int readingFontId = 0;
  int readingLineHeight = 20;
  int readingMarginH = 16;
  int readingMarginV = 8;

  bool preventAutoSleep() override { return true; }

  void cacheReadingSettings();
  void wifiOff();

  char* ensureBuffer();
  void freeBuffer();

  HalFile openFile;
  bool isFileOpen = false;
  void openArticleFile();
  void closeArticleFile();

  int pagesUntilFullRefresh = 0;

  // Shared header for every Wikipedia screen: Wikipedia logo at top-left, then
  // the page title, always at the same position (matches the Home rendering).
  void renderWikipediaHeader(const char* title);

  void renderSearchInput();
  void renderSearchHistory();
  void renderCachedPages();
  void renderResults();
  void renderArticle();
  void renderError();

  void performSearch(const std::string& query);
  void fetchArticleSummary();
  void fetchFullArticle();
  void openArticleForReading(const std::string& title);
  void launchSearchKeyboard();
  void onWifiSelectionComplete(bool connected);

  void loadHistory();
  void saveToHistory(const std::string& query);
  void loadCachedPages();
  static std::string legacyCachePathForTitle(const std::string& title);
  std::string cachePathForTitle(const std::string& title);
  bool cacheArticle(const std::string& title);
  bool loadCachedArticle(const std::string& title);

  void goBackToResults();
  void showError(const std::string& message);

 public:
  explicit WikipediaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wikipedia", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};