#include "CollectionManageActivity.h"

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
  static_cast<CollectionManageActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->viewMode_ == CollectionManageActivity::ViewMode::Collections) {
    if (self->collections_.empty()) return;
    const int idx = self->selectedIndex_;
    if (idx < 0 || idx >= self->collections_.size()) return;
    self->openMembers(self->collections_[idx].id, self->collections_[idx].name);
    return;
  }

  if (self->viewMode_ == CollectionManageActivity::ViewMode::Members) {
    if (self->members_.empty()) return;
    const int idx = self->selectedMemberIndex_;
    if (idx < 0 || idx >= self->members_.size()) return;
    LOG_DBG("COLL", "Open member %u from collection %s", self->members_[idx].id, self->currentCollectionId_.c_str());
  }
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->viewMode_ == CollectionManageActivity::ViewMode::Collections) {
    if (self->collections_.empty()) return;
    if (delta > 0) {
      self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
    } else if (delta < 0) {
      self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()));
    }
    self->requestUpdate();
    return;
  }

  if (self->viewMode_ == CollectionManageActivity::ViewMode::Members) {
    if (self->members_.empty()) return;
    if (delta > 0) {
      self->selectedMemberIndex_ = ButtonNavigator::nextIndex(self->selectedMemberIndex_, static_cast<int>(self->members_.size()));
    } else if (delta < 0) {
      self->selectedMemberIndex_ = ButtonNavigator::previousIndex(self->selectedMemberIndex_, static_cast<int>(self->members_.size()));
    }
    self->requestUpdate();
    return;
  }
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<CollectionManageActivity*>(ctx);
  if (self->viewMode_ == CollectionManageActivity::ViewMode::Collections) {
    if (self->collections_.empty()) return;
    if (delta > 0) {
      self->selectedIndex_ = ButtonNavigator::nextPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
    } else if (delta < 0) {
      self->selectedIndex_ = ButtonNavigator::previousPageIndex(self->selectedIndex_, static_cast<int>(self->collections_.size()), self->pageItems_);
    }
    self->requestUpdate();
    return;
  }

  if (self->viewMode_ == CollectionManageActivity::ViewMode::Members) {
    if (self->members_.empty()) return;
    if (delta > 0) {
      self->selectedMemberIndex_ = ButtonNavigator::nextPageIndex(self->selectedMemberIndex_, static_cast<int>(self->members_.size()), self->pageItems_);
    } else if (delta < 0) {
      self->selectedMemberIndex_ = ButtonNavigator::previousPageIndex(self->selectedMemberIndex_, static_cast<int>(self->members_.size()), self->pageItems_);
    }
    self->requestUpdate();
    return;
  }
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
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (viewMode_ == ViewMode::Members) {
      viewMode_ = ViewMode::Collections;
      currentCollectionId_.clear();
      members_.clear();
      selectedMemberIndex_ = 0;
      refreshCollections();
      return;
    }
    finish();
    return;
  }
  if (viewMode_ == ViewMode::Collections) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      createCollection();
      return;
    }
    if (!collections_.empty() && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      deleteSelected();
      return;
    }
  }
  if (viewMode_ == ViewMode::Members) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      addBookToCurrentCollection();
      return;
    }
    if (!members_.empty() && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      removeSelectedMember();
      return;
    }
  }
  listInputMapper_.loop(mappedInput);
}

void CollectionManageActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (viewMode_ == ViewMode::Collections) {
    ListRenderHelper::drawHeader(renderer, tr(STR_COLLECTIONS_MANAGE));

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
                                 char buf[32];
                                 std::snprintf(buf, sizeof(buf), "%d books", collections_[index].bookCount);
                                 return buf;
                               },
                               true);
    ListRenderHelper::drawHints(renderer, mappedInput,
                                tr(STR_BACK),
                                tr(STR_COLLECTION_RENAME),
                                tr(STR_COLLECTION_CREATE),
                                tr(STR_COLLECTION_DELETE));
    renderer.displayBuffer();
    return;
  }

  if (viewMode_ == ViewMode::Members) {
    char title[128];
    std::snprintf(title, sizeof(title), "%s (%d)", currentCollectionName_.c_str(), static_cast<int>(members_.size()));
    ListRenderHelper::drawHeader(renderer, title);

    if (members_.empty()) {
      GUI.drawPopup(renderer, tr(STR_COLLECTION_EMPTY));
      ListRenderHelper::drawHints(renderer, mappedInput,
                                  tr(STR_COLLECTION_BACK_TO_LIST),
                                  nullptr,
                                  tr(STR_COLLECTION_ADD_BOOK),
                                  nullptr);
      renderer.displayBuffer();
      return;
    }

    const auto layout = ListLayout::compute(renderer);
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(members_.size()), selectedMemberIndex_,
                               [this](int index) { return members_[index].title; }, nullptr, nullptr, nullptr,
                               true);
    ListRenderHelper::drawHints(renderer, mappedInput,
                                tr(STR_COLLECTION_BACK_TO_LIST),
                                tr(STR_COLLECTION_REMOVE_BOOK),
                                tr(STR_COLLECTION_ADD_BOOK),
                                nullptr);
    renderer.displayBuffer();
    return;
  }
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

void CollectionManageActivity::refreshMembers() {
  members_.clear();
  selectedMemberIndex_ = 0;
  if (currentCollectionId_.empty()) return;

  USER_COLLECTIONS.ensureLoaded();
  auto members = USER_COLLECTIONS.members(currentCollectionId_);
  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.position < b.position; });

  for (const auto& m : members) {
    LibraryIndex::BookRef ref;
    if (LibraryIndex::queryUserCollectionBooks(&ref, 0, 1, currentCollectionId_.c_str()) == 1) {
      members_.push_back(ref);
    }
  }
  requestUpdate();
}

void CollectionManageActivity::openMembers(const std::string& collectionId, const std::string& collectionName) {
  currentCollectionId_ = collectionId;
  currentCollectionName_ = collectionName;
  viewMode_ = ViewMode::Members;
  refreshMembers();
}

void CollectionManageActivity::startRename(int index) {
  if (index < 0 || index >= collections_.size()) return;
  const std::string currentName = collections_[index].name;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_COLLECTION_RENAME), currentName, 64),
      [this, index](const ActivityResult& result) {
        if (result.isCancelled) { requestUpdate(); return; }
        const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
        if (!kbResult || kbResult->text.empty()) { requestUpdate(); return; }
        USER_COLLECTIONS.ensureLoaded();
        USER_COLLECTIONS.renameCollection(collections_[index].id, kbResult->text);
        refreshCollections();
      });
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

void CollectionManageActivity::addBookToCurrentCollection() {
  if (currentCollectionId_.empty()) return;
  startActivityForResult(
      std::make_unique<CollectionPickerActivity>(renderer, mappedInput, 0),
      [this](const ActivityResult& result) {
        if (result.isCancelled) { requestUpdate(); return; }
        refreshMembers();
        requestUpdate();
      });
}

void CollectionManageActivity::removeSelectedMember() {
  if (members_.empty()) return;
  const int idx = selectedMemberIndex_;
  if (idx < 0 || idx >= members_.size()) return;
  USER_COLLECTIONS.ensureLoaded();
  USER_COLLECTIONS.removeBook(currentCollectionId_.c_str(), members_[idx].id);
  refreshMembers();
}
