#include "CollectionPickerActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "StoreManager.h"
#include "components/LibraryIndex.h"
#include "components/UITheme.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "../util/KeyboardEntryActivity.h"
#include "MappedInputManager.h"

static void s_onBack(void* ctx) {
  static_cast<CollectionPickerActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<CollectionPickerActivity*>(ctx);
  if (self->collections_.empty()) return;
  const int idx = self->selectedIndex_;
  if (idx < 0 || idx >= self->collections_.size()) return;
  USER_COLLECTIONS.ensureLoaded();
  USER_COLLECTIONS.addBook(self->collections_[idx].id, self->bookId_, 0.0f);
  self->finish();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<CollectionPickerActivity*>(ctx);
  if (self->collections_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<CollectionPickerActivity*>(ctx);
  if (self->collections_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
  }
  self->requestUpdate();
}

CollectionPickerActivity::CollectionPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   uint32_t bookId)
    : Activity("CollectionPicker", renderer, mappedInput), bookId_(bookId) {}

void CollectionPickerActivity::onEnter() {
  Activity::onEnter();
  pageItems_ = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  listInputMapper_.setBackHandler(s_onBack, this, false);
  listInputMapper_.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper_.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);
  refreshCollections();
}

void CollectionPickerActivity::onExit() {
  Activity::onExit();
}

void CollectionPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    createCollection();
    return;
  }
  listInputMapper_.loop(mappedInput);
}

void CollectionPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  ListRenderHelper::drawHeader(renderer, tr(STR_COLLECTION_PICK));

  if (collections_.empty()) {
    GUI.drawPopup(renderer, tr(STR_COLLECTION_EMPTY));
    ListRenderHelper::drawHints(renderer, mappedInput,
                                tr(STR_BACK),
                                nullptr,
                                tr(STR_COLLECTION_CREATE),
                                nullptr);
    renderer.displayBuffer();
    return;
  }

  const auto layout = ListLayout::compute(renderer);
  ListRenderHelper::drawList(renderer, layout, static_cast<int>(collections_.size()), selectedIndex_,
                             [this](int index) { return collections_[index].name; }, nullptr, nullptr,
                             [this](int index) -> std::string {
                               return collections_[index].hasBook ? tr(STR_COLLECTION_ADD_BOOK) : std::string();
                             },
                             true);
  ListRenderHelper::drawHints(renderer, mappedInput,
                              tr(STR_BACK),
                              tr(STR_COLLECTION_ADD_BOOK),
                              tr(STR_COLLECTION_CREATE),
                              nullptr);
  renderer.displayBuffer();
}

void CollectionPickerActivity::createCollection() {
  char id[16] = {};
  if (LibraryIndex::createUserCollection("New Collection", id, sizeof(id))) {
    refreshCollections();
  }
}

void CollectionPickerActivity::refreshCollections() {
  USER_COLLECTIONS.ensureLoaded();
  collections_.clear();
  for (const auto& c : USER_COLLECTIONS.collections()) {
    CollectionEntry entry;
    entry.id = c.id;
    entry.name = c.name;
    entry.hasBook = USER_COLLECTIONS.hasBook(c.id, bookId_);
    collections_.push_back(entry);
  }
  if (selectedIndex_ >= collections_.size()) selectedIndex_ = collections_.size() - 1;
  requestUpdate();
}
