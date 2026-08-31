#include "ReadingStatsBackupManager.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "util/TimeUtils.h"

namespace ReadingStatsBackup {

bool textWindowShowsReadingStatsData(const std::string& text) {
  static constexpr const char* DATA_ARRAY_KEYS[] = {
      "\"readingDays\":[",
      "\"legacyReadingDays\":[",
      "\"sessionLog\":[",
      "\"books\":[",
  };

  for (const char* key : DATA_ARRAY_KEYS) {
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
      size_t valuePos = pos + std::strlen(key);
      while (valuePos < text.size() &&
             (text[valuePos] == ' ' || text[valuePos] == '\n' || text[valuePos] == '\r' || text[valuePos] == '\t')) {
        ++valuePos;
      }
      if (valuePos < text.size() && text[valuePos] != ']') {
        return true;
      }
      pos = valuePos;
    }
  }
  return false;
}

bool statsFileAppearsToHaveData(const char* path) {
  if (!path || !Storage.exists(path)) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("RST", path, file)) {
    return false;
  }

  char buffer[256];
  std::string window;
  window.reserve(512);
  while (true) {
    const int readBytes = file.read(buffer, sizeof(buffer));
    if (readBytes <= 0) {
      break;
    }
    window.append(buffer, static_cast<size_t>(readBytes));
    if (textWindowShowsReadingStatsData(window)) {
      file.close();
      return true;
    }
    if (window.size() > 512) {
      window.erase(0, window.size() - 256);
    }
  }

  file.close();
  return false;
}

bool copyFileViaTemp(const char* moduleName, const char* sourcePath, const char* targetPath) {
  if (!sourcePath || !targetPath || !Storage.exists(sourcePath)) {
    return false;
  }

  const std::string tempPath = std::string(targetPath) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile source;
  if (!Storage.openFileForRead(moduleName, sourcePath, source)) {
    return false;
  }

  HalFile target;
  if (!Storage.openFileForWrite(moduleName, tempPath.c_str(), target)) {
    source.close();
    return false;
  }

  char buffer[512];
  bool ok = true;
  while (true) {
    const int readBytes = source.read(buffer, sizeof(buffer));
    if (readBytes < 0) {
      ok = false;
      break;
    }
    if (readBytes == 0) {
      break;
    }
    const size_t written = target.write(buffer, static_cast<size_t>(readBytes));
    if (written != static_cast<size_t>(readBytes)) {
      ok = false;
      break;
    }
  }

  target.flush();
  target.close();
  source.close();

  if (!ok) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (!Storage.rename(tempPath.c_str(), targetPath)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  return true;
}

std::string formatBackupDateFromDayOrdinal(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return "";
  }

  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
  return std::string(buffer);
}

std::string getAutoBackupPathForDayOrdinal(const uint32_t dayOrdinal) {
  const std::string dateText = formatBackupDateFromDayOrdinal(dayOrdinal);
  return dateText.empty() ? std::string() : std::string(READING_STATS_BACKUP_EXPORT_PREFIX) + dateText;
}

bool autoBackupFileHasDataForDayOrdinal(const uint32_t dayOrdinal) {
  const std::string backupPath = getAutoBackupPathForDayOrdinal(dayOrdinal);
  return !backupPath.empty() && statsFileAppearsToHaveData(backupPath.c_str());
}

bool parseAutoBackupDayOrdinal(const char* name, uint32_t& dayOrdinal) {
  if (!name || std::strncmp(name, READING_STATS_BACKUP_EXPORT_FILE_PREFIX,
                            std::strlen(READING_STATS_BACKUP_EXPORT_FILE_PREFIX)) != 0) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int consumed = 0;
  if (std::sscanf(name, "stats_backup_%4d-%2u-%2u%n", &year, &month, &day, &consumed) != 3 || name[consumed] != '\0') {
    return false;
  }

  if (!TimeUtils::getTimestampForLocalDate(year, month, day, nullptr)) {
    return false;
  }

  dayOrdinal = TimeUtils::getDayOrdinalForDate(year, month, day);
  return dayOrdinal != 0;
}

uint32_t getLatestAutoBackupDayOrdinal() {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  uint32_t latestDayOrdinal = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (!parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      continue;
    }

    const std::string backupPath = std::string(READING_STATS_EXPORT_DIR) + "/" + name;
    if (statsFileAppearsToHaveData(backupPath.c_str())) {
      latestDayOrdinal = std::max(latestDayOrdinal, dayOrdinal);
    }
  }
  dir.close();
  return latestDayOrdinal;
}

size_t countAutoBackupFiles() {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  size_t count = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      ++count;
    }
  }
  dir.close();
  return count;
}

bool findOldestAutoBackupPath(std::string& oldestPath) {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  uint32_t oldestDayOrdinal = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (!parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      continue;
    }
    if (oldestDayOrdinal == 0 || dayOrdinal < oldestDayOrdinal) {
      oldestDayOrdinal = dayOrdinal;
      oldestPath = std::string(READING_STATS_EXPORT_DIR) + "/" + name;
    }
  }
  dir.close();
  return oldestDayOrdinal != 0 && !oldestPath.empty();
}

void pruneAutoBackupsToLimit(const size_t maxBackups) {
  while (countAutoBackupFiles() > maxBackups) {
    std::string oldestPath;
    if (!findOldestAutoBackupPath(oldestPath)) {
      break;
    }
    if (!Storage.remove(oldestPath.c_str())) {
      LOG_ERR("RST", "Failed to prune old reading stats backup %s", oldestPath.c_str());
      break;
    }
    LOG_DBG("RST", "Pruned old reading stats backup %s", oldestPath.c_str());
  }
}

}  // namespace ReadingStatsBackup
