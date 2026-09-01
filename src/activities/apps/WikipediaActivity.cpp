#include "WikipediaActivity.h"

#include <WiFi.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HTTPClient.h>
#include <I18n.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <functional> // Aggiunto per std::function usata nelle lambda/callback

#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include "SilentRestart.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/ListLayout.h"
#include "activities/util/ListRenderHelper.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SilentRestart.h"
#include "network/HttpDownloader.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/WikiTxtReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/PanelDrawHelper.h"
#include "components/icons/wikipediaicon.h"
#include "util/HeaderDateUtils.h"
#include "util/MarkdownReader.h"
#include "util/StringUtils.h"
#include "util/WikitextToMarkdown.h"
#include "util/NetworkMemory.h"
#include "util/WiFiUtils.h"

namespace {

std::string g_articleFilePath;

// Map the selected UI language to the Wikipedia subdomain language code.
// See the language table in STEROIDS docs (english/.../vietnamese).
std::string wikipediaLangCode(Language lang) {
  switch (lang) {
    case Language::BE: return "be";   // Bielorusso
    case Language::CA: return "ca";   // Catalano
    case Language::CS: return "cs";   // Ceco
    case Language::DA: return "da";   // Danese
    case Language::NL: return "nl";   // Olandese
    case Language::FI: return "fi";   // Finlandese
    case Language::FR: return "fr";   // Francese
    case Language::DE: return "de";   // Tedesco
    case Language::HU: return "hu";   // Ungherese
    case Language::IT: return "it";   // Italiano
    case Language::KK: return "kk";   // Kazaco
    case Language::LT: return "lt";   // Lituano
    case Language::PL: return "pl";   // Polacco
    case Language::PT: return "pt";   // Portoghese
    case Language::RO: return "ro";   // Rumeno
    case Language::RU: return "ru";   // Russo
    case Language::SI: return "sl";   // Sloveno (Wikipedia usa "sl")
    case Language::ES: return "es";   // Spagnolo
    case Language::SV: return "sv";   // Svedese
    case Language::TR: return "tr";   // Turco
    case Language::UK: return "uk";   // Ucraino
    case Language::VI: return "vi";   // Vietnamita
    case Language::EN:
    default: return "en";             // Inglese
  }
}

// Base Wikipedia URL per the selected language.
std::string wikipediaBaseUrl() {
  return "https://" + wikipediaLangCode(I18N.getLanguage()) + ".wikipedia.org";
}


int fontSizeToPixels(uint8_t fs) {
  switch (fs) {
    case CrossPointSettings::X_SMALL: return 14;
    case CrossPointSettings::SMALL: return 16;
    case CrossPointSettings::MEDIUM: return 18;
    case CrossPointSettings::LARGE: return 20;
    case CrossPointSettings::EXTRA_LARGE: return 22;
    default: return 16;
  }
}

int lineSpacingToLineHeight(uint8_t ls) {
  switch (ls) { case 0: return 16; case 1: return 20; case 2: return 24; case 3: return 28; default: return 20; }
}

int builtinFontId(uint8_t family, int px) {
  if (px <= 10) px = 10;
  else if (px <= 12) px = 12;
  else if (px <= 14) px = 14;
  else if (px <= 16) px = 16;
  else px = 18;

  switch (family) {
    case CrossPointSettings::BOOKERLY:
      switch (px) { case 10: return BOOKERLY_10_FONT_ID; case 12: return BOOKERLY_12_FONT_ID; case 14: return BOOKERLY_14_FONT_ID; case 16: return BOOKERLY_16_FONT_ID; default: return BOOKERLY_18_FONT_ID; }
    case CrossPointSettings::NOTOSANS:
      switch (px) { case 10: return NOTOSANS_10_FONT_ID; case 12: return NOTOSANS_12_FONT_ID; case 14: return NOTOSANS_14_FONT_ID; case 16: return NOTOSANS_16_FONT_ID; default: return NOTOSANS_18_FONT_ID; }
#ifndef OMIT_LEXEND
    case CrossPointSettings::LEXEND:
      switch (px) { case 10: return LEXEND_10_FONT_ID; case 12: return LEXEND_12_FONT_ID; case 14: return LEXEND_14_FONT_ID; case 16: return LEXEND_16_FONT_ID; default: return LEXEND_18_FONT_ID; }
#endif
    default: return BOOKERLY_18_FONT_ID;
  }
}

std::string urlEncode(const std::string& s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      result += static_cast<char>(c);
    } else if (c == ' ') {
      result += "%20";
    } else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 0x0F];
    }
  }
  return result;
}

std::string urlEncodeForPath(const std::string& s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      result += static_cast<char>(c);
    } else if (c == ' ') {
      result += '_';
    } else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 0x0F];
    }
  }
  return result;
}

int parseOpensearchTitles(const char* json, std::string* out, int max) {
  int count = 0, depth = 0; bool in = false; const char* arr = nullptr;
  const char* p = json;
  while (*p) {
    if (*p == '\\' && in) { p += 2; continue; }
    if (*p == '"') { in = !in; p++; continue; }
    if (!in && *p == '[') { depth++; if (depth == 2) { arr = p + 1; break; } }
    p++;
  }
  if (!arr) return 0;
  p = arr;
  while (*p && count < max) {
    while (*p && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p == ']') break;
    if (*p == '"') {
      const char* ts = p + 1; 
      const char* te = ts;
      while (*te) { if (*te == '\\' && *(te+1)) { te += 2; continue; } if (*te == '"') break; te++; }
      if (*te != '"') break;
      size_t len = static_cast<size_t>(te - ts);
      if (len > 0) {
        char buf[256]; size_t o = 0;
        for (const char* s = ts; s < te && o < 255; s++) {
          if (*s == '\\' && s+1 < te) { 
            s++; 
            switch (*s) { 
              case 'n': buf[o++]='\n'; break; 
              case 't': buf[o++]='\t'; break; 
              case '\\': buf[o++]='\\'; break; 
              case '"': buf[o++]='"'; break; 
              default: buf[o++]=*s; break; 
            } 
          }
          else buf[o++] = *s;
        }
        buf[o] = '\0'; 
        out[count] = std::string(buf, o); 
        count++;
      }
      p = te + 1;
    } else break;
  }
  return count;
}

