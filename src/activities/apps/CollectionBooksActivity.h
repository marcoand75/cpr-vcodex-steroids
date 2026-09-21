#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/LibraryIndex.h"
#include "activities/util/ListInputMapper.h"

class CollectionBooksActivity final : public Activity {
 public:
  CollectionBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string collectionId, std::string collectionName);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void onBack(void* ctx);
  void onConfirm(void* ctx);
  void onNavRelease(void* ctx, int delta);
  void onNavContinuous(void* ctx, int delta);

  std::string collectionId_;
  std::string collectionName_;
  std::vector<LibraryIndex::BookRef> books_;
  int selectedIndex_ = 0;
  int pageItems_ = 0;
  ListInputMapper listInputMapper_;

 private:
  void refreshBooks();
  int findBookIndex(uint32_t bookId) const;
};
