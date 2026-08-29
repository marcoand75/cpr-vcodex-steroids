#include "LyraMarcoand75Theme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "I18n.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/PanelDrawHelper.h"
#include "components/icons/apps_hub.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/bookshelf.h"
#include "components/icons/cache_cleaner.h"
#include "components/icons/calendar_time.h"
#include "components/icons/calibre.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/ClipIcon32.h"
#include "components/icons/cover.h"
#include "components/icons/delete_file.h"
#include "components/icons/dictionary.h"
#include "components/icons/dictionary2.h"
#include "components/icons/file_transfer.h"
#include "components/icons/finish_flag.h"
#include "components/icons/flashcardquiz.h"
#include "components/icons/folder.h"
#include "components/icons/goalsmedal.h"
#include "components/icons/gps_found.h"
#include "components/icons/heatmap.h"
#include "components/icons/heart.h"
#include "components/icons/hotspot.h"
#include "components/icons/image.h"
#include "components/icons/library.h"
#include "components/icons/library_book.h"
#include "components/icons/library_new.h"
#include "components/icons/lostdevice.h"
#include "components/icons/medal_alt.h"
#include "components/icons/notification_unread.h"
#include "components/icons/opdsbrowser.h"
#include "components/icons/pageview.h"
#include "components/icons/readingprofile.h"
#include "components/icons/readingstats.h"
#include "components/icons/recent.h"
#include "components/icons/recentbooks.h"
#include "components/icons/rotation.h"
#include "components/icons/screensaver.h"
#include "components/icons/search.h"
#include "components/icons/search_minus.h"
#include "components/icons/search_plus.h"
#include "components/icons/settings.h"
#include "components/icons/settings2.h"
#include "components/icons/sleep.h"
#include "components/icons/sort_asc.h"
#include "components/icons/sort_desc.h"
#include "components/icons/time_fast.h"
#include "components/icons/transfer.h"
#include "components/icons/trophy.h"
#include "components/icons/wifi.h"
#include "components/icons/wikipediaicon.h"
#include "components/icons/quickcards.h"
#include "fontIds.h"