size_t extractExtractField(const char* json, char* buf, size_t sz) {
  const char* p = json; size_t o = 0;
  while (*p && o < sz - 1) {
    if (strncmp(p, "\"extract\":\"", 11) == 0) {
      p += 11;
      while (*p && o < sz - 1) {
        if (*p == '\\') {
          p++;
          switch (*p) {
            case 'n': buf[o++]='\n'; p++; break;
            case 't': buf[o++]='\t'; p++; break;
            case 'r': buf[o++]='\r'; p++; break;
            case '"': buf[o++]='"'; p++; break;
            case '\\': buf[o++]='\\'; p++; break;
            case '/': buf[o++]='/'; p++; break;
            case 'u': {
              p++;
              int val = 0;
              for (int i = 0; i < 4 && *p; i++) {
                char h = *p++;
                if (h <= '9') val = val * 16 + (h - '0');
                else val = val * 16 + ((h & 0xDF) - 'A' + 10);
              }
              if (val < 0x80) { if (o < sz-1) buf[o++] = static_cast<char>(val); }
              else if (val < 0x800) { if (o < sz-2) { buf[o++] = static_cast<char>(0xC0 | (val >> 6)); buf[o++] = static_cast<char>(0x80 | (val & 0x3F)); } }
              else { if (o < sz-3) { buf[o++] = static_cast<char>(0xE0 | (val >> 12)); buf[o++] = static_cast<char>(0x80 | ((val >> 6) & 0x3F)); buf[o++] = static_cast<char>(0x80 | (val & 0x3F)); } }
              break;
            }
            default: buf[o++]=*p; p++; break;
          }
          continue;
        }
        if (*p == '"') { buf[o] = '\0'; return o; }
        buf[o++] = *p; p++;
      }
      break;
    }
    p++;
  }
  buf[o] = '\0'; return o;
}

int mdLineWidth(GfxRenderer& renderer, int fontId, const MarkdownReader::TextLine& line) {
  if (line.spans.empty()) {
    return renderer.getTextAdvanceX(fontId, line.text.c_str(), static_cast<EpdFontFamily::Style>(line.style));
  }
  int width = 0;
  for (const auto& span : line.spans) {
    width += renderer.getTextAdvanceX(fontId, span.text.c_str(), static_cast<EpdFontFamily::Style>(span.style));
  }
  return width;
}

MarkdownReader::TextLine sliceMdLine(const MarkdownReader::TextLine& source, size_t begin, size_t length) {
  MarkdownReader::TextLine out;
  out.style = source.style;
  out.indent = source.indent;
  const size_t end = begin + length;
  size_t spanBegin = 0;
  for (const auto& span : source.spans) {
    const size_t spanEnd = spanBegin + span.text.length();
    if (spanEnd > begin && spanBegin < end) {
      const size_t localBegin = begin > spanBegin ? begin - spanBegin : 0;
      const size_t localEnd = std::min(span.text.length(), end - spanBegin);
      const std::string part = span.text.substr(localBegin, localEnd - localBegin);
      out.text += part;
      out.spans.push_back({part, span.style});
    }
    spanBegin = spanEnd;
  }
  if (out.spans.empty() && !source.text.empty()) {
    out.text = source.text.substr(begin, length);
    out.spans.push_back({out.text, source.style});
  }
  return out;
}

} // anonymous namespace

std::string WikipediaActivity::legacyCachePathForTitle(const std::string& title) {
  return std::string(CACHE_DIR) + "/" + StringUtils::sanitizeFilename(title, 50) + ".wiki";
}

void WikipediaActivity::cacheReadingSettings() {
  int px = fontSizeToPixels(SETTINGS.fontSize);
  readingLineHeight = std::max(px + 4, lineSpacingToLineHeight(SETTINGS.lineSpacing));
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    int id = sdFontSystem.resolveFontId(SETTINGS.sdFontFamilyName, SETTINGS.fontSize);
    if (id > 0) readingFontId = id;
  }
  if (readingFontId <= 0) readingFontId = builtinFontId(SETTINGS.fontFamily, px);
  readingMarginH = 8 + static_cast<int>(SETTINGS.screenMargin) * 3;
  readingMarginV = 4 + static_cast<int>(SETTINGS.screenMargin);
}

char* WikipediaActivity::ensureBuffer() {
  if (!textBuffer) {
    textBuffer = std::make_unique<char[]>(TEXT_BUF_SIZE);
    textBuffer[0] = '\0';
    LOG_DBG("WIKI", "Allocated text buffer (%zu bytes)", TEXT_BUF_SIZE);
  }
  return textBuffer.get();
}

void WikipediaActivity::freeBuffer() {
  textBuffer.reset();
  textLength = 0;
  g_articleFilePath.clear();
  closeArticleFile();

  LOG_DBG("WIKI", "Freed resources");
}

void WikipediaActivity::openArticleFile() {
  if (!g_articleFilePath.empty() && !isFileOpen) {
    if (Storage.openFileForRead("WIKI", g_articleFilePath.c_str(), openFile)) {
      isFileOpen = true;
    }
  }
}

void WikipediaActivity::closeArticleFile() {
  if (isFileOpen) {
    openFile.close();
    isFileOpen = false;
  }
}

void WikipediaActivity::onEnter() {
  LOG_DBG("WIKI", "onEnter");
  Activity::onEnter();
  cacheReadingSettings();
  state = State::SEARCH_INPUT;
  selectedIndex = 0;
  searchInput.clear();
  searchResults.clear();
  errorMessage.clear();
  freeBuffer(); // Assicura stato pulito
  loadHistory();
  loadCachedPages();
  requestUpdate();
}

void WikipediaActivity::onExit() {
  LOG_DBG("WIKI", "onExit");
  searchResults.clear();
  historyQueries.clear();
  cachedPageTitles.clear();
  freeBuffer();
  WiFiUtils::wifiOff();
}

void WikipediaActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (state) {
      case State::SEARCH_INPUT:
      case State::ERROR:
        // Wikipedia uses WiFi (HTTP/HTTPS) which fragments the heap.
        // Perform a seamless silent restart to clear the heap, routing
        // back to the correct destination (Apps or Home) after reboot.
        LOG_DBG("WIKI", "Back at root: requesting seamless silent restart (free=%d maxA=%d)",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (launchFromApps) {
          silentRestartToApps();
        } else {
          silentRestartToHome();
        }
        // Unreachable: ESP.restart() above resets the CPU.
        finish(); break;
      case State::SEARCH_HISTORY:
      case State::CACHED_PAGES:
        state = State::SEARCH_INPUT; requestUpdate(); break;
      case State::SEARCH_RESULTS:
        state = State::SEARCH_INPUT; searchInput.clear(); searchResults.clear(); requestUpdate(); break;
      case State::ARTICLE_DISPLAY:
        if (!searchResults.empty()) {
          state = State::SEARCH_RESULTS;
        } else {
          state = State::SEARCH_INPUT;
        }
        requestUpdate(); break;
      case State::LOADING_ARTICLE:
      case State::LOADING_FULL_ARTICLE:
        freeBuffer(); state = State::SEARCH_INPUT; requestUpdate(); break;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (state) {
      case State::SEARCH_INPUT:
        if (selectedIndex == 0) { if (!searchInput.empty()) performSearch(searchInput); else launchSearchKeyboard(); }
        else if (selectedIndex == 1) { state = State::SEARCH_HISTORY; selectedIndex = 0; loadHistory(); requestUpdate(); }
        else if (selectedIndex == 2) { state = State::CACHED_PAGES; selectedIndex = 0; loadCachedPages(); requestUpdate(); }
        break;
      case State::SEARCH_HISTORY:
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(historyQueries.size())) {
          if (mappedInput.getHeldTime() >= 1500) {
            LOG_DBG("WIKI", "Long-press: deleting history entry %d", selectedIndex);
            std::string toRemove = historyQueries[selectedIndex];
            String content;
            if (Storage.exists(HISTORY_FILE)) content = Storage.readFile(HISTORY_FILE);
            String searchStr = String(toRemove.c_str()) + "\n";
            int pos = content.indexOf(searchStr);
            if (pos >= 0) {
              content = content.substring(0, pos) + content.substring(pos + searchStr.length());
              Storage.writeFile(HISTORY_FILE, content);
            }
            loadHistory();
            selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(historyQueries.size()));
            requestUpdate();
          } else {
            currentQuery = historyQueries[selectedIndex]; searchInput = currentQuery; performSearch(currentQuery);
          }
        }
        break;
      case State::CACHED_PAGES:
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(cachedPageTitles.size())) {
          if (mappedInput.getHeldTime() >= 1500 && !cachedPageTitles.empty()) {
            LOG_DBG("WIKI", "Long-press: asking confirmation to delete cached page %d", selectedIndex);
            const std::string cachedTitle = cachedPageTitles[selectedIndex];
            const int currentSelection = selectedIndex;
            startActivityForResult(
                std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_CACHED_PAGE),
                                                       cachedTitle),
                [this, currentSelection, cachedTitle](const ActivityResult& result) {
                  if (result.isCancelled) {
                    requestUpdate();
                    return;
                  }
                  const std::string path = cachePathForTitle(cachedTitle);
                  if (!Storage.removeDir(path.c_str())) {
                    LOG_ERR("WIKI", "Failed to delete cached page: %s", path.c_str());
                  }
                  loadCachedPages();
                  selectedIndex = ButtonNavigator::clampIndex(currentSelection, static_cast<int>(cachedPageTitles.size()));
                  requestUpdate();
                });
          } else {
            const std::string& title = cachedPageTitles[selectedIndex];
            if (loadCachedArticle(title)) {
              fromCache = true;
              openArticleForReading(title);
            } else {
              currentQuery = title; searchInput = title; performSearch(title);
            }
          }
        }
        break;
      case State::SEARCH_RESULTS:
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(searchResults.size())) fetchArticleSummary();
        break;
      case State::ARTICLE_DISPLAY:
        LOG_DBG("WIKI", "Confirm on ARTICLE_DISPLAY — opening/loading full article");
        // Se l'articolo completo è già in cache lo apriamo subito senza rete;
        // altrimenti, scarichiamo e convertiamo il full article.
        if (loadCachedArticle(currentQuery)) {
          fromCache = true;
          openArticleForReading(currentQuery);
        } else {
          fetchFullArticle();
        }
        break;
      default: break;
    }
    return;
  }

  buttonNavigator.onNext([this] {
    if (state == State::SEARCH_INPUT) {
      int old = selectedIndex; selectedIndex = ButtonNavigator::nextIndex(selectedIndex, 3);
      if (old != selectedIndex) requestUpdate();
    } else if (state == State::SEARCH_HISTORY) {
      if (!historyQueries.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(historyQueries.size())); if (old != selectedIndex) requestUpdate(); }
    } else if (state == State::CACHED_PAGES) {
      if (!cachedPageTitles.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(cachedPageTitles.size())); if (old != selectedIndex) requestUpdate(); }
    } else if (state == State::SEARCH_RESULTS) {
      if (!searchResults.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(searchResults.size())); if (old != selectedIndex) requestUpdate(); }
    }
  });

  buttonNavigator.onPrevious([this] {
    if (state == State::SEARCH_INPUT) {
      int old = selectedIndex; selectedIndex = ButtonNavigator::previousIndex(selectedIndex, 3);
      if (old != selectedIndex) requestUpdate();
    } else if (state == State::SEARCH_HISTORY) {
      if (!historyQueries.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(historyQueries.size())); if (old != selectedIndex) requestUpdate(); }
    } else if (state == State::CACHED_PAGES) {
      if (!cachedPageTitles.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(cachedPageTitles.size())); if (old != selectedIndex) requestUpdate(); }
    } else if (state == State::SEARCH_RESULTS) {
      if (!searchResults.empty()) { int old = selectedIndex; selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(searchResults.size())); if (old != selectedIndex) requestUpdate(); }
    }
  });
}

void WikipediaActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case State::SEARCH_INPUT:         renderSearchInput(); break;
    case State::SEARCH_HISTORY:       renderSearchHistory(); break;
    case State::CACHED_PAGES:         renderCachedPages(); break;
    case State::SEARCH_RESULTS:       renderResults(); break;
    case State::LOADING_ARTICLE:
    case State::LOADING_FULL_ARTICLE:
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2, tr(STR_WIKIPEDIA_LOADING_ARTICLE));
      renderer.displayBuffer(); break;
    case State::ARTICLE_DISPLAY:      renderArticle(); break;
    case State::ERROR:                renderError(); break;
  }
}

// Shared header for every Wikipedia screen: Wikipedia logo (top-left, same
// rendering as Home) followed by the page title, always at the same position.
void WikipediaActivity::renderWikipediaHeader(const char* title) {
  // Top thin line (date/reminder) consistent with the rest of the UI.
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int margin = 20;
  const int logoSize = 32;

  // Same position on every page: logo at the left, title right after it.
  const int headerY = metrics.topPadding;
  const int logoX = margin;
  const int logoY = headerY + 12;

  // Draw exactly like Home uses the app icon (renderer.drawIcon), so the logo
  // is not rotated and looks identical to the Home/Apps grid.
  renderer.drawIcon(WikipediaIcon, logoX, logoY, logoSize, logoSize);

  // Title text, left-aligned and vertically centred with the logo.
  const int titleLh = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleY = logoY + (logoSize - titleLh) / 2;
  renderer.drawText(UI_12_FONT_ID, logoX + logoSize + 10, titleY, title, true, EpdFontFamily::BOLD);
}

