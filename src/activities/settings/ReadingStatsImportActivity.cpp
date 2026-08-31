#include "ReadingStatsImportActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr char READING_STATS_EXPORT_DIR[] = "/exports";
constexpr char READING_STATS_EXPORTED_FILE[] = "stats_exported";
constexpr char READING_STATS_EXPORTED_PATH[] = "/exports/stats_exported";
constexpr char READING_STATS_BACKUP_PREFIX[] = "stats_backup_";
constexpr char READING_STATS_SYNCDATE_PREFIX[] = "stats_syncdate_";

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Files are named stats_backup_YYYY-MM-DD / stats_syncdate_YYYY-MM-DD with NO
// extension (content is JSON), so a ".json" suffix would not be listed here.
bool isValidDateBackupName(const char* prefix, const char* name) {
  if (!name || std::strncmp(name, prefix, std::strlen(prefix)) != 0) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int consumed = 0;
  if (std::sscanf(name, "%*[^_]_%*[^_]_%4d-%2u-%2u%n", &year, &month, &day, &consumed) != 3 ||
      name[consumed] != '\0') {
    return false;
  }

  return year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool isReadingStatsBackupName(const char* name) {
  return isValidDateBackupName(READING_STATS_BACKUP_PREFIX, name);
}

bool isSyncDateBackupName(const char* name) {
  return isValidDateBackupName(READING_STATS_SYNCDATE_PREFIX, name);
}
}  // namespace

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
      if (isReadingStatsBackupName(name) || isSyncDateBackupName(name)) {
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finishWithSelection();
    return;
  }

  const int itemCount = static_cast<int>(importPaths.size());
  if (itemCount <= 0) {
    return;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  buttonNavigator.onNextRelease([this, itemCount] {
    selectedIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(selectedIndex), itemCount));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectedIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(selectedIndex), itemCount));
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, itemCount, pageItems] {
    selectedIndex =
        static_cast<size_t>(ButtonNavigator::nextPageIndex(static_cast<int>(selectedIndex), itemCount, pageItems));
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, itemCount, pageItems] {
    selectedIndex =
        static_cast<size_t>(ButtonNavigator::previousPageIndex(static_cast<int>(selectedIndex), itemCount, pageItems));
    requestUpdate();
  });
}

void ReadingStatsImportActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_IMPORT_READING_STATS), READING_STATS_EXPORT_DIR);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (importPaths.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_READING_STATS_EXPORT));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(importPaths.size()),
        static_cast<int>(selectedIndex), [this](int index) { return getDisplayName(index); }, nullptr,
        [](int) { return UIIcon::File; });
  }

  const bool hasImports = !importPaths.empty();
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), hasImports ? tr(STR_SELECT) : "",
                              hasImports ? tr(STR_DIR_UP) : "", hasImports ? tr(STR_DIR_DOWN) : "");
  renderer.displayBuffer();
}
