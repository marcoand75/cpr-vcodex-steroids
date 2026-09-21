#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"
#include "components/LibraryIndex.h"
#include "CollectionPickerActivity.h"

class CollectionManageActivity final : public Activity {
 public:
  explicit CollectionManageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void onBack(void* ctx);
  void onConfirm(void* ctx);
  void onNavRelease(void* ctx, int delta);
  void onNavContinuous(void* ctx, int delta);

  struct CollectionEntry {
    std::string id;
    std::string name;
    int bookCount;
  };

  enum class ViewMode { Collections, Members };

  std::vector<CollectionEntry> collections_;
  int selectedIndex_ = 0;
  int pageItems_ = 0;
  ListInputMapper listInputMapper_;

  ViewMode viewMode_ = ViewMode::Collections;
  std::string currentCollectionId_;
  std::string currentCollectionName_;
  std::vector<LibraryIndex::BookRef> members_;
  int selectedMemberIndex_ = 0;

  void refreshCollections();
  void refreshMembers();
  void createCollection();
  void deleteSelected();
  void startRename(int index);
  void renameCurrentCollection();
  void openMembers(const std::string& collectionId, const std::string& collectionName);
  void addBookToCurrentCollection();
  void removeSelectedMember();
};
