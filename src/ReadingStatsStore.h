#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingSessionLog.h"

inline uint64_t getDailyReadingGoalMs() { return SETTINGS.getDailyGoalMs(); }

struct ReadingDayStats {
  uint32_t dayOrdinal = 0;
  uint64_t readingMs = 0;
};

struct ReadingBookStats {
  std::string bookId;
  std::string path;
  std::vector<std::string> knownPaths;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  std::string chapterTitle;
  std::vector<ReadingDayStats> readingDays;
  uint64_t totalReadingMs = 0;
  uint32_t sessions = 0;
  uint32_t lastSessionMs = 0;
  uint32_t firstReadAt = 0;
  uint32_t lastReadAt = 0;
  uint32_t completedAt = 0;
  uint8_t lastProgressPercent = 0;
  uint8_t chapterProgressPercent = 0;
  bool completed = false;
};

struct ReadingSessionSnapshot {
  bool valid = false;
  uint32_t serial = 0;
  std::string bookId;
  std::string path;
  uint32_t sessionMs = 0;
  bool counted = false;
  bool completedThisSession = false;
  uint8_t startProgressPercent = 0;
  uint8_t endProgressPercent = 0;
};

// Lightweight global + per-book snapshot written to summary.json so the Home
// screen can render the stats panel and carousel progress badges without
// loading the full reading stats store into RAM.
struct SummaryJSON {
  struct Global {
    uint64_t totalReadingMs = 0;
    uint64_t todayReadingMs = 0;
    uint64_t recent7ReadingMs = 0;
    uint64_t recent30ReadingMs = 0;
    uint32_t currentStreakDays = 0;
    uint32_t maxStreakDays = 0;
    uint32_t booksFinishedCount = 0;
    uint64_t goalReadingMs = 0;
    uint64_t dailyAverageMs = 0;
    uint32_t referenceDayOrdinal = 0;
  };

  struct BookBadge {
    std::string bookId;
    std::string path;
    uint8_t progressPercent = 0;
    uint64_t totalReadingMs = 0;
    uint32_t sessions = 0;
    uint32_t readingDaysCount = 0;
    bool completed = false;
  };

  Global global;
  std::vector<BookBadge> bookBadges;
};

struct GlobalSummary {
  uint64_t dailyAverageMs = 0;
  uint64_t todayMs = 0;
  uint32_t streakDays = 0;
  uint32_t booksFinished = 0;
};

class ReadingStatsStore;
namespace JsonSettingsIO {
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);
bool loadReadingStatsDocument(ReadingStatsStore& store, const JsonDocument& doc);
}  // namespace JsonSettingsIO
namespace ReadingStatsStreamingLoader {
bool loadFromFileStreaming(const char* moduleName, const char* path, ReadingStatsStore& store);
}

class ReadingStatsStore {
  static ReadingStatsStore instance;

  struct SummaryCache {
    bool valid = false;
    uint32_t referenceDayOrdinal = 0;
    uint32_t booksFinishedCount = 0;
    uint64_t totalReadingMs = 0;
    uint64_t todayReadingMs = 0;
    uint64_t recent7ReadingMs = 0;
    uint64_t recent30ReadingMs = 0;
    uint32_t currentStreakDays = 0;
    uint32_t maxStreakDays = 0;
    uint64_t goalReadingMs = 0;
  };

  struct SessionState {
    bool active = false;
    bool paused = false;
    size_t bookIndex = 0;
    // Detached session: the store is not materialized (deferred load). The
    // session accumulates in RAM and ends as one fixed 32-byte record in the
    // binary journal (/.crosspoint/reading_sessions.jrn); records merge into
    // the store at the next full load. The ~50 KB store never materializes
    // inside the reader.
    bool detached = false;
    std::string detachedBookId;
    std::string detachedPath;
    uint8_t detachedProgress = 0;
    bool detachedCompleted = false;
    unsigned long lastInteractionMs = 0;
    uint64_t accumulatedMs = 0;
    uint8_t startProgressPercent = 0;
    bool startCompleted = false;
  };

  std::vector<ReadingBookStats> books;
  std::vector<ReadingDayStats> legacyReadingDays;
  std::vector<ReadingDayStats> readingDays;
  std::vector<ReadingSessionLogEntry> sessionLog;
  SessionState activeSession;
  ReadingSessionSnapshot lastSessionSnapshot;
  uint32_t sessionSerialCounter = 0;
  mutable SummaryCache summaryCache;
  mutable bool dirty = false;
  mutable unsigned long lastSaveMs = 0;
  mutable bool persistenceSuspended = false;
  mutable bool skippedSaveLogged = false;
  mutable bool internalBackupPrepared = false;
  mutable bool loaded_ = false;
  mutable SummaryJSON summaryJson;
  mutable bool summaryJsonValid_ = false;