// Returns the shared header content top (below logo+title) used by the screens.
namespace {
int wikipediaHeaderContentTop(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + 12 + 32 + 14;
}
}  // namespace

void WikipediaActivity::renderSearchInput() {
  const int pw = renderer.getScreenWidth();
  const int margin = 20;

  // --- Header: logo + "Wikipedia" title, shared across all app pages ---
  renderWikipediaHeader(tr(STR_WIKIPEDIA));

  // --- Cyberpunk action panels (pushed down 64px below the shared header, fixed) ---
  const int ct = wikipediaHeaderContentTop(renderer) + 64;
  const int panelW = pw - margin * 2;
  const int panelH = 54;
  const int gap = 18;

  auto drawPanel = [&](int idx, int x, int y, int w, int h, const char* label) {
    const bool sel = selectedIndex == idx;
    if (sel) renderer.fillRect(x, y, w, h, 1);
    PanelDrawHelper::drawCyberpunkPanel(renderer, x, y, w, h, sel);
    // Label, left-aligned with a little padding, vertically centred.
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, x + 16, y + (h - lh) / 2, label, !sel, EpdFontFamily::BOLD);
  };

  const char* searchLbl = searchInput.empty() ? tr(STR_SEARCH_HINT) : searchInput.c_str();
  drawPanel(0, margin, ct, panelW, panelH, searchLbl);
  drawPanel(1, margin, ct + panelH + gap, panelW, panelH, tr(STR_RECENT_SEARCHES));
  const int cacheCount = static_cast<int>(cachedPageTitles.size());
  char cacheLbl[64];
  snprintf(cacheLbl, sizeof(cacheLbl), "%s (%d)", tr(STR_CACHED_PAGES), cacheCount);
  drawPanel(2, margin, ct + 2 * (panelH + gap), panelW, panelH, cacheLbl);

  // Cyberpunk panel in fondo: modalità Wi-Fi (secondo Sync Day) + lingua di ricerca.
  // Bianco, bordo nero, testo nero, rialzato (parte subito sotto i pulsanti) e
  // più alto; usa lo stesso font dei pulsanti (UI_10).
  {
    const int ph = renderer.getScreenHeight();
    const int bh = UITheme::getInstance().getMetrics().buttonHintsHeight;
    const int pW = renderer.getScreenWidth() - margin * 2;
    const int pX = margin;
    const int actionsBottom = ct + 2 * (panelH + gap) + panelH;
    const int pY = actionsBottom + 12;              // rialzato: subito sotto i pulsanti
    const int hintsTop = ph - bh;
    const int pH = (hintsTop - 12) - pY;             // più alto: fino a sopra gli hint
    renderer.fillRect(pX, pY, pW, pH, 0);            // sfondo bianco
    PanelDrawHelper::drawCyberpunkPanel(renderer, pX, pY, pW, pH, false);

    const char* wifiText = SETTINGS.syncDayWifiChoice == CrossPointSettings::SYNC_DAY_WIFI_MANUAL
                               ? tr(STR_WIKIPEDIA_WIFI_MANUAL)
                               : tr(STR_WIKIPEDIA_WIFI_AUTO);
    std::string langText = std::string(tr(STR_WIKIPEDIA_SEARCH_LANG)) + " " +
                           I18N.getLanguageName(I18N.getLanguage());
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int textX = pX + 16;
    const int textW = pW - 32;
    const int panelBottom = pY + pH;

    // Draw a string inside the panel, wrapping onto several lines when it is
    // wider than the panel, and stop when the height is exhausted.
    auto drawWrappedLine = [&](const std::string& str, int& y, const bool bold) {
      for (const auto& wl : renderer.wrappedText(UI_10_FONT_ID, str.c_str(), textW, 8,
                                                 bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR)) {
        if (y + lh > panelBottom) return;
        renderer.drawText(UI_10_FONT_ID, textX, y, wl.c_str(), true,
                          bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        y += lh + 6;
      }
    };

    int lineY = pY + 16;
    drawWrappedLine(tr(STR_WIKIPEDIA_WIFI_HINT), lineY, true);
    drawWrappedLine(wifiText, lineY, false);
    drawWrappedLine(langText, lineY, false);
  }

  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void WikipediaActivity::renderSearchHistory() {
  renderWikipediaHeader(tr(STR_RECENT_SEARCHES));
  auto layout = ListLayout::compute(renderer, true, false);
  if (historyQueries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, layout.contentTop + layout.contentHeight / 2, tr(STR_WIKIPEDIA_NO_RESULTS));
  } else {
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(historyQueries.size()), selectedIndex,
                               [this](int i) { return historyQueries[i]; });
  }
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void WikipediaActivity::renderCachedPages() {
  renderWikipediaHeader(tr(STR_CACHED_PAGES));
  auto layout = ListLayout::compute(renderer, true, false);
  if (cachedPageTitles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, layout.contentTop + layout.contentHeight / 2, tr(STR_WIKIPEDIA_NO_RESULTS));
  } else {
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(cachedPageTitles.size()), selectedIndex,
                               [this](int i) { return cachedPageTitles[i]; });
  }
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void WikipediaActivity::renderResults() {
  auto layout = ListLayout::compute(renderer, true, false);
  renderWikipediaHeader(tr(STR_WIKIPEDIA));
  if (searchResults.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, layout.contentTop + layout.contentHeight / 2, tr(STR_WIKIPEDIA_NO_RESULTS));
  } else {
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(searchResults.size()), selectedIndex,
                               [this](int i) { return searchResults[i]; });
  }
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void WikipediaActivity::renderArticle() {
  char* buf = ensureBuffer();
  if (!buf || textLength == 0) {
    LOG_DBG("WIKI", "renderArticle: empty buf=%p len=%zu", (void*)buf, textLength);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2, "Loading...");
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), nullptr, nullptr, nullptr);
    renderer.displayBuffer();
    return;
  }

  LOG_DBG("WIKI", "renderArticle: rendering %zu bytes", textLength);
  HeaderDateUtils::drawHeaderWithDate(renderer, currentQuery.c_str());
  int pw = renderer.getScreenWidth(), ph = renderer.getScreenHeight();
  int hh = UITheme::getInstance().getMetrics().headerHeight;
  int bh = UITheme::getInstance().getMetrics().buttonHintsHeight;
  int ct = hh + 4, ch = ph - ct - bh - 4, tw = pw - readingMarginH * 2;
  int fId = readingFontId > 0 ? readingFontId : UI_10_FONT_ID;

  // Pannello cyberpunk in basso (stesso stile della prima pagina): bianco,
  // bordo nero, testo nero, più alto e rialzato (64px). Calcoliamo prima la
  // geometria per evitare che il testo del summary vada sotto il pannello.
  const int margin = 20;
  const int panelH = 80;                     // più alto
  const int panelX = margin;
  const int panelW = pw - margin * 2;
  const int panelY = ph - bh - panelH - 64;  // spostato 64px più in alto

  const char* text = buf;
  int y = ct;

  const int panelTop = panelY;
  int textBottomLimit = ct + ch;
  if (panelTop > ct) textBottomLimit = panelTop - readingLineHeight;  // non sovrapporre il pannello

  while (*text && y + readingLineHeight <= textBottomLimit) {
    const char* nl = text;
    while (*nl && *nl != '\n') nl++;

    std::string seg(text, static_cast<size_t>(nl - text));
    auto wrapped = renderer.wrappedText(fId, seg.c_str(), tw, 1000);

    if (wrapped.empty()) {
      y += readingLineHeight / 2;
    }

    for (const auto& line : wrapped) {
      if (y + readingLineHeight > textBottomLimit) break;
      if (!line.empty()) {
        bool blank = true; for (char c : line) { if (c != ' ' && c != '\t') { blank = false; break; } }
        if (!blank) renderer.drawText(fId, readingMarginH, y, line.c_str(), true);
      }
      y += readingLineHeight;
    }

    if (*nl == '\n') nl++;
    text = nl;
  }

  // Cyberpunk panel in fondo alla pagina: invita a premere Seleziona per
  // scaricare/convertire l'articolo completo. Stesso stile del pannello info
  // della homepage: bianco, bordo nero, testo nero, più alto e rialzato (64px).
  {
    renderer.fillRect(panelX, panelY, panelW, panelH, 0);   // sfondo bianco
    PanelDrawHelper::drawCyberpunkPanel(renderer, panelX, panelY, panelW, panelH, false);
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int textX = panelX + 16;
    const int textW = panelW - 32;
    const int panelBottom = panelY + panelH;

    // Wrap each hint line if it is wider than the panel and stop at the bottom.
    auto drawWrappedLine = [&](const std::string& str, int& y, const bool bold) {
      for (const auto& wl : renderer.wrappedText(UI_10_FONT_ID, str.c_str(), textW, 4,
                                                 bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR)) {
        if (y + lh > panelBottom) return;
        renderer.drawText(UI_10_FONT_ID, textX, y, wl.c_str(), true,
                          bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        y += lh + 6;
      }
    };

    int lineY = panelY + 16;
    drawWrappedLine(tr(STR_WIKIPEDIA_DOWNLOAD_NOTE), lineY, true);
    drawWrappedLine(tr(STR_WIKIPEDIA_DOWNLOAD_NOTE2), lineY, false);
  }

  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void WikipediaActivity::renderError() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_WIKIPEDIA));
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2 - 20, tr(STR_WIKIPEDIA_ERROR));
  if (!errorMessage.empty()) renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight()/2 + 10, errorMessage.c_str());
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), nullptr, nullptr, nullptr);
  renderer.displayBuffer();
}

