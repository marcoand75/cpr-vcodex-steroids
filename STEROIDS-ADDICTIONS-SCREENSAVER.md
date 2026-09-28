# CPR-vCodex Steroids — Screensaver

> **SCOPE:** Idle-time animated screensaver, separate from sleep screen. Medium merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Activation** | After configurable idle timeout (no input) |
| **Animation Types** | Clock, bouncing logo, image slideshow, custom |
| **Exit** | Any button/touch → immediate return to previous activity |
| **Settings** | Timeout, animation type, custom image folder |
| **Separate from Sleep** | Sleep = low-power; Screensaver = animated idle display |

---

## 2. Technical Architecture

### 2.1 ScreensaverActivity
```cpp
// src/activities/apps/ScreenSaverActivity.h/cpp
class ScreenSaverActivity : public Activity {
    enum class Type { CLOCK, BOUNCE_LOGO, SLIDESHOW, CUSTOM };

    // Idle detection via ActivityManager::updateIdleTimer()
    // Activated when idle > SETTINGS.screensaverTimeout
    // Runs until any input event
};
```

### 2.2 Idle Timer Integration
```cpp
// ActivityManager.cpp
void ActivityManager::updateIdleTimer() {
    if (currentActivity_ && !currentActivity_->isScreensaver()) {
        if (millis() - lastInputTime_ > SETTINGS.screensaverTimeout) {
            launchScreensaver();
        }
    }
}

void ActivityManager::onInputEvent() {
    lastInputTime_ = millis();
    if (currentActivity_->isScreensaver()) {
        exitScreensaver();  // Return to previous activity
    }
}
```

### 2.3 Animation Types
| Type | Description |
|------|-------------|
| `CLOCK` | Large analog/digital clock, updates per minute |
| `BOUNCE_LOGO` | Steroids logo bouncing (DVD screensaver style) |
| `SLIDESHOW` | Images from `/.crosspoint/screensaver/` |
| `CUSTOM` | Lua plugin or custom animation |

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/activities/apps/ScreenSaverActivity.h/cpp` | Main activity, animations |
| `src/ActivityManager.h/cpp` | Idle timer, launch/exit logic |
| `src/activities/settings/SettingsActivity.cpp` | Screensaver settings UI |
| `src/CrossPointSettings.h` | Settings fields |
| `src/activities/apps/AppsActivity.cpp` | "Screensaver" shortcut |
| `src/activities/home/HomeActivity.cpp` | "Screensaver" shortcut in Home |

---

## 4. Settings
```cpp
SETTINGS.screensaverEnabled       // bool
SETTINGS.screensaverTimeout       // uint32_t (minutes)
SETTINGS.screensaverType          // enum (CLOCK, BOUNCE, SLIDESHOW, CUSTOM)
SETTINGS.screensaverCustomPath    // string (custom image folder)
```

---

## 5. Upstream Merge Notes

### MEDIUM RISK
- `ScreenSaverActivity` — New activity
- `ActivityManager` — Idle timer integration (core change)
- Settings fields and UI

### Conflicts Likely
- `ActivityManager` idle tracking (core infrastructure)
- Activity stack management (screensaver interrupts)

### Safe Cherry-Picks
- Animation rendering patterns
- Idle detection concept (if upstream adds it)

---

## 6. Validation Checklist

- [ ] Screensaver activates after timeout
- [ ] Clock animation updates correctly
- [ ] Bounce logo animates smoothly
- [ ] Slideshow loads images from SD
- [ ] Any input exits screensaver immediately
- [ ] Returns to correct previous activity
- [ ] Settings UI configures all options
- [ ] Shortcut works from Apps/Home
- [ ] Fast restart from screensaver works
- [ ] Doesn't interfere with sleep screen

---

## 7. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-SLEEP.md` — Sleep screen (separate)
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — Screensaver fast restart
- `STEROIDS-ADDICTIONS-LUA.md` — Custom animation via Lua

---

*Last updated: 2026-09-28 | Commit: dd677063 (add screensaver app)*