  friend bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStatsDocument(ReadingStatsStore&, const JsonDocument&);
  friend bool ReadingStatsStreamingLoader::loadFromFileStreaming(const char*, const char*, ReadingStatsStore&);

  size_t findBookIndexByPath(const std::string& path) const;
  size_t findBookIndexByBookId(const std::string& bookId) const;
  size_t findLegacyMergeCandidate(const std::string& path, const std::string& title = "",
                                  const std::string& author = "") const;
  void mergeBookInto(ReadingBookStats& primary, const ReadingBookStats& duplicate);
  void normalizeBook(ReadingBookStats& book);
  void normalizeBooks();
  void rememberBookPath(ReadingBookStats& book, const std::string& path);
  void rememberBookIdAlias(ReadingBookStats& book, const std::string& bookId);
  size_t getOrCreateBookIndex(const std::string& path, const std::string& title, const std::string& author,
                              const std::string& coverBmpPath, const std::string& preferredBookId = "");
  void touchBook(size_t index);
  ReadingDayStats& getOrCreateReadingDay(uint32_t epochSeconds);
  ReadingDayStats& getOrCreateBookReadingDay(ReadingBookStats& book, uint32_t epochSeconds);
  uint32_t getLatestKnownTimestamp() const;
  uint32_t getReferenceTimestamp(uint32_t preferredTimestamp, uint32_t bookTimestamp = 0) const;
  uint32_t getReferenceDayOrdinal() const;
  void updateBookReadTimestamp(ReadingBookStats& book, uint32_t preferredTimestamp);
  void recordReadingTime(ReadingBookStats& book, uint32_t epochSeconds, uint64_t readingMs);
  void appendSessionLogEntry(uint32_t dayOrdinal, uint32_t sessionMs, size_t bookIndex);
  void appendSessionToJournal(uint32_t dayOrdinal, uint32_t sessionMs, const std::string& bookId,
                              uint8_t progressPercent, bool completed) const;
  // Session identity interning: resolve entries pointing at books[bookIndex]
  // and free their duplicate strings; used after JSON loads and book merges.
  void internSessionLogIdentities();
  // Fill outBookId/outPath with the session's identity, resolving interned
  // indexes against the current books array.
  void resolveSessionIdentity(const ReadingSessionLogEntry& session, std::string& outBookId,
                              std::string& outPath) const;
  bool convertLegacyReadingDaysToUnassigned();
  void rebuildAggregatedReadingDays();
  bool removeIgnoredBooks();
  bool hasAnyStats() const;
  void invalidateSummaryCache();
  void rebuildSummaryCache() const;
  bool shouldSaveDeferred() const;
  void markDirty();
  bool prepareInternalBackup() const;
  bool refreshInternalBackupFromMain() const;
  bool restoreInternalBackupToMain(const char* reason) const;
  bool maybeCreateAutoBackup(bool force) const;
  bool persistToFile(const char* path) const;
  bool saveSummaryJSON() const;
  bool loadSummaryJSON(SummaryJSON& out) const;
  const SummaryJSON& getSummaryJSON() const;
  static bool isClockValid(uint32_t epochSeconds);

 public:
  ~ReadingStatsStore() = default;

  static ReadingStatsStore& getInstance() { return instance; }

  void preloadHomeSummary();
  bool isSummaryValid() const { return summaryJsonValid_; }
  // Check whether the calendar day has changed since the last summary was
  // written. Returns true (and invalidates the cache) when a day boundary
  // was crossed so that todayReadingMs resets for the new day.
  bool checkDayChange();

