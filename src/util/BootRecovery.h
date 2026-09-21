#pragma once

#include <cstdint>
#include <functional>

namespace BootRecovery {

enum class BootStage : uint8_t {
  None = 0,
  Settings,
  Language,
  KOReader,
  OPDS,
  UiTheme,
  DisplayAndFonts,
  State,
  ReadingStats,
  RecentBooks,
  Favorites,
  Flashcards,
  Achievements,
  RouteDecision,
  Completed,
};

void initialize();
void enterStage(BootStage stage);
void markBootCompleted();

// Boot-stage entry helper. Consolidates the "if (shouldSkip) logSkip else
// enterStage + loader + LOG_DBG heap" ritual that setup() used to repeat 6
// times. The lambda is called when the stage is NOT skipped.
//
// Usage:
//   if (BootRecovery::runBootStage(BootRecovery::BootStage::Settings,
//                                  BootRecovery::shouldSkipSettings(),
//                                  "settings", [] { SETTINGS.loadFromFile(); })) {
//     LOG_DBG("BOOT", "After settings: free=%u maxA=%u", ...);
//   }
//
// When `loader` is null, the stage is treated as "deferred" and no lambda
// is invoked; this is the form used by reading-stats / favorites /
// flashcards / achievements which are loaded on demand.
bool runBootStage(BootStage stage, bool shouldSkip, const char* stageLabel, const std::function<void()>& loader);

// Set the skip-log emitter used by runBootStage(). Call once from setup().
// The default emitter is a no-op; providing a real one (typically a
// thin wrapper around CPR_VCODEX_LOG_EVENT) keeps the runBootStage log
// message in sync with the rest of the boot diagnostics.
using SkipLogFn = void (*)(const char* stageLabel);
void setSkipLogFn(SkipLogFn fn);

BootStage getRecordedStage();
const char* getStageName(BootStage stage);

bool isRecoveryActive();
bool shouldForceHome();

bool shouldSkipSettings();
bool shouldSkipLanguage();
bool shouldSkipKOReader();
bool shouldSkipOPDS();
bool shouldSkipState();
bool shouldSkipReadingStats();
bool shouldSkipRecentBooks();
bool shouldSkipFavorites();
bool shouldSkipFlashcards();
bool shouldSkipAchievements();

}  // namespace BootRecovery