void WikipediaActivity::performSearch(const std::string& query) {
  if (query.empty()) return;
  currentQuery = query; selectedIndex = 0;
  searchResults.clear();
  freeBuffer();

  NetworkMemory::prepareBeforeNetwork(renderer, "WIKI", "before_search");

  std::string encodedQuery = urlEncode(query);

  char url[512];
  snprintf(url, sizeof(url),
           "%s/w/api.php?action=opensearch&search=%s&limit=10&namespace=0&format=json",
           wikipediaBaseUrl().c_str(), encodedQuery.c_str());

  if (WiFi.status() != WL_CONNECTED) {
    NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "search_wifi_check");
    startActivityForResult(WifiSelectionActivity::createNetworkOperation(renderer, mappedInput, /*syncRtcOnConnect=*/false),
                           [this](const ActivityResult& r) { onWifiSelectionComplete(!r.isCancelled); });
    return;
  }

  std::string response;
  bool ok = HttpDownloader::fetchUrl(url, response);
  NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "after_search");

  if (!ok) { showError(tr(STR_ERROR_GENERAL_FAILURE)); return; }

  std::string titles[10];
  int found = parseOpensearchTitles(response.c_str(), titles, 10);
  searchResults.clear();
  for (int i = 0; i < found; i++) searchResults.push_back(titles[i]);

  if (searchResults.empty()) {
    state = State::ERROR; errorMessage = tr(STR_WIKIPEDIA_NO_RESULTS);
  } else {
    saveToHistory(query);
    if (searchResults.size() == 1) {
      LOG_DBG("WIKI", "Single result found, fetching summary directly");
      selectedIndex = 0;
      fetchArticleSummary();
    } else {
      state = State::SEARCH_RESULTS;
    }
  }
  requestUpdate();
}

void WikipediaActivity::openArticleForReading(const std::string& title) {
  currentQuery = title;
  cacheReadingSettings();

  const std::string wikiDir = cachePathForTitle(title);
  const std::string articlePath = wikiDir + "/" + ARTICLE_FILE;
  if (!Storage.exists(articlePath.c_str())) {
    showError(tr(STR_WIKIPEDIA_ERROR));
    return;
  }

  // Disattiva il Wi-Fi prima di aprire il reader per liberare heap.
  if (WiFi.status() == WL_CONNECTED) WiFiUtils::wifiOff();

  // Quando il reader chiude, si torna SEMPRE al menu principale di Wikipedia.
  startActivityForResult(
      std::make_unique<WikiTxtReaderActivity>(renderer, mappedInput, wikiDir, title),
      [this](const ActivityResult& /*r*/) {
        freeBuffer();
        searchResults.clear();
        // Riporta la home allo stato pulito: altrimenti il pulsante "Cerca su
        // Wikipedia" continua a mostrare il titolo dell'articolo appena letto.
        searchInput.clear();
        currentQuery.clear();
        errorMessage.clear();
        state = State::SEARCH_INPUT;
        selectedIndex = 0;
        requestUpdate();
      });
}