namespace {

enum class TrendSymbol : uint8_t { Down, Up, Equal };

TrendSymbol getTrendSymbol(uint64_t todayMs, uint64_t dailyAverageMs) {
  if (dailyAverageMs == 0) {
    return todayMs > 0 ? TrendSymbol::Up : TrendSymbol::Equal;
  }
  const uint64_t tolerance = (dailyAverageMs * 3ULL) / 100ULL;
  if (todayMs < dailyAverageMs - tolerance) return TrendSymbol::Down;
  if (todayMs > dailyAverageMs + tolerance) return TrendSymbol::Up;
  return TrendSymbol::Equal;
}

void drawTrendSymbol(const GfxRenderer& r, int x, int y, TrendSymbol symbol) {
  switch (symbol) {
    case TrendSymbol::Down:
      r.drawLine(x, y, x + 5, y + 5, 2, true);
      r.drawLine(x + 5, y + 5, x + 10, y, 2, true);
      break;
    case TrendSymbol::Up:
      r.drawLine(x, y + 5, x + 5, y, 2, true);
      r.drawLine(x + 5, y, x + 10, y + 5, 2, true);
      break;
    case TrendSymbol::Equal:
      r.drawLine(x, y, x + 10, y, 2, true);
      break;
  }
}

constexpr int kOverlap = 35;
constexpr int kCoverTopPad = 0;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;
constexpr int kSelectionLineW = 3;
constexpr int kCenterOutlineW = 2;
constexpr int kMenuIconSize = 32;
constexpr int kMenuIconPad = 14;
constexpr int kHighlightPad = 12;
constexpr int kVisibleMenuSlots = 7;

// 5-cover perspective carousel constants
constexpr int kFiveCoverCenterW = 210;
constexpr int kFiveCoverCenterH = 340;
constexpr int kFiveCoverNearW = 90;
constexpr int kFiveCoverNearH = 300;
constexpr int kFiveCoverFarW = 70;
constexpr int kFiveCoverFarH = 250;
constexpr int kFiveCoverOverlap = 16;
constexpr int kFiveCoverFarOverlap = 20;
constexpr int kFiveCoverOutlineW = 2;
constexpr int kFiveCoverHaloW = 1;

// Centratura perfetta: offset a 0
constexpr int kCenterXOffset = 0;

constexpr int kDotsToPanelGap = 6;

constexpr int kProgSegW = 14;
constexpr int kProgSegH = 24;
constexpr int kProgSegGap = 4;
constexpr int kProgSegCount = 15;

int lastCarouselSelectorIndex = -1;

const uint8_t* iconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:    return FolderIcon;
    case UIIcon::Book:      return BookIcon;
    case UIIcon::Recent:    return RecentIcon;
    case UIIcon::Settings:  return Settings2Icon;
    case UIIcon::Apps:      return SettingsIcon;
    case UIIcon::Transfer:  return TransferIcon;
    case UIIcon::Library:   return LibraryIcon;
    case UIIcon::Trophy:    return TrophyIcon;
    case UIIcon::Wifi:      return WifiIcon;
    case UIIcon::Hotspot:   return HotspotIcon;
    case UIIcon::Image:     return ImageIcon;
    case UIIcon::Heart:     return HeartIcon;
    case UIIcon::ScreenSaver: return ScreenSaverIcon;
    case UIIcon::Bookshelf: return BookshelfIcon;
    case UIIcon::SleepMode: return SleepModeIcon32;
    case UIIcon::CleanMonitor: return CleanMonitorIcon32;
    case UIIcon::Heatmap: return HeatmapReadingIcon32;
    case UIIcon::FlashcardQuiz: return FlashcardQuizIcon32;
    case UIIcon::ReadingProfile: return ReadingProfileIcon32;
    case UIIcon::LostDevice: return LostDeviceIcon32;
    case UIIcon::OpdsBrowser: return OPDSBrowserIcon;
    case UIIcon::Dictionary: return DictionaryIcon;
    case UIIcon::GoalsMedal: return GoalsMedalIcon;
    case UIIcon::ReadingStatsIcon: return ReadingStatsIcon32;
    case UIIcon::RecentBooks: return RecentBooksIcon32;
    case UIIcon::Bookmark: return BookmarkIcon;
    case UIIcon::Search: return SearchIcon;
    case UIIcon::Rotation: return RotationIcon;
    case UIIcon::Pageview: return PageviewIcon;
    case UIIcon::SearchPlus: return SearchPlusIcon;
    case UIIcon::SearchMinus: return SearchMinusIcon;
    case UIIcon::TimeFast: return TimeFastIcon;
    case UIIcon::SortAsc: return SortAscIcon;
    case UIIcon::SortDesc: return SortDescIcon;
    case UIIcon::LibraryNew: return LibraryNewIcon;
    case UIIcon::GpsFound: return GpsFoundIcon;
    case UIIcon::MedalAlt: return MedalAltIcon;
    case UIIcon::Dictionary2: return Dictionary2Icon;
    case UIIcon::AppsHub: return AppsHubIcon;
    case UIIcon::CalendarTime: return CalendarTimeIcon;
    case UIIcon::LibraryBook: return LibraryBookIcon;
    case UIIcon::DeleteFile: return DeleteFileIcon;
    case UIIcon::CacheCleaner: return CacheCleanerIcon;
    case UIIcon::FinishFlag: return FinishFlagIcon;
    case UIIcon::NotificationUnread: return NotificationUnreadIcon;
    case UIIcon::FileTransfer: return FileTransferIcon;
    case UIIcon::Calibre: return CalibreIcon;
    case UIIcon::File: return ClipIcon32;
    case UIIcon::Wikipedia: return WikipediaIcon;
    case UIIcon::QuickCards: return QuickCardsIcon;
    default: return nullptr;
  }
}

void drawCoverPlaceholder(GfxRenderer& renderer, int x, int y, int maxW, int maxH,
                                             const char* title) {
  renderer.drawRoundedRect(x, y, maxW, maxH, 1, kCornerRadius, true);
  renderer.fillRoundedRect(x, y + maxH / 3, maxW, 2 * maxH / 3, kCornerRadius, false, false, true, true, Color::Black);
  renderer.drawIcon(CoverIcon, x + maxW / 2 - 16, y + 8, 32, 32);
  if (title && title[0]) {
    // FIX: Usa truncatedText per accorciare il titolo con "..." se troppo lungo
    const auto truncTitle = renderer.truncatedText(UI_10_FONT_ID, title, maxW - 8, EpdFontFamily::BOLD);
    const int titleW = renderer.getTextWidth(UI_10_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + (maxW - titleW) / 2, y + maxH / 3 + 4, truncTitle.c_str(), false, EpdFontFamily::BOLD);
  }
}

// --- Data panel helpers ---

static void drawCyberPanel(const GfxRenderer& r, int x, int y, int w, int h, bool sel) {
  PanelDrawHelper::drawCyberpunkPanel(r, x, y, w, h, sel);
}

