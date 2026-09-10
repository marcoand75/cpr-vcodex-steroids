#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../util/ListInputMapper.h"

class CollectionPickerActivity final : public Activity {
 public:
  explicit CollectionPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    uint32_t bookId = 0);
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
    bool hasBook;
  };

  std::vector<CollectionEntry> collections_;
  int selectedIndex_ = 0;
  int pageItems_ = 0;
  ListInputMapper listInputMapper_;
  uint32_t bookId_ = 0;

 private:
  void refreshCollections();
  void createCollection();
};
