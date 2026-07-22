#include "ClippingsAppActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "FavoritesStore.h"
#include "RecentBooksStore.h"
#include "../reader/ClippingsActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/BookIdentity.h"

namespace {
constexpr unsigned long DELETE_CLIPPINGS_HOLD_MS = 1000;

struct ClippingBookCandidate {
  std::string bookId;
  std::string path;
  std::string title;
  std::string author;
};

std::string getDisplayTitle(const std::string& title, const std::string& path) {
  if (!title.empty()) return title;
  const auto slashPos = path.find_last_of('/');
  if (slashPos == std::string::npos || slashPos + 1 >= path.size()) return path;
  return path.substr(slashPos + 1);
}

bool shouldReplaceBookId(const std::string& current, const std::string& candidate) {
  if (candidate.empty()) return false;
  return current.empty() || (BookIdentity::isLegacyBookId(current) && !BookIdentity::isLegacyBookId(candidate));
}

void addCandidate(std::vector<ClippingBookCandidate>& candidates, ClippingBookCandidate candidate) {
  candidate.path = BookIdentity::normalizePath(candidate.path);
  if (candidate.path.empty() || !FsHelpers::hasEpubExtension(candidate.path) || !Storage.exists(candidate.path.c_str()))
    return;
  auto it = std::find_if(candidates.begin(), candidates.end(), [&candidate](const ClippingBookCandidate& existing) {
    if (!candidate.bookId.empty() && !existing.bookId.empty() && candidate.bookId == existing.bookId) return true;
    return existing.path == candidate.path;
  });
  if (it == candidates.end()) {
    candidates.push_back(std::move(candidate));
    return;
  }
  if (shouldReplaceBookId(it->bookId, candidate.bookId)) it->bookId = std::move(candidate.bookId);
  if (it->title.empty() && !candidate.title.empty()) it->title = std::move(candidate.title);
  if (it->author.empty() && !candidate.author.empty()) it->author = std::move(candidate.author);
}

bool loadClippingsForBook(const std::string& path, ClippingStore& store) {
  store.load(path);
  return !store.isEmpty();
}
}  // namespace

void ClippingsAppActivity::refreshEntries() {
  entries.clear();
  std::vector<ClippingBookCandidate> candidates;
  for (const auto& book : READING_STATS.getBooks())
    addCandidate(candidates, ClippingBookCandidate{book.bookId, book.path, book.title, book.author});
  for (const auto& book : RECENT_BOOKS.getBooks())
    addCandidate(candidates, ClippingBookCandidate{book.bookId, book.path, book.title, book.author});
  for (const auto& book : FAVORITES.getBooks())
    addCandidate(candidates, ClippingBookCandidate{book.bookId, book.path, book.title, book.author});

  for (const auto& candidate : candidates) {
    ClippingStore store;
    if (!loadClippingsForBook(candidate.path, store)) continue;
    entries.push_back(BookEntry{
        .bookId = candidate.bookId,
        .path = candidate.path,
        .title = getDisplayTitle(candidate.title, candidate.path),
        .author = candidate.author,
        .clippings = store.getAll(),
    });
  }
  if (selectedIndex >= static_cast<int>(entries.size()))
    selectedIndex = std::max(0, static_cast<int>(entries.size()) - 1);
}

void ClippingsAppActivity::openSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) return;
  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<ClippingsActivity>(
          renderer, mappedInput, entry.clippings,
          [path = entry.path](size_t index) {
            ClippingStore store;
            store.load(path);
            if (index >= store.getAll().size()) return false;
            const bool removed = store.remove(index);
            if (removed) store.save();
            return removed;
          }),
      [this, path = entry.path](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& bmResult = std::get<BookmarkResult>(result.data);
          activityManager.goToEpubBookmark(path, bmResult.spineIndex, bmResult.page);
          return;
        }
        refreshEntries();
        requestUpdate();
      });
}

bool ClippingsAppActivity::clearClippingsForBook(const std::string& bookId, const std::string& bookPath) const {
  ClippingStore store;
  store.load(bookPath);
  if (store.isEmpty()) return true;
  store.clear();
  store.save();
  return true;
}

void ClippingsAppActivity::confirmDeleteSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) return;
  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ALL_CLIPPINGS), entry.title),
      [this, bookId = entry.bookId, path = entry.path](const ActivityResult& result) {
        if (!result.isCancelled) {
          clearClippingsForBook(bookId, path);
          refreshEntries();
        }
        requestUpdate();
      });
}

void ClippingsAppActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  requestUpdate();
}

void ClippingsAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); return; }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_CLIPPINGS_HOLD_MS) { confirmDeleteSelectedBook(); return; }
    openSelectedBook();
    return;
  }
  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) return;
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) return;
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void ClippingsAppActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_CLIPPINGS), tr(STR_CLIPPINGS_APP_DESC));
  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_CLIPPINGS));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return entries[index].title; },
                 [this](const int index) { return entries[index].author.empty() ? entries[index].path : entries[index].author; },
                 [](const int) { return UIIcon::Book; },
                 [this](const int index) { return std::to_string(entries[index].clippings.size()); });
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
