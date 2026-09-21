#include "CollectionBooksActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "StoreManager.h"
#include "components/UITheme.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "MappedInputManager.h"

static void s_onBack(void* ctx) {
  static_cast<CollectionBooksActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<CollectionBooksActivity*>(ctx);
  if (self->books_.empty()) return;
  // Actual book open would be triggered here in a full implementation
  LOG_DBG("COLL", "Open book from collection %s", self->collectionId_.c_str());
  self->finish();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<CollectionBooksActivity*>(ctx);
  if (self->books_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, static_cast<int>(self->books_.size()));
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, static_cast<int>(self->books_.size()));
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<CollectionBooksActivity*>(ctx);
  if (self->books_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextPageIndex(self->selectedIndex_, static_cast<int>(self->books_.size()), self->pageItems_);
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousPageIndex(self->selectedIndex_, static_cast<int>(self->books_.size()), self->pageItems_);
  }
  self->requestUpdate();
}

CollectionBooksActivity::CollectionBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string collectionId, std::string collectionName)
    : Activity("CollectionBooks", renderer, mappedInput),
      collectionId_(std::move(collectionId)),
      collectionName_(std::move(collectionName)) {}

void CollectionBooksActivity::onEnter() {
  Activity::onEnter();
  pageItems_ = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  listInputMapper_.setBackHandler(s_onBack, this, false);
  listInputMapper_.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper_.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);
  refreshBooks();
}

void CollectionBooksActivity::onExit() {
  Activity::onExit();
}

void CollectionBooksActivity::loop() {
  listInputMapper_.loop(mappedInput);
}

void CollectionBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  char title[128];
  std::snprintf(title, sizeof(title), "%s (%d)", collectionName_.c_str(), static_cast<int>(books_.size()));
  ListRenderHelper::drawHeader(renderer, title);

  if (books_.empty()) {
    GUI.drawPopup(renderer, tr(STR_COLLECTION_EMPTY));
    renderer.displayBuffer();
    return;
  }

  const auto layout = ListLayout::compute(renderer);
  ListRenderHelper::drawList(renderer, layout, static_cast<int>(books_.size()), selectedIndex_,
                             [this](int index) { return books_[index].title; }, nullptr, nullptr, nullptr,
                             true);
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void CollectionBooksActivity::refreshBooks() {
  books_.clear();
  selectedIndex_ = 0;
  USER_COLLECTIONS.ensureLoaded();
  auto members = USER_COLLECTIONS.members(collectionId_);
  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
    return a.position < b.position;
  });
  for (const auto& m : members) {
    LibraryIndex::BookRef ref;
    if (LibraryIndex::queryUserCollectionBooks(&ref, 0, 1, collectionId_.c_str()) == 1) {
      books_.push_back(ref);
    }
  }
}
