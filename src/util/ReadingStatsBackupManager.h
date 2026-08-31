#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ReadingStatsBackup {

// Paths and limits for reading stats persistence.
constexpr char READING_STATS_FILE_JSON[] = "/.crosspoint/reading_stats.json";
constexpr char READING_STATS_BACKUP_FILE_JSON[] = "/.crosspoint/reading_stats.json.bak";
constexpr char READING_STATS_SUMMARY_JSON[] = "/.crosspoint/summary.json";
constexpr char READING_STATS_EXPORT_DIR[] = "/exports";
constexpr char READING_STATS_BACKUP_EXPORT_PREFIX[] = "/exports/stats_backup_";
constexpr char READING_STATS_BACKUP_EXPORT_FILE_PREFIX[] = "stats_backup_";
constexpr size_t MAX_READING_STATS_AUTO_BACKUPS = 30;

bool copyFileViaTemp(const char* moduleName, const char* sourcePath, const char* targetPath);
std::string formatBackupDateFromDayOrdinal(const uint32_t dayOrdinal);
std::string getAutoBackupPathForDayOrdinal(const uint32_t dayOrdinal);
bool autoBackupFileHasDataForDayOrdinal(const uint32_t dayOrdinal);
bool parseAutoBackupDayOrdinal(const char* name, uint32_t& dayOrdinal);
uint32_t getLatestAutoBackupDayOrdinal();
size_t countAutoBackupFiles();
bool findOldestAutoBackupPath(std::string& oldestPath);
void pruneAutoBackupsToLimit(const size_t maxBackups);

// Helpers shared with main reading-stats data validation.
bool textWindowShowsReadingStatsData(const std::string& text);
bool statsFileAppearsToHaveData(const char* path);

}  // namespace ReadingStatsBackup