  void beginSession(const std::string& path, const std::string& title, const std::string& author,
                    const std::string& coverBmpPath, uint8_t progressPercent = 0, const std::string& chapterTitle = "",
                    uint8_t chapterProgressPercent = 0);
  void noteActivity();
  void tickActiveSession();
  void pauseSession();
  void resumeSession();
  void updateProgress(uint8_t progressPercent, bool completed = false, const std::string& chapterTitle = "",
                      uint8_t chapterProgressPercent = 0);
  void endSession();
  bool adjustBookReadingTime(const std::string& path, uint32_t dayOrdinal, int32_t deltaMs);
  bool setBookFirstReadDate(const std::string& path, uint32_t dayOrdinal);
  bool updateBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                          const std::string& coverBmpPath);
  bool updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title = "",
                      const std::string& author = "", const std::string& coverBmpPath = "",
                      const std::string& bookId = "");
  bool removeBook(const std::string& path);
  const ReadingBookStats* findBook(const std::string& key) const;
  const ReadingBookStats* findMatchingBookForPath(const std::string& path, const std::string& title = "",
                                                  const std::string& author = "") const;
  const ReadingSessionSnapshot& getLastSessionSnapshot() const { return lastSessionSnapshot; }

  // Additive lazy-loading helpers used by the Library. `ensureLoaded()` is cheap
  // once main.cpp has loaded the store at boot.
  bool isLoaded() const { return loaded_; }
  bool ensureLoaded();
  void resetLoaded() { loaded_ = false; }

   // Binary session journal (fixed 48-byte records, O_APPEND):
   // [u32 dayOrdinal][u32 sessionMs][u8 progress][u8 flags][u8 bookId[32]][u16 pad]
   // The full 32-byte KOReader content hash is stored verbatim — no truncation.
   // Detached sessions append here without the store; records merge into the
   // store and the journal is removed at the next full load.
   static constexpr size_t JOURNAL_RECORD_BYTES = 48;
   static constexpr uint32_t JOURNAL_MAGIC_V2 = 0x52534A32; // "RSJ2"
  bool hasPendingJournalSessions() const;
  void mergeSessionJournal();
  // Lightweight attempt to merge pending journal sessions and update the
  // summary JSON without requiring the full store to be materialized in RAM.
  // Returns true if the journal was successfully merged (store was loaded),
  // false if deferred due to low heap or boot gate. Safe to call from the
  // reader exit path so the stats detail page always has fresh data.
  bool tryMergePendingSession();
  // Lightweight journal-to-summary update: reads journal records and updates
  // summary.json directly without materializing the full 50 KB store. This is
   // called from endSession() (detached path) and preloadHomeSummary() to keep
   // summary.json current even when the full store was never loaded.
   // hintPath: when provided, populates stub badge paths so that lookups with
   // full 32-byte bookIds can still match via the path fallback.
   void updateSummaryFromJournal();
  // const-safe lazy load for read getters (used when boot deferred the load).
  void ensureLoadedForRead() const {
    if (!loaded_) {
      const_cast<ReadingStatsStore*>(this)->ensureLoaded();
    }
  }

  // Lightweight per-book completion/badge lookup for the Home/Library render
  // path. Additive non-streaming shim over the resident store (the memory-lean
  // summary.json variant is a separate Steroids port).
  const ReadingBookStats* getHomeBookStatsForRender(const std::string& bookId, const std::string& path) const;

  // Additive home-screen helpers used by the Lyra MarcoAnd75 theme. Computed
  // from the resident store; the memory-lean summary.json variant is a
  // separate Steroids port.
  uint8_t getBookProgressForHome(const std::string& bookId, const std::string& path) const;
  bool getBookHomeStats(const std::string& bookId, const std::string& path, SummaryJSON::BookBadge& badge) const;
  GlobalSummary getGlobalSummary() const;

  const std::vector<ReadingBookStats>& getBooks() const { return books; }
  const std::vector<ReadingDayStats>& getReadingDays() const { return readingDays; }
  const std::vector<ReadingSessionLogEntry>& getSessionLog() const { return sessionLog; }
  static bool shouldIgnorePath(const std::string& path);

  uint32_t getBooksStartedCount() const { return static_cast<uint32_t>(books.size()); }
  uint32_t getBooksFinishedCount() const;
  uint64_t getTotalReadingMs() const;
  uint64_t getTodayReadingMs() const;
  uint64_t getRecentReadingMs(uint32_t days) const;
  uint32_t getCurrentStreakDays() const;
  uint32_t getMaxStreakDays() const;
  uint32_t getDisplayTimestamp(bool* usedFallback = nullptr) const;
  bool hasReadingDays() const { return !readingDays.empty(); }

  void reset();
  bool exportToFile(const std::string& path) const;
  bool importFromFile(const std::string& path);
  bool saveToFile() const;
  bool isAutoBackupDue() const;
  bool createDueAutoBackup() const;
  bool hasAutoBackups() const;
  bool ensureAutoBackupForEnabledSetting() const;
  int clearAutoBackups() const;
  bool loadFromFile();
  void markLoadSkippedForRecovery();
  bool releaseMemoryForNetwork();
  bool reloadAfterNetwork();
};

#define READING_STATS ReadingStatsStore::getInstance()