void drawSegmentProgressBar(const GfxRenderer& r, int x, int y, int filled, int total) {
  int sx = x;
  for (int i = 0; i < total; ++i) {
    if (i < filled) {
      r.fillRect(sx, y, kProgSegW, kProgSegH, true);
      if (kProgSegW > 4 && kProgSegH > 4) {
        r.drawLine(sx + 2, y + 2, sx + kProgSegW - 3, y + 2, 1, false);
      }
    } else {
      r.drawRect(sx, y, kProgSegW, kProgSegH, true);
    }
    sx += kProgSegW + kProgSegGap;
  }
  r.drawLine(x - 2, y - 2, x - 2, y + kProgSegH + 2, 1, true);
  r.drawLine(x + total * (kProgSegW + kProgSegGap) - kProgSegGap + 1, y - 2,
             x + total * (kProgSegW + kProgSegGap) - kProgSegGap + 1, y + kProgSegH + 2, 1, true);
}

uint8_t getBookProgress(const RecentBook& b) {
  return READING_STATS.getBookProgressForHome(b.bookId, b.path);
}

const ReadingBookStats* getBookStats(const RecentBook& b) {
  return READING_STATS.getHomeBookStatsForRender(b.bookId, b.path);
}

void fmtDuration(uint64_t ms, char* buf, size_t bufSize) {
  if (ms == 0) { snprintf(buf, bufSize, "0m"); return; }
  uint64_t m = ms / 60000ULL, h = m / 60;
  m %= 60;
  if (h) snprintf(buf, bufSize, "%lluh%llum", h, m);
  else snprintf(buf, bufSize, "%llum", m);
}

std::string getEta(const ReadingBookStats& s) {
  if (s.completed || s.lastProgressPercent >= 100 || s.totalReadingMs < 600000ULL || s.lastProgressPercent < 5)
    return "";
  uint64_t tot = (s.totalReadingMs * 100ULL + s.lastProgressPercent - 1) / s.lastProgressPercent;
  if (tot <= s.totalReadingMs) return "";
  uint64_t rem = ((tot - s.totalReadingMs + 299999ULL) / 300000ULL) * 300000ULL;
  uint64_t min = rem / 60000ULL, h = min / 60;
  min %= 60;
  return "~" + std::to_string(h) + "h" + std::to_string(min) + "m";
}

