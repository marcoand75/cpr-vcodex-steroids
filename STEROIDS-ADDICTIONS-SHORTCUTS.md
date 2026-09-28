# CPR-vCodex Steroids — Shortcuts Registry

> **SCOPE:** Centralized shortcut definitions, CTAD array, app launching, keyboard mappings. Low merge risk.

---

## 1. User-Facing Functionality

| Shortcut | Trigger | Action |
|----------|---------|--------|
| **Library** | Home grid / Apps | `LibraryActivity` |
| **Settings** | Home grid / Apps | `SettingsActivity` |
| **Apps** | Home grid | `AppsActivity` |
| **Favorites** | Home grid / Apps | `LibraryActivity` (filter=FAVORITES) |
| **Flashcards** | Home grid / Apps | `QuickCardsActivity` |
| **Dictionary** | Home grid / Apps | `DictionaryActivity` |
| **File Transfer** | Home grid / Apps | `UsbMassStorageActivity` |
| **Sleep** | Home grid / Apps | `SleepActivity` |
| **Quick Cards** | Home grid / Apps | `QuickCardsActivity` |
| **Wikipedia** | Home grid / Apps | `WikipediaActivity` |
| **Plugins** | Home grid / Apps | `PluginBrowserActivity` |
| **Screensaver** | Home grid / Apps | `ScreenSaverActivity` |
| **Reading Stats** | Home grid / Apps | `ReadingStatsActivity` |
| **Bookmarks** | Home grid / Apps | `BookmarksAppActivity` |
| **Recent Books** | Home grid | `RecentBooksActivity` |

---

## 2. Technical Architecture

### 2.1 CTAD Array (`ShortcutRegistry.h`)
```cpp
// Modern C++17: Class Template Argument Deduction
// Eliminates 2 ghost entries (size auto-deduced)
struct ShortcutDefinition {
    const char* id;
    const char* i18nKey;
    uint16_t iconId;
    ActivityFactory factory;  // std::function<Activity*()>
    bool showInHome;
    bool showInApps;
};

// Auto-sized array — no manual count, no ghost entries
static const std::array definitions = {
    ShortcutDefinition{"library", "STR_LIBRARY", ICON_LIBRARY,
        []{ return new LibraryActivity(renderer, input); }, true, true},
    ShortcutDefinition{"settings", "STR_SETTINGS", ICON_SETTINGS,
        []{ return new SettingsActivity(renderer, input); }, true, true},
    // ... all shortcuts
};
```

### 2.2 AppsActivity Launch
```cpp
// AppsActivity::openApp(const char* appId)
if (strcmp(appId, "library") == 0) launch<LibraryActivity>();
else if (strcmp(appId, "favorites") == 0) launch<LibraryActivity>(Filter::FAVORITES);
else if (strcmp(appId, "flashcards") == 0) launch<QuickCardsActivity>();
else if (strcmp(appId, "dictionary") == 0) launch<DictionaryActivity>();
else if (strcmp(appId, "file_transfer") == 0) launch<UsbMassStorageActivity>();
else if (strcmp(appId, "sleep") == 0) launch<SleepActivity>();
else if (strcmp(appId, "quick_cards") == 0) launch<QuickCardsActivity>();
else if (strcmp(appId, "wikipedia") == 0) launch<WikipediaActivity>();
else if (strcmp(appId, "plugins") == 0) launch<PluginBrowserActivity>();
else if (strcmp(appId, "screensaver") == 0) launch<ScreenSaverActivity>();
else if (strcmp(appId, "reading_stats") == 0) launch<ReadingStatsActivity>();
else if (strcmp(appId, "bookmarks") == 0) launch<BookmarksAppActivity>();
```

### 2.3 HomeActivity Keyboard Mapping
```cpp
// HomeActivity::handleKeyboardShortcut(key)
switch (key) {
    case 'l': launchShortcut("library"); break;
    case 's': launchShortcut("settings"); break;
    case 'a': launchShortcut("apps"); break;
    case 'f': launchShortcut("favorites"); break;
    case 'c': launchShortcut("flashcards"); break;  // Quick Cards
    case 'w': launchShortcut("wikipedia"); break;
    case 'p': launchShortcut("plugins"); break;
    // ...
}
```

### 2.4 Null Guard in Activity::startActivityForResult
```cpp
// Activity.cpp
void Activity::startActivityForResult(std::unique_ptr<Activity> activity,
                                       ActivityResultCallback callback) {
    if (!activity) return;  // Guard against null factory
    activityManager.pushActivity(std::move(activity), std::move(callback));
}
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/util/ShortcutRegistry.h` | CTAD array, all shortcut definitions |
| `src/activities/apps/AppsActivity.cpp` | `openApp()` launch logic |
| `src/activities/home/HomeActivity.cpp` | Keyboard shortcuts, home grid |
| `src/Activity.cpp` | Null guard in `startActivityForResult` |

---

## 4. Upstream Merge Notes

### LOW RISK
- `ShortcutRegistry.h` — CTAD array (cleaner, no ghost entries)
- `AppsActivity::openApp()` — Extended with new shortcuts
- `HomeActivity` keyboard mapping — Extended
- `Activity::startActivityForResult()` — Null guard (defensive)

### Conflicts Unlikely
- Shortcut definitions are fork-specific additions
- CTAD is pure syntax improvement

### Safe Cherry-Picks
- CTAD array pattern (eliminates size mismatch bugs)
- Null guard in `startActivityForResult`

---

## 5. Validation Checklist

- [ ] All 15+ shortcuts defined in CTAD array
- [ ] Home grid shows correct shortcuts (configurable)
- [ ] Apps grid shows all shortcuts
- [ ] Keyboard shortcuts work in Home
- [ ] `openApp()` launches correct activity for each ID
- [ ] Favorites launches Library with FAVORITES filter
- [ ] Null factory doesn't crash
- [ ] No ghost entries in array (size = definitions count)

---

## 6. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-LUA.md` — Plugins shortcut
- `STEROIDS-ADDICTIONS-WIKIPEDIA.md` — Wikipedia shortcut
- `STEROIDS-ADDICTIONS-SCREENSAVER.md` — Screensaver shortcut
- `STEROIDS-ADDICTIONS-QUICK-CARDS.md` — Quick Cards shortcut

---

*Last updated: 2026-09-28 | Commit: a103e3fe (CTAD registry + complete shortcuts)*