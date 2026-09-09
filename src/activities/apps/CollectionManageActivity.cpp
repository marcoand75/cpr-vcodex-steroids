#include "CollectionManageActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "StoreManager.h"
#include "components/LibraryIndex.h"
#include "components/UITheme.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "MappedInputManager.h"

static void s_onBack(void* ctx) {
  static_cast<CollectionManageActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->collections_.empty()) return;
  const int idx = self->selectedIndex_;
  if (idx < 0 || idx >= self->collections_.size()) return;
  self->renameSelected();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->collections_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->collections_.empty()) return;
  if (delta > 0) {
    self->selectedIndex_ = ButtonNavigator::nextPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
  } else if (delta < 0) {
    self->selectedIndex_ = ButtonNavigator::previousPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
  }
  self->requestUpdate();
}

CollectionManageActivity::CollectionManageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("CollectionManage", renderer, mappedInput) {}

void CollectionManageActivity::onEnter() {
  Activity::onEnter();
  pageItems_ = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  listInputMapper_.setBackHandler(s_onBack, this, false);
  listInputMapper_.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper_.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);
  refreshCollections();
}

void CollectionManageActivity::onExit() {
  Activity::onExit();
}

void CollectionManageActivity::loop() {
  listInputMapper_.loop(mappedInput);
}

void CollectionManageActivity::render(RenderLock&&) {
  renderer.clearScreen();
  ListRenderHelper::drawHeader(renderer, tr(STR_COLLECTIONS_MANAGE));

  if (collections_.empty()) {
    GUI.drawPopup(renderer, tr(STR_COLLECTION_EMPTY));
    renderer.displayBuffer();
    return;
  }

  const auto layout = ListLayout::compute(renderer);
  ListRenderHelper::drawList(renderer, layout, static_cast<int>(collections_.size()), selectedIndex_,
                             [this](int index) { return collections_[index].name; }, nullptr, nullptr,
                             [this](int index) -> std::string {
                               char buf[32];
                               std::snprintf(buf, sizeof(buf), "%d books", collections_[index].bookCount);
                               return buf;
                             },
                             true);
  ListRenderHelper::drawStandardHints(renderer, mappedInput);
  renderer.displayBuffer();
}

void CollectionManageActivity::refreshCollections() {
  USER_COLLECTIONS.ensureLoaded();
  collections_.clear();
  for (const auto& c : USER_COLLECTIONS.collections()) {
    CollectionEntry entry;
    entry.id = c.id;
    entry.name = c.name;
    entry.bookCount = USER_COLLECTIONS.memberCount(c.id);
    collections_.push_back(entry);
  }
  if (selectedIndex_ >= collections_.size()) selectedIndex_ = collections_.size() - 1;
  requestUpdate();
}

void CollectionManageActivity::createCollection() {
  char id[16] = {};
  if (LibraryIndex::createUserCollection("New Collection", id, sizeof(id))) {
    refreshCollections();
  }
}

void CollectionManageActivity::deleteSelected() {
  if (collections_.empty()) return;
  const int idx = selectedIndex_;
  if (idx < 0 || idx >= collections_.size()) return;
  USER_COLLECTIONS.ensureLoaded();
  USER_COLLECTIONS.deleteCollection(collections_[idx].id);
  refreshCollections();
}

void CollectionManageActivity::renameSelected() {
  if (collections_.empty()) return;
  const int idx = selectedIndex_;
  if (idx < 0 || idx >= collections_.size()) return;
  // Full rename requires KeyboardActivity; placeholder for now
  LOG_DBG("COLL", "Rename requested for collection %s", collections_[idx].id.c_str());
}
