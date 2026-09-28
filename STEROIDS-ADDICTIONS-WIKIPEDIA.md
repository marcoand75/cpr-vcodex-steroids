# CPR-vCodex Steroids — Wikipedia Offline

> **SCOPE:** Offline Wikipedia reader with cache, search, article rendering. Medium merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Article Cache** | Downloaded ZIM/HTML articles stored on SD |
| **Search** | Full-text search across cached articles |
| **Offline Reading** | No network required after download |
| **Wikipedia Shortcut** | Home/Apps shortcut → WikipediaActivity |
| **Article Rendering** | HTML → formatted text with images |

---

## 2. Technical Architecture

### 2.1 Cache Structure
```
~/.crosspoint/wikipedia/
├── index.json          # Article metadata (title, path, size)
├── articles/
│   ├── <hash>.html     # Cached article content
│   └── <hash>_img/     # Extracted images
└── search_index.bin    # Inverted index for search
```

### 2.2 WikipediaCacheUtils
```cpp
// src/util/WikipediaCacheUtils.h
class WikipediaCacheUtils {
    // Similar to SleepImageUtils — directory-based listing
    static bool scanCache(std::vector<WikiPage>& out);
    static bool loadArticle(const std::string& path, WikiArticle& out);
    static bool search(const std::string& query, std::vector<WikiPage>& results);
};
```

### 2.3 Article Rendering
```cpp
// WikipediaActivity.cpp
// 1. Load HTML from cache
// 2. Parse with lightweight HTML parser (subset)
// 3. Render: headings, paragraphs, links, images
// 4. Images: decode from article_img/ subdir → display
// 5. Links: tap → load linked article (if cached)
```

### 2.4 Search
```cpp
// Inverted index (built on cache scan)
// - Tokenize titles + content
// - Store: term → list of article hashes
// - Search: intersect term postings
// - Rank: title matches > content matches
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/activities/apps/WikipediaActivity.h/cpp` | Main activity, rendering, navigation |
| `src/util/WikipediaCacheUtils.h/cpp` | Cache scanning, article loading, search index |
| `src/activities/apps/AppsActivity.cpp` | "Wikipedia" shortcut |
| `src/activities/home/HomeActivity.cpp` | "Wikipedia" shortcut in Home grid |
| `src/ShortcutRegistry.h` | Shortcut definition |

---

## 4. RAM/Flash Impact

| Metric | Value |
|--------|-------|
| Flash | +~50 KB (HTML parser + activity) |
| RAM (idle) | Minimal |
| RAM (rendering article) | ~20-40 KB (HTML + decoded images) |
| SD Cache | User-dependent (10 MB - 1 GB) |

---

## 5. Upstream Merge Notes

### MEDIUM RISK
- `WikipediaActivity` — New activity
- `WikipediaCacheUtils` — New utility
- Shortcut registrations in Apps/Home

### Conflicts Likely
- `AppsActivity` / `HomeActivity` shortcut additions
- SD card cache directory structure

### Safe Cherry-Picks
- Cache scanning pattern (directory-based, like SleepImageUtils)
- Lightweight HTML rendering approach

---

## 6. Validation Checklist

- [ ] Wikipedia shortcut appears in Apps/Home
- [ ] Cache scan finds articles in `/.crosspoint/wikipedia/`
- [ ] Article list renders with titles
- [ ] Tap article → renders formatted text
- [ ] Images in article display correctly
- [ ] Search finds articles by title/content
- [ ] Back navigation works (article → list → home)
- [ ] Fast restart from Wikipedia → Home seamless

---

## 7. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — Wikipedia fast restart
- `STEROIDS-ADDICTIONS-SHORTCUTS.md` — Shortcut registration

---

*Last updated: 2026-09-28 | Commit: dd677063 (add wikipedia app)*