void WikipediaActivity::fetchArticleSummary() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(searchResults.size())) return;
  const std::string& title = searchResults[selectedIndex];

  if (loadCachedArticle(title)) {
    openArticleForReading(title);
    return;
  }

  state = State::LOADING_ARTICLE;
  requestUpdate();
  freeBuffer();

  NetworkMemory::prepareBeforeNetwork(renderer, "WIKI", "before_article");
  std::string encodedTitle = urlEncodeForPath(title);
  char url[512];
  snprintf(url, sizeof(url), "%s/api/rest_v1/page/summary/%s", wikipediaBaseUrl().c_str(), encodedTitle.c_str());

  if (WiFi.status() != WL_CONNECTED) {
    NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "summary_wifi_check");
    startActivityForResult(WifiSelectionActivity::createNetworkOperation(renderer, mappedInput, /*syncRtcOnConnect=*/false),
                           [this](const ActivityResult& r) {
                             if (!r.isCancelled && !currentQuery.empty()) fetchArticleSummary();
                             else showError(tr(STR_WIFI_CONN_FAILED));
                           });
    return;
  }

  std::string response;
  bool ok = HttpDownloader::fetchUrl(url, response);
  NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "after_article");

  if (!ok) { showError(tr(STR_WIKIPEDIA_ERROR)); return; }

  char* buf = ensureBuffer();
  textLength = extractExtractField(response.c_str(), buf, TEXT_BUF_SIZE - 1);
  buf[textLength] = '\0';
  g_articleFilePath.clear();

  if (textLength == 0) {
    LOG_DBG("WIKI", "Empty summary, fetching full article directly");
    currentQuery = title;
    fetchFullArticle();
    return;
  }

  currentQuery = title;
  state = State::ARTICLE_DISPLAY;
  requestUpdate();
}

void WikipediaActivity::fetchFullArticle() {
  LOG_DBG("WIKI", "fetchFullArticle: textLength=%zu", textLength);

  std::string fallbackText;
  if (textLength > 0 && textBuffer) {
    fallbackText.assign(textBuffer.get(), textLength);
  }

  state = State::LOADING_FULL_ARTICLE;
  requestUpdate();
  freeBuffer();

  NetworkMemory::prepareBeforeNetwork(renderer, "WIKI", "before_full");
  if (WiFi.status() != WL_CONNECTED) {
    NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "full_wifi_check");
    startActivityForResult(WifiSelectionActivity::createNetworkOperation(renderer, mappedInput, /*syncRtcOnConnect=*/false),
                           [this](const ActivityResult& r) { if (!r.isCancelled) fetchFullArticle(); else showError(tr(STR_WIFI_CONN_FAILED)); });
    return;
  }

  std::string titleForUrl = currentQuery;
  for (auto& c : titleForUrl) { if (c == ' ') c = '_'; }
  std::string encodedTitle = urlEncode(titleForUrl);

  std::string cacheDir = cachePathForTitle(currentQuery);
  Storage.mkdir(cacheDir.c_str());
  std::string rawPath = cacheDir + "/" + RAW_FILE;
  Storage.mkdir(CACHE_DIR);
  LOG_DBG("WIKI", "Streaming wikitext JSON to SD: %s", rawPath.c_str());

  char url[512];
  snprintf(url, sizeof(url),
           "%s/w/api.php?action=parse&page=%s&prop=wikitext&format=json",
           wikipediaBaseUrl().c_str(), encodedTitle.c_str());

  size_t totalSaved = 0;
  // HTTP/TLS racchiusi in uno scope: sia HTTPClient sia il client TLS vengono
  // distrutti qui (ordine corretto: prima http, poi il client) liberando decine
  // di KB prima della conversione/restore. NON chiamare httpClient.reset()
  // manualmente: HTTPClient conserva un riferimento interno al client TLS, e
  // il suo distruttore finirebbe in un double free (multi_heap_free).
  {
  std::unique_ptr<NetworkClient> httpClient;
  auto* secClient = new NetworkClientSecure();
  secClient->setInsecure();
  secClient->setTimeout(60);
  httpClient.reset(secClient);

  HTTPClient http;
  http.begin(*httpClient, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32/1.0");

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("WIKI", "HTTP failed: %d", httpCode);
    http.end();
    if (!fallbackText.empty()) {
      char* buf = ensureBuffer();
      size_t len = std::min(fallbackText.size(), static_cast<size_t>(TEXT_BUF_SIZE - 1));
      memcpy(buf, fallbackText.c_str(), len);
      textLength = len; buf[textLength] = '\0';
      cacheArticle(currentQuery);
      openArticleForReading(currentQuery);
    } else { showError(tr(STR_WIKIPEDIA_ERROR)); }
    return;
  }


  HalFile sdFile;
  if (!Storage.openFileForWrite("WIKI", rawPath.c_str(), sdFile)) {
    LOG_ERR("WIKI", "Failed to open SD file for writing");
    http.end();
    showError(tr(STR_WIKIPEDIA_ERROR));
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    LOG_ERR("WIKI", "Failed to get HTTP stream");
    sdFile.close();
    http.end();
    showError(tr(STR_WIKIPEDIA_ERROR));
    return;
  }

  uint8_t chunkBuf[512];
  unsigned long lastChunk = millis();
  bool streamOk = true;
  const unsigned long GRACEFUL_IDLE = 10000;
  const unsigned long STALL_TIMEOUT = 60000;

  while (http.connected() || stream->available()) {
    if (stream->available() == 0) {
      unsigned long idle = millis() - lastChunk;
      if (idle > GRACEFUL_IDLE && totalSaved > 1000) {
        LOG_DBG("WIKI", "Idle %lu ms, download assumed complete at %zu bytes", idle, totalSaved);
        break;
      }
      if (idle > STALL_TIMEOUT) {
        LOG_ERR("WIKI", "Stream stalled after %zu bytes (%lu ms idle)", totalSaved, idle);
        streamOk = false;
        break;
      }
      delay(5);
      continue;
    }
    int got = stream->read(chunkBuf, sizeof(chunkBuf));
    if (got > 0) {
      size_t wrote = sdFile.write(chunkBuf, static_cast<size_t>(got));
      if (wrote != static_cast<size_t>(got)) {
        LOG_ERR("WIKI", "SD write failed at %zu bytes", totalSaved);
        streamOk = false;
        break;
      }
      totalSaved += wrote;
      lastChunk = millis();
    } else if (got < 0) {
      LOG_ERR("WIKI", "Stream read error: %d at %zu bytes", got, totalSaved);
      streamOk = false;
      break;
    }
  }

  sdFile.close();

  if (!streamOk || totalSaved == 0) {
    http.end();
    NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "after_full");
    Storage.remove(rawPath.c_str());
    LOG_DBG("WIKI", "Download incomplete (%zu bytes), using fallback", totalSaved);
    if (!fallbackText.empty()) {
      char* buf = ensureBuffer();
      size_t len = std::min(fallbackText.size(), static_cast<size_t>(TEXT_BUF_SIZE - 1));
      memcpy(buf, fallbackText.c_str(), len);
      textLength = len; buf[textLength] = '\0';
      openArticleForReading(currentQuery);
    } else { showError(tr(STR_WIKIPEDIA_ERROR)); }
    return;
  }

  http.end();
  }  // fine scope HTTP/TLS: distrugge http e httpClient (ordine corretto)

  LOG_DBG("WIKI", "Streamed %zu bytes to SD, starting conversion...", totalSaved);

  LOG_DBG("WIKI", "Converting wikitext to markdown...");
  HalFile inFile;
  if (!Storage.openFileForRead("WIKI", rawPath, inFile)) {
    LOG_ERR("WIKI", "Failed to open raw JSON for conversion");
    showError(tr(STR_WIKIPEDIA_ERROR));
    return;
  }

  std::string articlePath = cacheDir + "/" + ARTICLE_FILE;
  if (Storage.exists(articlePath.c_str())) Storage.remove(articlePath.c_str());

  WikitextToMarkdown converter;
  bool convOk = converter.convert(inFile, articlePath.c_str());
  inFile.close();
  Storage.remove(rawPath.c_str());

  if (!convOk) {
    LOG_ERR("WIKI", "Wikitext conversion failed");
    Storage.remove(articlePath.c_str());
    if (!fallbackText.empty()) {
      char* buf = ensureBuffer();
      size_t len = std::min(fallbackText.size(), static_cast<size_t>(TEXT_BUF_SIZE - 1));
      memcpy(buf, fallbackText.c_str(), len);
      textLength = len; buf[textLength] = '\0';
      state = State::ARTICLE_DISPLAY;
      requestUpdate();
    } else { showError(tr(STR_WIKIPEDIA_ERROR)); }
    return;
  }

  LOG_DBG("WIKI", "Conversion done. Article saved to SD cache.");
  // Persist the real display title so the CACHED_PAGES list can show and
  // reopen the article by title (the folder name is a hash of the title).
  Storage.writeFile((cacheDir + "/" + TITLE_FILE).c_str(), String(currentQuery.c_str()));

  size_t writtenSize = 0;
  {
    HalFile check;
    if (Storage.openFileForRead("WIKI", articlePath.c_str(), check)) {
      writtenSize = check.size();
      check.flush();
      check.close();
    }
    if (writtenSize == 0) {
      LOG_ERR("WIKI", "Converted article is empty; nothing to open");
      showError(tr(STR_WIKIPEDIA_ERROR));
      return;
    }
  }

  // Il flusso di download+conversione usa allocazioni transienti (HTTP, chunk,
  // conversione). Verifichiamo lo stato dell'heap e la coerenza prima di
  // toccare la cache/reader: un abort() qui indica corruzione heap dello
  // streaming/conversione, non del reader.
  heap_caps_check_integrity_all(true);
  LOG_DBG("HCR-FRAG", "WIKI full-article after-convert: free=%d maxA=%d frag=%d bytes=%zu",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()), writtenSize);

  loadCachedPages();
  LOG_DBG("HCR-FRAG", "WIKI full-article after-loadCachedPages: free=%d maxA=%d frag=%d pages=%zu",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()),
          cachedPageTitles.size());

  // Il WikiTxtReader NON usa le reading stats. Dopo lo streaming+conversione
  // l'heap è basso/frammentato (~46KB, maxA~20KB): ricaricare le 29 stats qui
  // richiede allocazioni grandi e faceva abort() (illegal instruction). Le
  // stats sono già persistite su file da prepareBeforeNetwork; verranno
  // ricaricate in modo lazy da ensureLoaded() alla prossima esigenza reale.
  NetworkMemory::restoreAfterNetwork(renderer, "WIKI", "after_full", /*reloadReadingStats=*/false);
  LOG_DBG("HCR-FRAG", "WIKI full-article after-restore: free=%d maxA=%d frag=%d",
          static_cast<int>(ESP.getFreeHeap()), static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));

  openArticleForReading(currentQuery);
}

