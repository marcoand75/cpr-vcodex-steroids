#include "ReadingStatsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr unsigned long BOOK_LONG_PRESS_MS = 1000;
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int DETAILS_BUTTON_HEIGHT = 58;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;
constexpr int BOOK_ROW_HEIGHT = 80;
constexpr int BOOK_ROW_GAP = 10;
// constexpr int BOOKS_PER_PAGE = 3; // Removed, now using viewport-based calculation

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawMoreDetailsButton(GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const char* label = tr(STR_MORE_DETAILS);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2 + 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
}

void drawMiniProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t percent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 4);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4));
  }
}

void drawBookRow(GfxRenderer& renderer, const Rect& rect, const ReadingBookStats& book, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  } else {
    renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
  }

  const int sidePadding = 12;
  const int topPadding = 9;
  const int metaWidth = 88;
  const int innerX = rect.x + sidePadding;
  const int innerY = rect.y + topPadding;
  const int textWidth = rect.width - sidePadding * 2 - metaWidth;
  const int titleY = innerY;
  const int subtitleY = innerY + 26;
  const int progressBarY = rect.y + rect.height - 14;

  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, getBookTitle(book).c_str(), textWidth - 4, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, innerX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(book).c_str(), textWidth - 4, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, innerX, subtitleY, subtitle.c_str());

  const std::string progressText = std::to_string(book.lastProgressPercent) + "%";
  const std::string totalTimeText = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
  const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int timeWidth = renderer.getTextWidth(UI_10_FONT_ID, totalTimeText.c_str());
  const int progressX = rect.x + rect.width - sidePadding - progressWidth;
  const int timeX = rect.x + rect.width - sidePadding - timeWidth;

  renderer.drawText(UI_12_FONT_ID, progressX, titleY, progressText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, timeX, subtitleY, totalTimeText.c_str());

  drawMiniProgressBar(renderer, Rect{innerX, progressBarY, rect.width - sidePadding * 2, 9}, book.lastProgressPercent);
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  READING_STATS.ensureLoaded();
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  selectedIndex = READING_STATS.getBooks().empty() ? 0 : 1;

  if (!selectedBookPath.empty()) {
    const auto& books = READING_STATS.getBooks();
    for (int i = 0; i < static_cast<int>(books.size()); ++i) {
      if (books[i].path == selectedBookPath) {
        selectedIndex = i + 1;
        break;
      }
    }
  }

  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  requestUpdate();
  createDueAutoBackupWithFeedback();
}

void ReadingStatsActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

void ReadingStatsActivity::freeBackgroundMemory() {
  selectedBookPath.clear();
  LOG_DBG("ACT", "ReadingStats freeBackgroundMemory: selectedBookPath cleared");
}

void ReadingStatsActivity::loop() {
    const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
    const int selectableCount = bookCount + 1; // 0 for details, 1+ for books

    if (waitForBackRelease) {
        if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
            !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            waitForBackRelease = false;
        }
        return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    if (waitForConfirmRelease) {
        if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
            waitForConfirmRelease = false;
        }
        return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (selectedIndex > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
            confirmRemoveSelectedBook();
            return;
        }

        openSelectedEntry();
        return;
    }

    // Calculate visible book count for continuous scrolling (based on current viewport)
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int detailsTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
    const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
    const int listTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;
    const int listBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int listHeight = listBottom - listTop;

    int visibleBookCount = 0;
    if (listHeight > 0 && bookCount > 0) {
        int currentHeight = 0;
        for (int i = 0; i < bookCount; ++i) {
            if (currentHeight + BOOK_ROW_HEIGHT > listHeight) {
                break;
            }
            currentHeight += BOOK_ROW_HEIGHT;
            visibleBookCount++;
            if (i < bookCount - 1) {
                currentHeight += BOOK_ROW_GAP;
            }
        }
        if (visibleBookCount == 0) {
            visibleBookCount = 1;
        }
    } else {
        visibleBookCount = 1; // fallback
    }

    buttonNavigator.onNextRelease([this, selectableCount] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, selectableCount);
        requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this, selectableCount] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, selectableCount);
        requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, selectableCount, visibleBookCount, bookCount] {
        if (selectableCount <= 1) {
            return;
        }

        // If we are on the details button (selectedIndex == 0), move to the first book
        if (selectedIndex == 0) {
            selectedIndex = 1;
        } else {
            // We are in the book list, move by visibleBookCount books (page down)
            const int bookIndex = selectedIndex - 1;
            const int nextBookIndex = bookIndex + visibleBookCount;
            if (nextBookIndex >= bookCount) {
                // Clamp to last book
                selectedIndex = bookCount;
            } else {
                selectedIndex = nextBookIndex + 1; // because selectedIndex = bookIndex + 1
            }
        }
        requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, selectableCount, visibleBookCount, bookCount] {
        if (selectableCount <= 1) {
            return;
        }

        // If we are on the first book (selectedIndex == 1), move to the details button
        if (selectedIndex == 1) {
            selectedIndex = 0;
        } else {
            // We are in the book list, move by visibleBookCount books (page up)
            const int bookIndex = selectedIndex - 1;
            int prevBookIndex = bookIndex - visibleBookCount;
            if (prevBookIndex < 0) {
                // Clamp to first book
                selectedIndex = 1;
            } else {
                selectedIndex = prevBookIndex + 1;
            }
        }
        requestUpdate();
    });
}