void drawDataPanel(const GfxRenderer& r, const RecentBook& book, bool inCar, int px, int py, int pw) {
  const ReadingBookStats* stats = getBookStats(book);
  const uint8_t pct = getBookProgress(book);
  const bool done = stats && stats->completed;
  const uint64_t tMs = stats ? stats->totalReadingMs : 0;
  const uint32_t sess = stats ? stats->sessions : 0;
  char etaBuf[32] = {};
  if (stats) {
    const auto e = getEta(*stats);
    snprintf(etaBuf, sizeof(etaBuf), "%s", e.c_str());
  } else {
    snprintf(etaBuf, sizeof(etaBuf), "...");
  }
  char timeVal[32], sessVal[16], daysVal[16], streakVal[16], goalVal[32], dayVal[32], avgVal[32], booksFinished[16];
  fmtDuration(tMs, timeVal, sizeof(timeVal));
  snprintf(sessVal, sizeof(sessVal), "%u", sess);
  // Days: number of distinct reading days for this book
  snprintf(daysVal, sizeof(daysVal), "%u", stats ? static_cast<uint32_t>(stats->readingDays.size()) : 0u);
  fmtDuration(getDailyReadingGoalMs(), goalVal, sizeof(goalVal));
  fmtDuration(READING_STATS.getTodayReadingMs(), dayVal, sizeof(dayVal));
  fmtDuration(READING_STATS.getGlobalSummary().dailyAverageMs, avgVal, sizeof(avgVal));
  snprintf(streakVal, sizeof(streakVal), "%dd", READING_STATS.getCurrentStreakDays());
  snprintf(booksFinished, sizeof(booksFinished), "%d", READING_STATS.getBooksFinishedCount());

  constexpr int gap = 6;
  constexpr int pad = 5;
  constexpr int textLeft = 20;
  const int dataFont = UI_10_FONT_ID;
  const int lh = r.getLineHeight(dataFont);
  int curY = py;

  {
    const int h1 = r.getLineHeight(SMALL_FONT_ID) + lh + 2 * pad + 6;
    drawCyberPanel(r, px, curY, pw, h1, inCar);
    const auto tTrunc = r.truncatedText(UI_12_FONT_ID, book.title.c_str(), pw - textLeft - pad, EpdFontFamily::BOLD);
    r.drawText(UI_12_FONT_ID, px + textLeft, curY + pad + 2, tTrunc.c_str(), true, EpdFontFamily::BOLD);
    if (!book.author.empty()) {
      const auto aTrunc = r.truncatedText(SMALL_FONT_ID, book.author.c_str(), pw - textLeft - pad);
      r.drawText(SMALL_FONT_ID, px + textLeft, curY + pad + 2 + lh + 2, aTrunc.c_str(), true);
    }
    curY += h1 + gap;
  }

  {
    const int colW = (pw - gap) / 2;
    const int h2 = lh * 5 + 2 * pad + 6;
    // ── Left: BOOK STATS ──
    drawCyberPanel(r, px, curY, colW, h2, inCar);
    int ly = curY + pad;
    r.drawText(dataFont, px + textLeft, ly, tr(STR_HOME_PANEL_BOOK), true, EpdFontFamily::BOLD);
    ly += lh + 2;
    char buf[48];
    // "Read" = total reading time spent on this book
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_TIME), timeVal);
    r.drawText(dataFont, px + textLeft, ly, buf, true);
    ly += lh + 2;
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_SESSIONS), sessVal);
    r.drawText(dataFont, px + textLeft, ly, buf, true);
    ly += lh + 2;
    // Days = distinct days this book was read
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_DAYS), daysVal);
    r.drawText(dataFont, px + textLeft, ly, buf, true);
    ly += lh + 2;
    // "Left" = estimated remaining time to finish the book
    if (!done && etaBuf[0] != '\0') {
      snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_ETA), etaBuf);
    } else {
      snprintf(buf, sizeof(buf), "%s: --", tr(STR_HOME_PANEL_ETA));
    }
    r.drawText(dataFont, px + textLeft, ly, buf, true);

    // ── Right: GLOBAL STATS ──
    const int rightX = px + colW + gap;
    drawCyberPanel(r, rightX, curY, colW, h2, inCar);
    int ry = curY + pad;
    r.drawText(dataFont, rightX + textLeft, ry, tr(STR_HOME_PANEL_STATS), true, EpdFontFamily::BOLD);
    ry += lh + 2;
    snprintf(buf, sizeof(buf), "%s: %s (%s)", tr(STR_HOME_PANEL_TODAY), dayVal, avgVal);
    r.drawText(dataFont, rightX + textLeft, ry, buf, true);
    // Trend symbol vs daily average
    {
      const int todayTextWidth = r.getTextWidth(dataFont, buf, EpdFontFamily::REGULAR);
      const int iconX = rightX + textLeft + todayTextWidth + 6;
      const int iconY = ry + lh / 2;
      const TrendSymbol trend = getTrendSymbol(READING_STATS.getTodayReadingMs(), READING_STATS.getGlobalSummary().dailyAverageMs);
      drawTrendSymbol(r, iconX, iconY, trend);
    }
    ry += lh + 2;
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_GOAL), goalVal);
    r.drawText(dataFont, rightX + textLeft, ry, buf, true);
    // Mini-checkmark when daily goal is reached
    {
      const int chkX = rightX + textLeft + r.getTextWidth(dataFont, buf, EpdFontFamily::REGULAR) + 6;
      const int chkY = ry + lh / 2;
      if (getDailyReadingGoalMs() > 0 && READING_STATS.getTodayReadingMs() >= getDailyReadingGoalMs()) {
        r.drawLine(chkX, chkY, chkX + 4, chkY + 5, 2, true);
        r.drawLine(chkX + 4, chkY + 5, chkX + 11, chkY - 3, 2, true);
      }
    }
    ry += lh + 2;
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_STREAK), streakVal);
    r.drawText(dataFont, rightX + textLeft, ry, buf, true);
    ry += lh + 2;
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_HOME_PANEL_FINISHED), booksFinished);
    r.drawText(dataFont, rightX + textLeft, ry, buf, true);

    curY += h2 + gap;
  }

  {
    const int h3 = kProgSegH + 2 * pad + 8;
    drawCyberPanel(r, px, curY, pw, h3, inCar);
    char pctBuf[8]; snprintf(pctBuf, sizeof(pctBuf), "%u%%", pct);
    const int pctW = r.getTextWidth(UI_12_FONT_ID, pctBuf, EpdFontFamily::BOLD) + 8;
    const int segFull = kProgSegW + kProgSegGap;
    const int availBarW = pw - 48;
    const int dynSegCount = std::max(3, (availBarW - pctW - 6) / segFull);
    const int barTotalW = dynSegCount * segFull - kProgSegGap;
    const int totalContentW = barTotalW + 6 + pctW;
    const int contentOffset = (pw - totalContentW) / 2;
    if (!done) {
      const int segs = (pct * dynSegCount + 50) / 100;
      const int barX = px + contentOffset;
      const int barY = curY + (h3 - kProgSegH) / 2;
      drawSegmentProgressBar(r, barX, barY, segs, dynSegCount);
      int pctX = barX + barTotalW + 6;
      int pctY2 = barY + (kProgSegH - r.getLineHeight(UI_12_FONT_ID)) / 2;
      r.drawText(UI_12_FONT_ID, pctX, pctY2, pctBuf, true, EpdFontFamily::BOLD);
    } else {
      r.drawText(UI_12_FONT_ID, px + pw / 2 - 35, curY + (h3 - r.getLineHeight(UI_12_FONT_ID)) / 2,
                 "COMPLETED", true, EpdFontFamily::BOLD);
    }
    curY += h3 + gap;
  }
}