void WikipediaActivity::launchSearchKeyboard() {
  // Launch the text editor. On cancel we must return to the Wikipedia main page
  // (SEARCH_INPUT), not close the app; on confirm we run the search.
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_WIKIPEDIA), searchInput, 128,
                                              InputType::Text, WikipediaIcon, /*headerIconSize=*/32),
      [this](const ActivityResult& r) {
        if (r.isCancelled) {
          // Stay on the main search page.
          state = State::SEARCH_INPUT;
          requestUpdate();
          return;
        }
        const auto* kbResult = std::get_if<KeyboardResult>(&r.data);
        if (!kbResult) {
          state = State::SEARCH_INPUT;
          requestUpdate();
          return;
        }
        searchInput = kbResult->text;
        if (searchInput.empty()) {
          state = State::SEARCH_INPUT;  // empty query: just return to main page
          requestUpdate();
          return;
        }
        performSearch(searchInput);
      });
}

void WikipediaActivity::onWifiSelectionComplete(bool connected) {
  if (connected && !currentQuery.empty()) performSearch(currentQuery);
  else showError(tr(STR_WIFI_CONN_FAILED));
}

void WikipediaActivity::goBackToResults() {
  freeBuffer(); state = State::SEARCH_RESULTS; requestUpdate();
}

void WikipediaActivity::showError(const std::string& msg) {
  freeBuffer(); errorMessage = msg; state = State::ERROR; requestUpdate();
}

void WikipediaActivity::loadHistory() {
  historyQueries.clear();
  if (!Storage.exists(HISTORY_FILE)) return;
  String content = Storage.readFile(HISTORY_FILE);
  int start = 0;
  while (start < static_cast<int>(content.length())) {
    int end = content.indexOf('\n', start);
    if (end < 0) end = static_cast<int>(content.length());
    if (end > start) {
      std::string line = content.substring(start, end).c_str();
      while (!line.empty() && (line.back()=='\r' || line.back()=='\n' || line.back()==' ')) line.pop_back();
      if (!line.empty()) historyQueries.push_back(line);
    }
    start = end + 1;
  }
}

