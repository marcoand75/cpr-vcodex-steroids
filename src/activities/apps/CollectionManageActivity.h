#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"

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

  std::vector<CollectionEntry> collections_;
  int selectedIndex_ = 0;
  int pageItems_ = 0;
  ListInputMapper listInputMapper_;

  void refreshCollections();
  void createCollection();
  void deleteSelected();
  void startRename(int index);
};
