#include "BatchCoverGenerationActivity.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include "../ActivityManager.h"
#include "HalStorage.h"
#include "CrossPointSettings.h"
#include "Logging.h"
#include "components/LibraryIndex.h"
#include "components/LibraryCache.h"
#include "components/UITheme.h"
#include "Epub.h"
#include "Xtc.h"
#include "FsHelpers.h"
#include "fontIds.h"
#include "I18n.h"
#include "../util/ListRenderHelper.h"

static const char* TAG = "BATCH_COV";

void BatchCoverGenerationActivity::scanMissingCovers() {
  missingBooks_.clear();
  doneCount_ = 0;
  totalCount_ = 0;
  currentIndex_ = 0;
  running_ = false;
  finished_ = false;

  // Use the current library layout to determine cover size.
  const uint8_t layout = SETTINGS.libraryLayout;
  if (layout == CrossPointSettings::LIBRARY_LAYOUT_4X4) {
    coverWidth_ = 100;
    coverHeight_ = 150;
  } else if (layout == CrossPointSettings::LIBRARY_LAYOUT_3X3) {
    coverWidth_ = 120;
    coverHeight_ = 180;
  } else {
    coverWidth_ = 140;
    coverHeight_ = 210;
  }

  const int total = LibraryIndex::totalBooks();
  if (total <= 0) {
    LOG_INF(TAG, "No books in library");
    return;
  }

  constexpr int kPageSize = 16;
  LibraryIndex::BookRef page[kPageSize];

  for (int pageStart = 0; pageStart < total; pageStart += kPageSize) {
    const int count = LibraryIndex::queryPage(page, pageStart / kPageSize, kPageSize,
                                               LibraryIndex::SortMode::TITLE_ASC);
    for (int i = 0; i < count; ++i) {
      if (page[i].path[0] == '\0') continue;
      const std::string thumbPath = LibraryIndex::thumbPathFor(std::string(page[i].path), coverWidth_, coverHeight_);
      if (thumbPath.empty()) continue;
      if (Storage.exists(thumbPath.c_str())) continue;

      missingBooks_.push_back({page[i].path, page[i].title});
    }
  }

  totalCount_ = static_cast<int>(missingBooks_.size());
  LOG_INF(TAG, "Found %d missing covers out of %d books", totalCount_, total);
}

bool BatchCoverGenerationActivity::generateCoverForBook(const std::string& path) {
  const std::string thumbPath = LibraryIndex::thumbPathFor(path, coverWidth_, coverHeight_);
  if (thumbPath.empty()) return false;

  // Ensure cache directory exists.
  const size_t slash = thumbPath.find_last_of('/');
  if (slash != std::string::npos) {
    const std::string dir = thumbPath.substr(0, slash);
    if (!Storage.exists(dir.c_str())) {
      Storage.mkdir(dir.c_str());
    }
  }

  if (FsHelpers::hasEpubExtension(path)) {
    if (ESP.getMaxAllocHeap() < 32 * 1024) {
      LOG_DBG(TAG, "Cover SKIP low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    Epub epub(path, "/.crosspoint");
    if (!epub.load(true, true)) {
      LOG_DBG(TAG, "Cover SKIP EPUB load fail: %s", path.c_str());
      return false;
    }
    if (ESP.getMaxAllocHeap() < 28 * 1024) {
      LOG_DBG(TAG, "Cover SKIP post-load low heap maxA=%u", ESP.getMaxAllocHeap());
      return false;
    }
    return epub.generateAdaptiveThumbBmp(coverWidth_, coverHeight_);
  }

  if (FsHelpers::hasXtcExtension(path)) {
    if (ESP.getFreeHeap() < 20000) return false;
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) return false;
    return xtc.generateThumbBmp(coverWidth_, coverHeight_);
  }

  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    if (ESP.getMaxAllocHeap() < 24 * 1024 || ESP.getFreeHeap() < 28 * 1024) return false;
    return false;  // text fallback not implemented in batch mode
  }

  return false;
}

void BatchCoverGenerationActivity::onEnter() {
  Activity::onEnter();
  scanMissingCovers();
  if (totalCount_ > 0) {
    running_ = true;
  } else {
    finished_ = true;
  }
  requestUpdate();
}

void BatchCoverGenerationActivity::onExit() {
  running_ = false;
  finished_ = false;
  Activity::onExit();
}

void BatchCoverGenerationActivity::loop() {
  if (!running_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      onGoHome();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    running_ = false;
    requestUpdate();
    return;
  }

  if (currentIndex_ < totalCount_) {
    const bool ok = generateCoverForBook(missingBooks_[currentIndex_].path);
    if (ok) {
      ++doneCount_;
    }
    ++currentIndex_;
    requestUpdate();
  } else {
    running_ = false;
    finished_ = true;
    requestUpdate();
  }
}

void BatchCoverGenerationActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pw = renderer.getScreenWidth();
  const int ph = renderer.getScreenHeight();

  // Header
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lh12 = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 20,
                            tr(STR_BATCH_GENERATE_COVERS), true, EpdFontFamily::BOLD);

  if (finished_) {
    char msg[64];
    snprintf(msg, sizeof(msg), tr(STR_BATCH_GENERATE_COVERS_DONE), doneCount_, totalCount_);
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + 60, msg, true);
  } else if (running_) {
    char msg[64];
    snprintf(msg, sizeof(msg), tr(STR_BATCH_GENERATE_COVERS_PROGRESS), currentIndex_ + 1, totalCount_);
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + 60, msg, true);

    if (currentIndex_ < totalCount_) {
      const std::string& title = missingBooks_[currentIndex_].title;
      const std::string truncated = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), pw - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + 80, truncated.c_str(), true);
    }
  } else if (totalCount_ == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + 60, "No missing covers", true);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + 60, "Press Back to cancel", true);
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), finished_ ? tr(STR_CONFIRM) : "", "", "");
  renderer.displayBuffer();
}