void drawReadRibbon(GfxRenderer& renderer, int coverX, int coverY, int coverW, int coverH) {
  (void)coverH;
  const int leg = std::max(20, std::min(coverW * 2 / 5, 44));
  const int rightX = coverX + coverW;
  for (int dy = 0; dy < leg; ++dy) {
    const int spanW = leg - dy;
    renderer.fillRect(rightX - spanW, coverY + dy, spanW, 1, true);
  }
  const char* label = "Read";
  const int tw = renderer.getTextWidth(SMALL_FONT_ID, label, EpdFontFamily::BOLD);
  const int th = renderer.getLineHeight(SMALL_FONT_ID);
  const int rowFromTop = leg / 3;
  const int avail = leg - rowFromTop;
  if (tw + 6 <= avail) {
    const int tx = rightX - (avail + tw) / 2;
    const int ty = coverY + rowFromTop - th / 2;
    renderer.drawText(SMALL_FONT_ID, tx, ty, label, false, EpdFontFamily::BOLD);
  } else {
    const int cx = rightX - leg / 3;
    const int cy = coverY + leg / 3;
    renderer.drawLine(cx - 5, cy, cx - 1, cy + 4, 2, false);
    renderer.drawLine(cx - 1, cy + 4, cx + 6, cy - 4, 2, false);
  }
}

}  // namespace

void LyraMarcoand75Theme::setPreRenderIndex(int index) { lastCarouselSelectorIndex = index; }

