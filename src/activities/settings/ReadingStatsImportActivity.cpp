#include "ReadingStatsImportActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
constexpr char READING_STATS_EXPORT_DIR[] = "/exports";
constexpr char READING_STATS_EXPORTED_FILE[] = "stats_exported";
constexpr char READING_STATS_EXPORTED_PATH[] = "/exports/stats_exported";
constexpr char READING_STATS_BACKUP_PREFIX[] = "stats_backup_";

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool isReadingStatsBackupName(const char* name) {
  if (!name || std::strncmp(name, READING_STATS_BACKUP_PREFIX, std::strlen(READING_STATS_BACKUP_PREFIX)) != 0) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int consumed = 0;
  if (std::sscanf(name, "stats_backup_%4d-%2u-%2u%n", &year, &month, &day, &consumed) != 3 || name[consumed] != '\0') {
    return false;
  }

  return year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
}  // namespace

static void s_onBack(void* ctx) {
  auto* self = static_cast<ReadingStatsImportActivity*>(ctx);
  ActivityResult result;
  result.isCancelled = true;
  self->setResult(std::move(result));
  self->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<ReadingStatsImportActivity*>(ctx);
  self->finishWithSelection();
}

static void s_onNavRelease(void* ctx, int delta) {
  auto* self = static_cast<ReadingStatsImportActivity*>(ctx);
  const int itemCount = static_cast<int>(self->importPaths.size());
  if (delta > 0) {
    self->selectedIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(self->selectedIndex), itemCount));
  } else if (delta < 0) {
    self->selectedIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(self->selectedIndex), itemCount));
  }
  self->requestUpdate();
}

static void s_onNavContinuous(void* ctx, int delta) {
  auto* self = static_cast<ReadingStatsImportActivity*>(ctx);
  const int itemCount = static_cast<int>(self->importPaths.size());
  if (delta > 0) {
    self->selectedIndex = static_cast<size_t>(ButtonNavigator::nextPageIndex(static_cast<int>(self->selectedIndex), itemCount, self->pageItems));
  } else if (delta < 0) {
    self->selectedIndex = static_cast<size_t>(ButtonNavigator::previousPageIndex(static_cast<int>(self->selectedIndex), itemCount, self->pageItems));
  }
  self->requestUpdate();
}

std::vector<std::string> ReadingStatsImportActivity::getImportPaths() {
  std::vector<std::string> paths;
  if (Storage.exists(READING_STATS_EXPORTED_PATH)) {
    paths.emplace_back(READING_STATS_EXPORTED_PATH);
  }

  std::vector<std::string> backupPaths;
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (dir && dir.isDirectory()) {
    char name[256];
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (entry.isDirectory()) {
        entry.close();
        continue;
      }

      entry.getName(name, sizeof(name));
      entry.close();
      if (std::strcmp(name, READING_STATS_EXPORTED_FILE) == 0) {
        continue;
      }
      if (isReadingStatsBackupName(name)) {
        backupPaths.emplace_back(std::string(READING_STATS_EXPORT_DIR) + "/" + name);
      }
    }
  }
  if (dir) {
    dir.close();
  }

  std::sort(backupPaths.begin(), backupPaths.end(), [](const std::string& left, const std::string& right) {
    return fileNameFromPath(left) > fileNameFromPath(right);
  });
  paths.insert(paths.end(), backupPaths.begin(), backupPaths.end());
  return paths;
}

void ReadingStatsImportActivity::onEnter() {
  Activity::onEnter();
  importPaths = getImportPaths();
  selectedIndex = 0;

  pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavReleaseAndContinuous(s_onNavRelease, s_onNavContinuous, this);

  requestUpdate();
}

std::string ReadingStatsImportActivity::getDisplayName(const int index) const {
  if (index < 0 || index >= static_cast<int>(importPaths.size())) {
    return "";
  }
  return fileNameFromPath(importPaths[static_cast<size_t>(index)]);
}

void ReadingStatsImportActivity::finishWithSelection() {
  if (importPaths.empty()) {
    return;
  }

  setResult(ActivityResult{FilePathResult{importPaths[selectedIndex]}});
  finish();
}

void ReadingStatsImportActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void ReadingStatsImportActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);
  const bool isEmpty = importPaths.empty();

  ListRenderHelper::drawHeader(renderer, tr(STR_IMPORT_READING_STATS), READING_STATS_EXPORT_DIR, true);

  if (isEmpty) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.contentTop + 20, tr(STR_NO_READING_STATS_EXPORT));
  } else {
    ListRenderHelper::drawList(renderer, layout, static_cast<int>(importPaths.size()), static_cast<int>(selectedIndex),
                               [this](int index) { return getDisplayName(index); }, nullptr,
                               [](int) { return UIIcon::File; });
  }

  const char* btn2 = isEmpty ? "" : tr(STR_SELECT);
  const char* btn3 = isEmpty ? "" : tr(STR_DIR_UP);
  const char* btn4 = isEmpty ? "" : tr(STR_DIR_DOWN);
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), btn2, btn3, btn4);
  renderer.displayBuffer();
}