void WikipediaActivity::saveToHistory(const std::string& query) {
  String content;
  if (Storage.exists(HISTORY_FILE)) content = Storage.readFile(HISTORY_FILE);
  String newEntry = String(query.c_str()) + "\n";
  int dupPos = content.indexOf(newEntry);
  if (dupPos >= 0) {
    int dupEnd = content.indexOf('\n', dupPos + 1);
    if (dupEnd < 0) dupEnd = static_cast<int>(content.length());
    content = content.substring(0, dupPos) + content.substring(dupEnd + 1);
  }
  content = newEntry + content;
  int lineCount = 0, trimPos = 0;
  for (int i = 0; i < static_cast<int>(content.length()) && lineCount <= MAX_HISTORY; i++) {
    if (content[i] == '\n') { lineCount++; if (lineCount == MAX_HISTORY) { trimPos = i + 1; break; } }
  }
  if (trimPos > 0) content = content.substring(0, trimPos);
  Storage.writeFile(HISTORY_FILE, content);
  loadHistory();
}

std::string WikipediaActivity::cachePathForTitle(const std::string& title) {
  return std::string(CACHE_DIR) + "/wiki_" + std::to_string(std::hash<std::string>{}(title));
}

bool WikipediaActivity::cacheArticle(const std::string& title) {
  if (!textBuffer || textLength == 0) return false;
  const std::string cacheDir = cachePathForTitle(title);
  Storage.mkdir(cacheDir.c_str());
  const std::string articlePath = cacheDir + "/" + ARTICLE_FILE;
  String s(textBuffer.get(), textLength);
  bool ok = Storage.writeFile(articlePath.c_str(), s);
  if (ok) {
    // Persist the real display title so the CACHED_PAGES list can show and
    // reopen the article even though the folder name is a hash.
    Storage.writeFile((cacheDir + "/" + TITLE_FILE).c_str(), String(title.c_str()));
    LOG_DBG("WIKI", "Cached article: %s (%zu bytes)", articlePath.c_str(), textLength);
    const std::string legacyPath = legacyCachePathForTitle(title);
    if (Storage.exists(legacyPath.c_str())) {
      Storage.remove(legacyPath.c_str());
      LOG_DBG("WIKI", "Removed legacy cache: %s", legacyPath.c_str());
    }
    loadCachedPages();
  }
  return ok;
}

bool WikipediaActivity::loadCachedArticle(const std::string& title) {
  const std::string cacheDir = cachePathForTitle(title);
  const std::string articlePath = cacheDir + "/" + ARTICLE_FILE;
  if (Storage.exists(articlePath.c_str())) {
    HalFile f;
    if (!Storage.openFileForRead("WIKI", articlePath.c_str(), f)) return false;
    size_t fileSize = f.size();
    f.close();

    if (fileSize == 0) return false;

    freeBuffer();

    if (fileSize > TEXT_BUF_SIZE - 1) {
      g_articleFilePath = articlePath;
      textLength = fileSize;
      LOG_DBG("WIKI", "Loaded large cached article: %s (%zu bytes on SD)", articlePath.c_str(), textLength);
      return true;
    }

    String content = Storage.readFile(articlePath.c_str());
    if (content.length() == 0) return false;

    char* buf = ensureBuffer();
    size_t maxCopy = std::min(static_cast<size_t>(content.length()), TEXT_BUF_SIZE - 1);
    memcpy(buf, content.c_str(), maxCopy);
    textLength = maxCopy;
    buf[textLength] = '\0';
    g_articleFilePath.clear();
    
    LOG_DBG("WIKI", "Loaded cached article: %s (%zu bytes in RAM)", articlePath.c_str(), textLength);
    return true;
  }

  const std::string legacyPath = legacyCachePathForTitle(title);
  if (Storage.exists(legacyPath.c_str())) {
    HalFile f;
    if (!Storage.openFileForRead("WIKI", legacyPath.c_str(), f)) return false;
    size_t fileSize = f.size();
    f.close();

    if (fileSize == 0) return false;

    freeBuffer();

    if (fileSize > TEXT_BUF_SIZE - 1) {
      g_articleFilePath = legacyPath;
      textLength = fileSize;
      LOG_DBG("WIKI", "Loaded legacy cached article: %s (%zu bytes on SD)", legacyPath.c_str(), textLength);
      return true;
    }

    String content = Storage.readFile(legacyPath.c_str());
    if (content.length() == 0) return false;

    char* buf = ensureBuffer();
    size_t maxCopy = std::min(static_cast<size_t>(content.length()), TEXT_BUF_SIZE - 1);
    memcpy(buf, content.c_str(), maxCopy);
    textLength = maxCopy;
    buf[textLength] = '\0';
    g_articleFilePath.clear();
    
    LOG_DBG("WIKI", "Loaded legacy cached article: %s (%zu bytes in RAM)", legacyPath.c_str(), textLength);
    return true;
  }

  return false;
}

void WikipediaActivity::loadCachedPages() {
  cachedPageTitles.clear();
  Storage.mkdir(CACHE_DIR);
  auto files = Storage.listFiles(CACHE_DIR, 200);
  for (auto& f : files) {
    std::string name = f.c_str();
    // New format: per-article directories named wiki_<hash> containing article.md
    // and a title.txt carrying the real display title. Without title.txt we
    // cannot show a readable name nor reopen the article, so we skip it.
    if (name.size() > 5 && name.compare(0, 5, "wiki_") == 0 && Storage.exists((std::string(CACHE_DIR) + "/" + name + "/" + ARTICLE_FILE).c_str())) {
      const std::string titlePath = std::string(CACHE_DIR) + "/" + name + "/" + TITLE_FILE;
      const String titleFile = Storage.readFile(titlePath.c_str());
      if (titleFile.length() > 0) {
        cachedPageTitles.push_back(std::string(titleFile.c_str(), titleFile.length()));
      }
    }
  }
  // Fallback: legacy flat .wiki files for backward compatibility.
  for (auto& f : files) {
    std::string name = f.c_str();
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".wiki") == 0) {
      std::string title = name.substr(0, name.size() - 4);
      // Sostituito std::replace con ciclo manuale per massima compatibilità di compilazione
      for (char& c : title) {
        if (c == '_') c = ' ';
      }
      cachedPageTitles.push_back(title);
    }
  }
  std::sort(cachedPageTitles.begin(), cachedPageTitles.end());
  LOG_DBG("WIKI", "Loaded %zu cached pages", cachedPageTitles.size());
}