void LyraMarcoand75Theme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                               const std::vector<RecentBook>& recentBooks,
                                               const int selectorIndex, bool& coverRendered,
                                               bool& coverBufferStored, bool& bufferRestored,
                                               std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  if (recentBooks.empty()) { drawEmptyRecents(renderer, rect); return; }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = selectorIndex < bookCount;
  int centerIdx = inCarouselRow ? selectorIndex : (lastCarouselSelectorIndex >= 0 ? lastCarouselSelectorIndex : 0);
  centerIdx = std::max(0, std::min(centerIdx, bookCount - 1));
  if (centerIdx != lastCarouselSelectorIndex) { coverRendered = false; coverBufferStored = false; }

  const int screenW = renderer.getScreenWidth();
  const int centerTileY = rect.y + kCoverTopPad;
  const int centerX = (screenW - kFiveCoverCenterW) / 2 + kCenterXOffset;

  // Side cover positions
  const int nearLeftX  = centerX - kFiveCoverNearW + kFiveCoverOverlap;
  const int nearRightX = centerX + kFiveCoverCenterW - kFiveCoverOverlap;
  const int farLeftX   = std::max(8, nearLeftX - kFiveCoverFarW + kFiveCoverFarOverlap);
  const int farRightX  = std::min(screenW - kFiveCoverFarW - 8,
                                   nearRightX + kFiveCoverNearW - kFiveCoverFarOverlap);

  const int clampedFarLeftX  = farLeftX;
  const int clampedFarRightX = farRightX;

  // Y positions
  const int centerCoverTop = centerTileY + 12;
  const int centerMidY     = centerCoverTop + kFiveCoverCenterH / 2;
  const int nearLeftY  = centerMidY - kFiveCoverNearH / 2;
  const int nearRightY = centerMidY - kFiveCoverNearH / 2;
  const int farLeftY   = centerMidY - kFiveCoverFarH / 2 - 8;
  const int farRightY  = centerMidY - kFiveCoverFarH / 2 - 8;

  auto drawStackedCover = [&](int bookIdx, bool isLeft, bool isFar) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) {
      return false;
    }

    const RecentBook& book = recentBooks[bookIdx];

    const int sw = isFar ? kFiveCoverFarW : kFiveCoverNearW;
    const int sh = isFar ? kFiveCoverFarH : kFiveCoverNearH;
    const int sx = isFar ? (isLeft ? clampedFarLeftX : clampedFarRightX)
                         : (isLeft ? nearLeftX : nearRightX);
    const int sy = isFar ? (isLeft ? farLeftY : farRightY)
                         : (isLeft ? nearLeftY : nearRightY);

    // Aggiunge un bordo bianco (padding) sotto le cover near, esattamente come per la cover centrale
    if (!isFar) {
      renderer.fillRect(sx - kCenterOutlineW, sy - kCenterOutlineW, 
                        sw + 2 * kCenterOutlineW, sh + 2 * kCenterOutlineW, false);
    } else {
      renderer.fillRect(sx, sy, sw, sh, false);
    }

    bool hasCover = false;
    std::string thumbPath;

    if (!book.coverBmpPath.empty()) {
      // FIX: Ripristinata la ricerca originale delle cover per evitare problemi di percorso
      thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, sw, sh);
      if (!Storage.exists(thumbPath.c_str())) {
        thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath,
                                                kFiveCoverCenterW, kFiveCoverCenterH);
      }
      if (!Storage.exists(thumbPath.c_str())) {
        thumbPath = UITheme::getCoverThumbPath(
            book.coverBmpPath, LyraMarcoand75Metrics::values.homeCoverHeight);
      }
      if (!Storage.exists(thumbPath.c_str())) {
        thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath,
                                                LyraMarcoand75Theme::kCenterCoverW, 
                                                LyraMarcoand75Theme::kCenterCoverH);
      }
      
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const float bmpRatio  = static_cast<float>(bitmap.getWidth())
                                  / static_cast<float>(bitmap.getHeight());
          const float tileRatio = static_cast<float>(sw) / static_cast<float>(sh);

          // Allineamento reale a libri sovrapposti per tutte le cover laterali.
          if (bmpRatio > tileRatio) {
            int drawH = sh;
            int drawW = static_cast<int>(drawH * bmpRatio);
            int drawX = isLeft ? sx : (sx + sw - drawW);
            int drawY = sy;
            // Disegna l'immagine scalata in altezza
            renderer.drawBitmap(bitmap, drawX, drawY, drawW, drawH, 0.0f, 0.0f);
            
            // Copre la parte in eccesso con il bianco per simulare il taglio netto
            if (isLeft) {
              renderer.fillRect(sx + sw, sy, drawW - sw + 2, sh, false);
            } else {
              renderer.fillRect(drawX - 2, sy, sx - drawX + 2, sh, false);
            }
          } else {
            // Cover verticali: taglio centrato classico
            const float cropX = 0.0f;
            const float cropY = (bmpRatio < tileRatio) ? (1.0f - bmpRatio / tileRatio) : 0.0f;
            renderer.drawBitmap(bitmap, sx, sy, sw, sh, cropX, cropY);
          }
          hasCover = true;
        }
        file.close();
      }
    }

    if (!hasCover) {
      drawCoverPlaceholder(renderer, sx, sy, sw, sh, "");
    }

    // --- Outline ---
    // Outline rettangolare semplice per tutte le cover, niente più tagli trapezoidali.
    const int topY = sy;
    const int botY = sy + sh;

    renderer.drawLine(sx, topY, sx + sw, topY, kFiveCoverOutlineW, true);
    renderer.drawLine(sx, botY, sx + sw, botY, kFiveCoverOutlineW, true);
    renderer.drawLine(sx, topY, sx, botY, kFiveCoverOutlineW, true);
    renderer.drawLine(sx + sw, topY, sx + sw, botY, kFiveCoverOutlineW, true);

    // Dark overlay rettangolare pulito per le cover far
    if (isFar) {
      for (int y = 0; y < sh; y++) {
        for (int x = sx; x < sx + sw; x += 2) {
          if ((x + y) % 4 == 0) {
            renderer.drawPixel(x, sy + y, true);
          }
        }
      }
    }

    if (hasCover && !book.path.empty()) {
      SummaryJSON::BookBadge badge;
      if (READING_STATS.getBookHomeStats(book.bookId, book.path, badge) && badge.completed) {
        drawReadRibbon(renderer, sx, sy, sw, sh);
      }
    }
    return true;
  };

  if (!coverRendered) {
    lastCarouselSelectorIndex = centerIdx;

    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    const int panelX   = rect.x + 8;
    const int panelW   = rect.width - 16;
    const int dotsY    = centerCoverTop + kFiveCoverCenterH + 2;  // +8 - 6 = +2
    constexpr int carouselGap = 14;

    const int panelTopY = rect.y + kCoverTopPad + 6;
    const int panelBotY = dotsY + kDotSize + 14;
    const int panelH    = panelBotY - panelTopY;
    renderer.fillRect(panelX + 4, panelTopY, panelW - 8, panelH - 4, false);
    drawCyberPanel(renderer, panelX, panelTopY, panelW, panelH, inCarouselRow);

    const int showLeftCnt  = std::min(bookCount - 1, 2);
    const int showRightCnt = std::min(bookCount - 1, 2);

    // Far covers (drawn first, behind everything)
    if (showLeftCnt >= 2) {
      drawStackedCover((centerIdx + bookCount - 2) % bookCount, true, true);
    }
    if (showRightCnt >= 2) {
      drawStackedCover((centerIdx + 2) % bookCount, false, true);
    }
    // Near covers (drawn on top of far covers, pulendo l'overflow interno col loro bordo bianco)
    if (showLeftCnt >= 1) {
      drawStackedCover((centerIdx + bookCount - 1) % bookCount, true, false);
    }
    if (showRightCnt >= 1) {
      drawStackedCover((centerIdx + 1) % bookCount, false, false);
    }

    // Center cover background + outline
    renderer.fillRect(centerX - kCenterOutlineW - kFiveCoverHaloW,
                      centerCoverTop - kFiveCoverHaloW,
                      kFiveCoverCenterW + 2 * (kCenterOutlineW + kFiveCoverHaloW),
                      kFiveCoverCenterH + 2 * kFiveCoverHaloW, false);
    renderer.fillRect(centerX - kCenterOutlineW, centerCoverTop,
                      kFiveCoverCenterW + 2 * kCenterOutlineW,
                      kFiveCoverCenterH + 2 * kCenterOutlineW, false);

    {
      const RecentBook& book = recentBooks[centerIdx];
      bool hasCover = false;
      std::string thumbPath;
      if (!book.coverBmpPath.empty()) {
        thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath,
                                                kFiveCoverCenterW, kFiveCoverCenterH);
        if (!Storage.exists(thumbPath.c_str())) {
          thumbPath = UITheme::getCoverThumbPath(
              book.coverBmpPath, LyraMarcoand75Metrics::values.homeCoverHeight);
        }
        if (!Storage.exists(thumbPath.c_str())) {
          thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath,
                                                  LyraMarcoand75Theme::kCenterCoverW, 
                                                  LyraMarcoand75Theme::kCenterCoverH);
        }
        FsFile file;
        if (Storage.openFileForRead("HOME", thumbPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            const float bmpRatio  = static_cast<float>(bitmap.getWidth())
                                    / static_cast<float>(bitmap.getHeight());
            const float tileRatio = static_cast<float>(kFiveCoverCenterW)
                                    / static_cast<float>(kFiveCoverCenterH);
            const float cropX = (bmpRatio > tileRatio)
                                    ? (1.0f - tileRatio / bmpRatio)
                                    : 0.0f;
            const float cropY = (bmpRatio < tileRatio)
                                    ? (1.0f - bmpRatio / tileRatio)
                                    : 0.0f;
            renderer.drawBitmap(bitmap, centerX, centerCoverTop,
                                kFiveCoverCenterW, kFiveCoverCenterH, cropX, cropY);
            renderer.maskRoundedRectOutsideCorners(centerX, centerCoverTop,
                                                    kFiveCoverCenterW, kFiveCoverCenterH,
                                                    kCornerRadius, Color::White);
            hasCover = true;
          }
          file.close();
        }
      }
      if (!hasCover) {
        drawCoverPlaceholder(renderer, centerX, centerCoverTop,
                             kFiveCoverCenterW, kFiveCoverCenterH,
                             recentBooks[centerIdx].title.c_str());
      }
      if (hasCover && !book.path.empty()) {
        SummaryJSON::BookBadge badge;
        if (READING_STATS.getBookHomeStats(book.bookId, book.path, badge) && badge.completed) {
          drawReadRibbon(renderer, centerX, centerCoverTop,
                         kFiveCoverCenterW, kFiveCoverCenterH);
        }
      }
    }

    if (inCarouselRow) {
      renderer.drawRoundedRect(centerX - 1, centerCoverTop - 1, kFiveCoverCenterW + 2,
                               kFiveCoverCenterH + 2, 4, kCornerRadius + 1, true);
      renderer.drawRoundedRect(centerX - 3, centerCoverTop - 3, kFiveCoverCenterW + 6,
                               kFiveCoverCenterH + 6, 2, kCornerRadius + 2, false);
    }

    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerX + (kFiveCoverCenterW - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx) renderer.fillRect(dotX, dotsY + 12, kDotSize, kDotSize, true);
      else                  renderer.drawRect(dotX, dotsY + 12, kDotSize, kDotSize, true);
      dotX += kDotSize + kDotGap;
    }

    const int panelY = dotsY + kDotSize + carouselGap + 6;
    drawDataPanel(renderer, recentBooks[centerIdx], inCarouselRow, panelX, panelY, panelW);
    coverBufferStored = storeCoverBuffer();
    coverRendered     = coverBufferStored;

  }

  if (inCarouselRow) {
    renderer.drawRoundedRect(centerX - 1, centerCoverTop - 1, kFiveCoverCenterW + 2,
                             kFiveCoverCenterH + 2, 4, kCornerRadius + 1, true);
    renderer.drawRoundedRect(centerX - 3, centerCoverTop - 3, kFiveCoverCenterW + 6,
                             kFiveCoverCenterH + 6, 2, kCornerRadius + 2, false);
  }
}

