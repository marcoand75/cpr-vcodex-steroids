# CPR-vCodex Steroids — Dictionary App

> **SCOPE:** Complete technical reference for the multi-dictionary lookup subsystem.

---

## 1. Architecture Overview

```
DictionaryActivity (main)
    │
    ├─ Lookup modes: Auto (failover) / Manual
    ├─ Reorderable active dictionary list (stored order)
    ├─ Orphan IFO cleanup on delete
    ├─ Word-selection reading-progress indicator
    └─ Configurable lookup order
```

---

## 2. Features

| Feature | Implementation |
|---------|----------------|
| Multi-dictionary | Multiple `.ifo`/`.dict`/`.idx` sets on SD |
| Lookup modes | `AUTO` (failover chain) / `MANUAL` (user picks) |
| Dictionary order | Drag-reorder in settings (persisted) |
| Orphan cleanup | Deleting dictionary removes orphan `.ifo` files |
| Word selection | Shows reading progress % in lookup overlay |
| Failover | Tries next dictionary if word not found |

---

## 3. Dictionary Format

Standard StarDict format:
```
/custom/dicts/
├── dict1/
│   ├── dict1.ifo    # Metadata
│   ├── dict1.idx    # Word index
│   └── dict1.dict   # Definitions (optional .dict.dz compressed)
└── dict2/
    ...
```

---

## 4. Lookup Flow

### 4.1 Auto Mode (Failover)
```cpp
for (dict in activeDictionaries) {
    result = dict.lookup(word);
    if (result.found) return result;
}
// Not found in any
```

### 4.2 Manual Mode
```cpp
// User selects dictionary from list → single lookup
result = selectedDict.lookup(word);
```

### 4.3 Word Selection Overlay
- Shows word + reading progress % (from ReadingStatsStore)
- Long-press on word → lookup
- Progress indicator: "Reading: 45%"

---

## 5. Settings Persistence

```cpp
// CrossPointSettings
dictionaryLookupMode        // 0=AUTO, 1=MANUAL
dictionaryOrder[]           // Vector of dict IDs (reorderable)
dictionaryActive[]          // Enabled/disabled per dict
```

---

## 6. Key Files

| File | Role |
|------|------|
| `src/activities/apps/DictionaryActivity.h/cpp` | Main dictionary UI |
| `src/activities/settings/ButtonActionSelectorActivity.h/cpp` | Popup selector for actions |
| `lib/StarDict/StarDictParser.h/cpp` | StarDict format parser |
| `src/CrossPointSettings.h` | Dictionary settings fields |

---

## 7. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (DictionaryActivity protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` (unrelated)
- **Lua Plugins:** `STEROIDS-ADDICTIONS-LUA.md` (unrelated)

---

*Last updated: 2026-09-14*