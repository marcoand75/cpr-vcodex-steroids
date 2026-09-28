# CPR-vCodex Steroids — Standardized Fast Restart

> **SCOPE:** Declarative fast-restart exit policy for all activities. Replaces ad-hoc `goHome()`/`finish()` with `exitRestartPlan()` virtual method and `ActivityManager::exitWithFastRestart()`.

---

## 1. User-Facing Functionality

| Scenario | Before | After (Fast Restart) |
|----------|--------|----------------------|
| Exit Library → Home | Full reload | Seamless restart, no logos |
| Exit Reader → Home | Full reload | Seamless restart, preserves session |
| Exit Settings → Home | Full reload | Seamless restart |
| Exit Apps → Home | Full reload | Seamless restart |
| Exit Wikipedia/QuickCards → Home | Full reload | Seamless restart |
| Long-press power → Sleep | N/A | Cycles sleep screen modes |

**User Experience:** Instant return to Home, no boot logos, heap defragmented.

---

## 2. Technical Architecture

### 2.1 Core Types

```cpp
// src/SilentRestart.h
enum class RestartLanding {
    None = 0,      // Normal exit (finish/pop)
    Home = 1,      // Restart to Home activity
    Apps = 2,      // Restart to Apps activity
    Reader = 3,    // Restart to Reader (resume book)
};

struct ExitRestartPlan {
    bool enabled = false;
    RestartLanding landing = RestartLanding::None;
    bool seamless = false;  // true = silent restart (no logos, RTC wake)
};
```

### 2.2 Activity Integration

```cpp
// src/Activity.h
virtual ExitRestartPlan exitRestartPlan() const { return {}; }
void exitWithFastRestart();  // calls ActivityManager::exitWithFastRestart()
```

### 2.3 ActivityManager Coordination

```cpp
// src/ActivityManager.cpp
void ActivityManager::exitWithFastRestart() {
    ExitRestartPlan plan = currentActivity_->exitRestartPlan();
    if (!plan.enabled) { goHome(); return; }

    // RTC wake + silent boot
    rtc.setWakeupSource(plan.landing);
    if (plan.seamless) rtc.enableSilentBoot();
    esp_deep_sleep_start();  // or equivalent
}
```

### 2.4 RTC Targets (SilentRestart.cpp)
```cpp
// Maps RestartLanding to RTC memory slots for post-restart routing
void setRestartTarget(RestartLanding landing, bool seamless);
RestartLanding getRestartTarget();
bool consumeRestartTarget();  // called in main.cpp boot sequence
```

---

## 3. Activity Implementations

| Activity | `exitRestartPlan()` | Notes |
|----------|---------------------|-------|
| `LibraryActivity` | `{true, Home, true}` | Always seamless to Home |
| `AppsActivity` | `{true, Home, true}` | Always seamless to Home |
| `SettingsActivity` | `{true, Home, true}` | Always seamless to Home |
| `WikipediaActivity` | `{true, Home, true}` | Always seamless to Home |
| `QuickCardsActivity` | `{true, Home, true}` | Always seamless to Home |
| `EpubReaderActivity` | `{true, Home, true}` | Via `activityManager.exitWithFastRestart()` |
| `TxtReaderActivity` | `{true, Home, true}` | Via `activityManager.exitWithFastRestart()` |
| `XtcReaderActivity` | `{true, Home, true}` | Via `activityManager.exitWithFastRestart()` |
| `ReadingStatsDetailActivity` | `{true, Home, true}` only if `showSessionSummary` | Else normal `finish()` |
| `LuaPluginActivity` | `{true, Home, true}` for browser; in-process disabled | `PluginBrowserActivity` → Home |

### 3.1 Reader Activities (Special)
```cpp
// EpubReaderActivity::onExit()
if (shouldFastRestart()) {
    activityManager.exitWithFastRestart();  // delegates to manager
} else {
    finish();  // normal pop
}
```

### 3.2 ReadingStatsDetailActivity (Conditional)
```cpp
ExitRestartPlan ReadingStatsDetailActivity::exitRestartPlan() const override {
    if (context.showSessionSummary) return {true, RestartLanding::Home, true};
    return {};  // normal pop for regular stats navigation
}
```