void LyraMarcoand75Theme::drawCarouselBorder(GfxRenderer& renderer, Rect rect, bool inCarouselRow) const {
  if (!inCarouselRow) return;
  const int centerTileY = rect.y + kCoverTopPad;
  const int centerX = (renderer.getScreenWidth() - kFiveCoverCenterW) / 2 + kCenterXOffset;
  renderer.drawRoundedRect(centerX, centerTileY+12, kFiveCoverCenterW, kFiveCoverCenterH, kSelectionLineW, kCornerRadius, true);
}

void LyraMarcoand75Theme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                         const std::function<std::string(int index)>& buttonLabel,
                                         const std::function<UIIcon(int index)>& rowIcon,
                                         const std::function<std::string(int index)>& buttonSubtitle,
                                         const std::function<bool(int index)>& showAccessory) const {
  (void)buttonLabel; (void)buttonSubtitle; (void)showAccessory;
  if (buttonCount <= 0) return;
  constexpr int kIconSize = 32;
  constexpr int kIconPad32 = 8;
  constexpr int kHighlightPad32 = 8;
  const int visibleCount = std::min(buttonCount, kVisibleMenuSlots);
  const int safeSelectedIndex = (selectedIndex >= 0 && selectedIndex < buttonCount) ? selectedIndex : -1;
  const int maxWindowStart = std::max(0, buttonCount - visibleCount);
  int windowStart = 0;
  if (safeSelectedIndex >= 0) windowStart = std::clamp(safeSelectedIndex - visibleCount / 2, 0, maxWindowStart);
  const int screenW = renderer.getScreenWidth();
  const int tileH = kIconPad32 + kIconSize + kIconPad32;
  const int tileW = screenW / visibleCount;
  const int rowY = renderer.getScreenHeight() - LyraMarcoand75Metrics::values.buttonHintsHeight - tileH - 8;
  const int panelX = rect.x + 8;
  const int panelW = rect.width - 16;
  constexpr int kIconPanelPadCyber = 4;
  const int panelIconY = rowY - kIconPanelPadCyber;
  const int panelIconH = tileH + 2 * kIconPanelPadCyber;
  renderer.fillRect(panelX + 5, panelIconY + 5, panelW - 10, panelIconH - 10, false);
  drawCyberPanel(renderer, panelX, panelIconY, panelW, panelIconH, false);
  for (int slot = 0; slot < visibleCount; ++slot) {
    const int i = windowStart + slot;
    const int tileX = slot * tileW;
    const int iconX = tileX + (tileW - kIconSize) / 2;
    const int iconY = rowY + kIconPad32;
    if (safeSelectedIndex == i) {
      const int highlightSize = kIconSize + 2 * kHighlightPad32;
      const int highlightY = rowY + (tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad32, highlightY, highlightSize, highlightSize, kCornerRadius, Color::Black);
    }
    if (rowIcon != nullptr) {
      const uint8_t* bmp = iconForName(rowIcon(i));
      if (bmp != nullptr) {
        if (safeSelectedIndex == i) {
          if (renderer.isDarkMode()) {
            renderer.drawIconBlack(bmp, iconX, iconY, kIconSize, kIconSize);
          } else {
            renderer.drawIconInverted(bmp, iconX, iconY, kIconSize, kIconSize);
          }
        } else {
          renderer.drawIcon(bmp, iconX, iconY, kIconSize, kIconSize);
        }
      }
    }
  }
  if (buttonCount > visibleCount) {
    const int midY = rowY + tileH / 2;
    if (windowStart > 0) {
      renderer.drawLine(10, midY, 20, midY - 9, 2, true);
      renderer.drawLine(10, midY, 20, midY + 9, 2, true);
    }
    if (windowStart + visibleCount < buttonCount) {
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY - 9, 2, true);
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY + 9, 2, true);
    }
  }
}

void LyraMarcoand75Theme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                   const std::function<std::string(int index)>& rowTitle,
                                   const std::function<std::string(int index)>& rowSubtitle,
                                   const std::function<UIIcon(int index)>& rowIcon,
                                   const std::function<std::string(int index)>& rowValue,
                                   bool highlightValue, const std::function<bool(int index)>& rowCompleted) const {
  LyraTheme::drawList(renderer, rect, itemCount, selectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue, highlightValue, rowCompleted);
}

void LyraMarcoand75Theme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, bool selected) const {
  LyraTheme::drawTabBar(renderer, rect, tabs, selected);
}