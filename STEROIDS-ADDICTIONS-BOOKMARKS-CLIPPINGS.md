# CPR-vCodex Steroids — Bookmarks & Clippings

> **SCOPE:** Complete technical reference for the bookmark and clipping (highlight) subsystems.

---

## 1. Architecture Overview

Both subsystems use **absolute word indices** for layout-independent positioning — the same word stays highlighted at the same screen position regardless of font size, margins, or orientation.

```
BookmarkStore (per-book)          ClippingStore (global)
       │                                │
       ▼                                ▼
bookdata/{bookId}/bookmarks.bin    /.crosspoint/clippings/epub_{hash}.bin
```

---

## 2. Storage

| Component | Format | Location |
|-----------|--------|----------|
| Bookmarks | Binary (v4) | `/.crosspoint/bookdata/{bookId}/bookmarks.bin` |
| Clippings | Binary (v2) | `/.crosspoint/clippings/epub_{hash}.bin` |

### 2.1 Stable Book Identity
`BookIdentity` — same book → same path regardless of SD path changes (hash of metadata).

---

## 3. Data Structures

### 3.1 Bookmark (32+ bytes, v4)
```cpp
struct Bookmark {
    uint16_t spineIndex;        // Chapter index
    uint16_t pageNumber;        // Legacy (v3 compat)
    char snippet[80];           // Text snippet (~80 chars)
    uint32_t absoluteWordStart; // **Layout-independent position (v4)**
};
```

### 3.2 Clipping (~560 bytes, v2)
```cpp
struct Clipping {
    uint16_t spineIndex;
    uint16_t startPage, endPage;
    uint32_t startWordIndex, endWordIndex;
    uint32_t wordCount;
    uint32_t absoluteWordStart; // **Layout-independent (v2)**
    uint32_t timestamp;
    char chapterTitle[48];
    char selectedText[512];     // Highlighted text
};
```

---

## 4. Layout-Independent Positioning

### 4.1 Core Algorithm
```cpp
// Section::buildCumulativeWordCounts()
// cumulativeWordCounts[page] = total words from chapter start to page start

// Storing:
bookmark.absoluteWordStart = cumulative[page] + wordIndexInPage;

// Restoring (on layout change):
for (page = 0; page < numPages; ++page) {
    if (cumulative[page] <= absoluteWordStart && absoluteWordStart < cumulative[page+1]) {
        wordIndex = absoluteWordStart - cumulative[page];
        // Found exact word position on new layout
    }
}
```

### 4.2 Cumulative Array
- ~2 bytes/page in RAM
- Built on first open (~2-5 s for long sections)
- Cached per section

---

## 5. Apps

### 5.1 `BookmarksAppActivity`
- Cross-book bookmark browser
- Loads recent books via `RecentBooksStore`
- Loads bookmarks from `bookdata/{bookId}/`
- Jumps to exact word position in reader

### 5.2 `ClippingsAppActivity`
- Cross-book clippings browser (`/.crosspoint/clippings/`)
- Delete individual or all clippings
- i18n: `STR_CLIPPINGS`, `STR_NO_CLIPPINGS`, `STR_DELETE_ALL_CLIPPINGS`

---

## 6. Reader Integration

### 6.1 Selection UI
- **Cursor word:** Inverted (black bg / white text)
- **Selected words:** Light-gray with readable text
- **Anti-aliasing compatible**

### 6.2 Long-Press Configuration
| Setting | Options |
|---------|---------|
| `frontLongPressBehavior` | Bookmark / Clipping / Chapter Skip / Orientation / Font Size |
| `longPressButtonBehavior` | Bookmark / Clipping / Chapter Skip / Orientation / Font Size / Dictionary / Dark Mode / Full Refresh / Quick Settings / Reading Timer / Off |

---

## 7. Performance

| Operation | Time |
|-----------|------|
| `buildCumulativeWordCounts()` | 2-5 s (first open, long section) |
| Cumulative array RAM | ~2 bytes/page |
| Bookmark lookup | O(1) via absolute word index |

---

## 8. Key Files

| File | Role |
|------|------|
| `src/activities/reader/BookmarkStore.h` | Bookmark persistence |
| `src/activities/reader/ClippingStore.h` | Clipping persistence |
| `src/activities/apps/BookmarksAppActivity.h/cpp` | Bookmark browser |
| `src/activities/apps/ClippingsAppActivity.h/cpp` | Clipping browser |
| `src/activities/reader/ClippingsActivity.h/cpp` | In-reader clipping UI |
| `src/activities/reader/EpubReaderActivity.cpp` | Reader integration |
| `lib/Epub/Epub/Section.cpp` | `buildCumulativeWordCounts()` |
| `src/util/BookIdentity.h/cpp` | Stable book hashing |

---

## 9. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (all bookmark/clipping files protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` (unrelated)
- **Library:** `STEROIDS-ADDICTIONS-LIBRARY.md` (unrelated)

---

*Last updated: 2026-09-14*