---

## 4. Boot Sequence Integration

```cpp
// main.cpp boot sequence
void setup() {
    // ... hardware init ...

    // Check for fast restart target
    if (SilentRestart::consumeRestartTarget()) {
        RestartLanding target = SilentRestart::getRestartTarget();
        if (target == RestartLanding::Home) {
            activityManager.goHome();  // seamless, no logos
            return;
        }
    }

    // Normal boot → BootActivity → Home
}
```

---

## 5. Memory Management

### 5.1 Pre-Restart Release
```cpp
// ReaderActivity::create() — called for ALL reader entries
READING_STATS.releaseMemoryForNetwork();  // clears fat store, keeps summary

// LibraryActivity::onExit() — implicit via ActivityManager
```

### 5.2 Post-Restart State
- `summary.json` valid (preloaded at boot)
- Fat store NOT loaded until needed
- Heap defragmented (deep sleep + fresh boot)
- `maxA` typically > 90 KB vs ~10 KB pre-restart

---

## 6. File Inventory

| File | Role |
|------|------|
| `src/SilentRestart.h/cpp` | `RestartLanding`, `ExitRestartPlan`, RTC target management |
| `src/Activity.h/cpp` | Virtual `exitRestartPlan()`, `exitWithFastRestart()` |
| `src/ActivityManager.h/cpp` | `exitWithFastRestart()` coordination |
| `src/activities/apps/LibraryActivity.cpp` | `exitRestartPlan()` → Home seamless |
| `src/activities/apps/AppsActivity.cpp` | `exitRestartPlan()` → Home seamless |
| `src/activities/settings/SettingsActivity.cpp` | `exitRestartPlan()` → Home seamless |
| `src/activities/apps/WikipediaActivity.cpp` | `exitRestartPlan()` → Home seamless |
| `src/activities/apps/QuickCardsActivity.cpp` | `exitRestartPlan()` → Home seamless |
| `src/activities/reader/EpubReaderActivity.cpp` | Delegates to `activityManager.exitWithFastRestart()` |
| `src/activities/reader/TxtReaderActivity.cpp` | Delegates to `activityManager.exitWithFastRestart()` |
| `src/activities/reader/XtcReaderActivity.cpp` | Delegates to `activityManager.exitWithFastRestart()` |
| `src/activities/apps/ReadingStatsDetailActivity.cpp` | Conditional plan (session summary only) |
| `src/activities/apps/LuaPluginActivity.cpp` | `PluginBrowserActivity` → Home seamless |
| `src/main.cpp` | Boot sequence consumes RTC restart target |

---

## 7. Upstream Merge Notes

### Protected
- `Activity::exitRestartPlan()` virtual method (new API)
- `ActivityManager::exitWithFastRestart()` (new coordination)
- `SilentRestart` module (new RTC-based restart system)
- All activity overrides of `exitRestartPlan()`

### Safe Cherry-Picks
- `ReaderActivity::create()` calling `releaseMemoryForNetwork()` pre-reader
- `ReadingStatsDetailActivity` conditional fast-restart logic
- RTC wakeup source management pattern

### Conflicts Likely
- `Activity` base class (new virtual method)
- `ActivityManager` (new public method)
- Boot sequence in `main.cpp` (restart target consumption)

---

## 8. Validation Checklist

- [ ] Library → Home: seamless, no logos, heap ~112 KB free
- [ ] Reader (EPUB/TXT/XTC) → Home: seamless, session journaled
- [ ] Settings → Home: seamless
- [ ] Apps → Home: seamless
- [ ] Wikipedia/QuickCards → Home: seamless
- [ ] ReadingStatsDetail (session summary) → Home: seamless
- [ ] ReadingStatsDetail (regular) → normal pop
- [ ] Lua Plugin Browser → Home: seamless
- [ ] Post-restart: summary.json valid, store lazy-load works

---

## 9. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-READING-STATS.md` — Reader exit releases store
- `STEROIDS-ADDICTIONS-HOME-THEMES.md` — HomeActivity fast restart

---

*Last updated: 2026-09-28 | Commit: a103e3fe (standardized declarative fast-restart)*