void ReadingStatsActivity::openSelectedEntry() {
  const auto& books = READING_STATS.getBooks();
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<ReadingStatsExtendedActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             guardBackReturn();
                             requestUpdate();
                           });
    return;
  }
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, books[bookIndex].path),
                         [this](const ActivityResult&) {
                           guardBackReturn();
                           requestUpdate();
                         });
}

void ReadingStatsActivity::confirmRemoveSelectedBook() {
  const auto& books = READING_STATS.getBooks();
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  const ReadingBookStats selectedBook = books[bookIndex];
  const int currentSelection = selectedIndex;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_STATS_ENTRY),
                                                                getBookTitle(selectedBook)),
                         [this, selectedBook, currentSelection](const ActivityResult& result) {
                           if (!result.isCancelled && READING_STATS.removeBook(selectedBook.path)) {
                             const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
                             if (bookCount == 0) {
                               selectedIndex = 0;
                             } else if (currentSelection > bookCount) {
                               selectedIndex = bookCount;
                             } else {
                               selectedIndex = currentSelection;
                             }
                           }

                           guardBackReturn();
                           requestUpdate(true);
                         });
}

void ReadingStatsActivity::guardBackReturn() { waitForBackRelease = true; }

void ReadingStatsActivity::createDueAutoBackupWithFeedback() {
  if (!READING_STATS.isAutoBackupDue()) {
    return;
  }

  PopupUtils::showTransientPopup(*this,tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
  const bool backupReady = READING_STATS.createDueAutoBackup();
  PopupUtils::showTransientPopup(*this,backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                     backupReady ? 100 : -1, backupReady ? 350 : 700);
  requestUpdate(true);
}

void ReadingStatsActivity::render(RenderLock&&) {
    renderer.clearScreen();

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int sidePadding = metrics.contentSidePadding;
    const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
    const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int detailsTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
    const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
    const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                      ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());

    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

    drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                  std::to_string(READING_STATS.getCurrentStreakDays()));
    drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                  tr(STR_MAX_STREAK), std::to_string(READING_STATS.getMaxStreakDays()));
    drawMetricCard(renderer,
                  Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth, SUMMARY_CARD_HEIGHT},
                  tr(STR_DAILY_GOAL), dailyGoalValue, todayReadingMs >= getDailyReadingGoalMs());
    drawMetricCard(renderer,
                  Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                       SUMMARY_CARD_HEIGHT},
                  tr(STR_READING_TIME), ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
    drawMetricCard(
        renderer, Rect{sidePadding, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2, cardWidth, SUMMARY_CARD_HEIGHT},
        tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));
    drawMetricCard(renderer,
                  Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2,
                       cardWidth, SUMMARY_CARD_HEIGHT},
                  tr(STR_BOOKS_STARTED), std::to_string(READING_STATS.getBooksStartedCount()));

    drawMoreDetailsButton(renderer, Rect{sidePadding, detailsTop, pageWidth - sidePadding * 2, DETAILS_BUTTON_HEIGHT},
                         selectedIndex == 0);

    const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
    const auto& books = READING_STATS.getBooks();
    const int bookCount = static_cast<int>(books.size());
    const std::string startedBooksLabel =
        std::string(tr(STR_STARTED_BOOKS)) + " (" + std::to_string(READING_STATS.getBooksStartedCount()) + ")";

    // Calculate book list viewport
    const int listTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;
    const int listBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int listHeight = listBottom - listTop;

    // Total height of the book list
    int totalBookListHeight = 0;
    if (bookCount > 0) {
        totalBookListHeight = bookCount * BOOK_ROW_HEIGHT + (bookCount - 1) * BOOK_ROW_GAP;
    }

    // Calculate how many books fit in the viewport
    int visibleBookCount = 0;
    if (listHeight > 0 && bookCount > 0) {
        int currentHeight = 0;
        for (int i = 0; i < bookCount; ++i) {
            if (currentHeight + BOOK_ROW_HEIGHT > listHeight) {
                break;
            }
            currentHeight += BOOK_ROW_HEIGHT;
            visibleBookCount++;
            if (i < bookCount - 1) {
                currentHeight += BOOK_ROW_GAP;
            }
        }
        if (visibleBookCount == 0) {
            visibleBookCount = 1;
        }
    } else {
        visibleBookCount = (bookCount > 0) ? 1 : 0;
    }

    // Determine the first visible book index based on the selected book
    int selectedBookIndex = selectedIndex - 1; // because selectedIndex 0 is the details button
    int firstVisibleIndex = 0;
    if (bookCount > 0 && selectedIndex > 0) { // we have a book selected
        // We want the selected book to be in the viewport.
        // We'll try to put it in the middle of the viewport.
        int desiredFirstVisibleIndex = selectedBookIndex - visibleBookCount / 2;
        if (desiredFirstVisibleIndex < 0) {
            desiredFirstVisibleIndex = 0;
        }
        if (desiredFirstVisibleIndex + visibleBookCount > bookCount) {
            desiredFirstVisibleIndex = bookCount - visibleBookCount;
        }
        firstVisibleIndex = desiredFirstVisibleIndex;
    }

    // Calculate the viewport offset in the list (for scrollbar)
    int viewportOffset = 0;
    if (firstVisibleIndex > 0) {
        viewportOffset = firstVisibleIndex * (BOOK_ROW_HEIGHT + BOOK_ROW_GAP);
    }

    // Draw the book list header (started books label and page indicator)
    int totalPages = 1;
    if (visibleBookCount > 0) {
        totalPages = (bookCount + visibleBookCount - 1) / visibleBookCount; // ceil division
    }
    int currentPage = 1;
    if (visibleBookCount > 0 && bookCount > 0) {
        currentPage = (firstVisibleIndex / visibleBookCount) + 1;
    }
    const std::string pageLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
    GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, startedBooksLabel.c_str(),
                     pageLabel.c_str());

    const int contentTop = listTop;

    if (books.empty()) {
        renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop + 20, tr(STR_NO_READING_STATS));
    } else {
        // Draw the visible books
        int currentY = contentTop;
        for (int i = firstVisibleIndex; i < firstVisibleIndex + visibleBookCount && i < bookCount; ++i) {
            const bool selected = (selectedIndex > 0 && (i == selectedBookIndex));
            const Rect bookRect{sidePadding, currentY, pageWidth - sidePadding * 2, BOOK_ROW_HEIGHT};
            drawBookRow(renderer, bookRect, books[i], selected);
            currentY += BOOK_ROW_HEIGHT;
            if (i < bookCount - 1) {
                currentY += BOOK_ROW_GAP;
            }
        }

        // Draw scrollbar if needed
        if (totalBookListHeight > listHeight) {
            constexpr int scrollBarWidth = 4;
            constexpr int scrollBarGap = 6;
            const int scrollTrackX = pageWidth - sidePadding;
            const int scrollBarHeight = std::max(18, (listHeight * listHeight) / totalBookListHeight);
            const int maxScrollOffset = std::max(1, totalBookListHeight - listHeight);
            const int scrollBarY =
                listTop + ((listHeight - scrollBarHeight) * std::min(viewportOffset, maxScrollOffset)) / maxScrollOffset;
            renderer.drawLine(scrollTrackX, listTop, scrollTrackX, listTop + listHeight, true);
            renderer.fillRect(scrollTrackX - scrollBarWidth + 1, scrollBarY, scrollBarWidth, scrollBarHeight, true);
        }
    }

    ListRenderHelper::drawStandardHints(renderer, mappedInput);
    renderer.displayBuffer();
}
