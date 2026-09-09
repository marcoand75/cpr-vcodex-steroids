#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "components/LibraryIndex.h"

/**
 * BatchCoverGenerationActivity
 *
 * Iterates through all books in the library and generates missing cover
 * thumbnails in batch. Shows progress as "x/y" and allows cancellation
 * with the Back button.
 */
class BatchCoverGenerationActivity final : public Activity {
  struct BookTask {
    std::string path;
    std::string title;
  };

  std::vector<BookTask> missingBooks_;
  int doneCount_ = 0;
  int totalCount_ = 0;
  int currentIndex_ = 0;
  bool running_ = false;
  bool finished_ = false;
  bool scanning_ = false;

  int coverWidth_ = 100;
  int coverHeight_ = 150;

  void scanMissingCovers();
  bool generateCoverForBook(const std::string& path);

  // Incremental scan state
  int scanNextPage_ = 0;
  int scanScannedCount_ = 0;

  // Power management during long-running generation
  bool powerLocked_ = false;
  void lockPowerSaving();
  void unlockPowerSaving();

 public:
  explicit BatchCoverGenerationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BatchCoverGeneration", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
