# CPR-vCodex Steroids — Sleep Screen & Power Button

> **SCOPE:** Sleep screen modes, short-press power button cycling, cover display, separation from screensaver. Low-medium merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Sleep Screen Modes** | Off, Clock, Cover, Clock+Cover, Custom Image |
| **Cover Mode** | Current book cover / recent book cover / random |
| **Cover Filter** | Grayscale, inverted, sepia, none |
| **Clean Refresh** | Full refresh vs partial on sleep entry |
| **Power Button Short Press** | Cycles sleep screen mode (configurable) |
| **Power Button Long Press** | Sleep / Power off (configurable) |
| **Separate from Screensaver** | Sleep screen ≠ Screensaver (different features) |

---

## 2. Technical Architecture

### 2.1 Sleep Screen Modes (`CrossPointSettings`)
```cpp
enum SleepScreenMode {
    SLEEP_SCREEN_OFF = 0,
    SLEEP_SCREEN_CLOCK = 1,
    SLEEP_SCREEN_COVER = 2,
    SLEEP_SCREEN_CLOCK_COVER = 3,
    SLEEP_SCREEN_CUSTOM = 4,
    SLEEP_SCREEN_MODE_COUNT
};

enum SleepScreenCoverMode {
    COVER_CURRENT_BOOK = 0,
    COVER_RECENT_BOOK = 1,
    COVER_RANDOM = 2,
};

enum SleepScreenCoverFilter {
    FILTER_NONE = 0,
    FILTER_GRAYSCALE = 1,
    FILTER_INVERTED = 2,
    FILTER_SEPIA = 3,
};
```

### 2.2 Power Button Actions
```cpp
enum PowerButtonShortPress {
    SHORT_PRESS_NONE = 0,
    SHORT_PRESS_CYCLE_SLEEP_MODE = 1,
    SHORT_PRESS_CYCLE_COVER = 2,
    SHORT_PRESS_SHOW_CLOCK = 3,
};

enum PowerButtonLongPress {
    LONG_PRESS_SLEEP = 0,
    LONG_PRESS_POWER_OFF = 1,
    LONG_PRESS_NONE = 2,
};
```

### 2.3 SleepActivity Flow
```cpp
// SleepActivity::onEnter()
1. Read settings: mode, coverMode, coverFilter, cleanRefresh
2. If mode == OFF → immediate deep sleep
3. Generate sleep screen image:
   - Clock: draw time/date
   - Cover: load current/recent/random book cover
   - Custom: load from `/.crosspoint/sleep/`
4. Apply filter (grayscale/inverted/sepia)
5. Display with cleanRefresh setting
6. Enter low-power loop (wait for button/wake)

// Short press handler:
if (powerButtonShortPress == CYCLE_SLEEP_MODE) {
    cycleSetting(&SETTINGS.sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    requestSleepScreenUpdate();
}
```

### 2.4 Separation from Screensaver
```cpp
// SleepScreen = low-power display during sleep (shows cover/clock)
// Screensaver = animated display during idle (separate feature)
// Decision: keep separate, integrate screensaver as own feature
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/activities/boot_sleep/SleepActivity.h/cpp` | Sleep screen rendering, mode cycling |
| `src/CrossPointSettings.h` | Settings enums, persistence |
| `src/activities/settings/SettingsActivity.cpp` | Sleep screen settings UI |
| `src/main.cpp` | Power button interrupt handling |
| `src/components/themes/lyra/LyraTheme.cpp` | Sleep screen drawing helpers |

---

## 4. Settings Persistence
```cpp
// Saved on Settings exit
SETTINGS.sleepScreen
SETTINGS.sleepScreenCoverMode
SETTINGS.sleepScreenCoverFilter
SETTINGS.cleanSleepRefresh
SETTINGS.powerButtonShortPress
SETTINGS.powerButtonLongPress
```

---

## 5. Upstream Merge Notes

### LOW-MEDIUM RISK
- `SleepActivity` — Extended with mode cycling
- `CrossPointSettings` — New enums and fields
- `SettingsActivity` — New sleep screen settings section

### Conflicts Likely
- `SleepActivity` rendering logic
- Power button interrupt handling in `main.cpp`
- Settings persistence structure

### Safe Cherry-Picks
- Short-press power button cycle pattern
- Cover filter application (grayscale/inverted/sepia)
- Clean refresh toggle

---

## 6. Validation Checklist

- [ ] Sleep screen shows clock (mode 1)
- [ ] Sleep screen shows current book cover (mode 2)
- [ ] Sleep screen shows clock + cover (mode 3)
- [ ] Cover filter: grayscale/inverted/sepia applied
- [ ] Clean refresh: full vs partial visible
- [ ] Short press power → cycles sleep mode
- [ ] Long press power → sleep/power off per setting
- [ ] Custom image from `/.crosspoint/sleep/` loads
- [ ] Settings UI shows all sleep options
- [ ] Screensaver unaffected (separate feature)

---

## 7. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-SCREENSAVER.md` — Screensaver (separate)
- `STEROIDS-ADDICTIONS-SETTINGS.md` — Settings UI integration
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — Sleep fast restart

---

*Last updated: 2026-09-28 | Commit: af0a3fcb (short-press power button)*