# CPR-vCodex Steroids — Wikipedia App

> **SCOPE:** Complete technical reference for the Wikipedia article download/read subsystem.

---

## 1. Architecture Overview

The Wikipedia app is split into two activities:
- **`WikipediaActivity`** — Search, cache management, summary preview, download
- **`WikiTxtReaderActivity`** — Dedicated reader for cached articles

---

## 2. Cache Layout

```
/.crosspoint/wikipedia-cache/
└── wiki_<FNVhash>/
    ├── article.md       # Markdown body
    ├── title.txt        # Real display title
    ├── index.bin        # Page-index cache (settings-validated)
    └── progress.bin     # Last read page + byte offset
```

**Legacy format:** `.wiki` flat files (removed when newer folder exists)

---

## 3. Download Flow (`fetchFullArticle`)

### 3.1 HTTP Request
```cpp
GET https://it.wikipedia.org/w/api.php?action=parse&page=<TITLE>&prop=wikitext&format=json
```
- Returns wikitext JSON (not mobile-html REST API)
- Streams to `wiki_<hash>/raw.json` on SD (no full-RAM copy)

### 3.2 TLS Lifetime Management (Critical)
```cpp
// HTTPClient + NetworkClientSecure held in dedicated scope
// Destroyed in CORRECT ORDER before conversion:
// 1. httpClient.reset() — WRONG! Causes double-free
// 2. Let scope exit naturally — destructors run in correct order
```

### 3.3 Wikitext → Markdown Conversion
```cpp
// WikitextToMarkdown streams JSON, scans "wikitext" → "*" field
// On-the-fly JSON escape decoding
// Writes wiki_<hash>/article.md as markdown:
//   '''...''' / ''...'' → **...** / *...*
//   ==H== → # H
//   [[X|Y]] → display text
//   {{templates}}, <ref>, HTML comments, infoboxes stripped
```

### 3.4 Post-Download
- Write `title.txt` with real title
- `loadCachedPages()` refreshes list
- **No reading-stats reload** (`reloadReadingStats=false`) — heap too fragmented after streaming

---

## 4. Reading Flow

### 4.1 Summary Preview
```cpp
GET https://it.wikipedia.org/api/rest_v1/page/summary/...
```
- Rendered in-app via `renderArticle()` (plain text)

### 4.2 Full Article
- **From cache** if available (no network)
- **Else** fallback to `fetchFullArticle()`
- **Reopen cached:** `loadCachedArticle()` → `openArticleForReading()`

### 4.3 Cached Article Management
- **List state:** `State::CACHED_PAGES` shows titles from `title.txt`
- **Long-press delete:** `ConfirmationActivity` → `Storage.removeDir()` on `wiki_<hash>`
- **Home cleanup:** Clears `searchInput`/`currentQuery`/`errorMessage` on reader close

---

## 5. WikiTxtReaderActivity (Dedicated Reader)

Shares rendering with `TxtReaderActivity` but **without** book-reader side effects:

| Feature | Implementation |
|---------|----------------|
| Markdown parsing | `**bold**`, `*italic*`, `#` headings, `-`/`1.` lists, `>` blockquotes |
| Page index | RAM + `index.bin` cache (font/margin/lines/viewport validated) |
| File reading | Chunked (`loadPageAtOffset`) with span-aware wrapping |
| Rendering | Two-pass prewarm + status bar (`Pag. N/M`) |
| Progress | `progress.bin` (page + byte offset), saved on render + exit |
| RTL | Line-level via `BidiUtils` |
| No-op features | Reading stats, achievements, recent books, completed-book mover, orientation |

---

## 5.1 Interface Requirements
```cpp
// Must implement for framework hooks:
getScreenshotInfo()   // For screenshots
isReaderActivity()    // True for reader-related hooks
```

---

## 6. Key Files

| File | Role |
|------|------|
| `src/activities/apps/WikipediaActivity.h/cpp` | Search, download, cache, summary |
| `src/activities/reader/WikiTxtReaderActivity.h/cpp` | Article reader |
| `src/util/WikitextToMarkdown.h/cpp` | Wikitext → Markdown conversion |
| `src/util/MarkdownReader.h/cpp` | Markdown rendering (shared) |
| `lib/hal/HalStorage.h/cpp` | SD file operations |

---

## 7. Plumbing

- `SDCardManager::listFiles()` gained `includeDirectories` option (default `false`)
- `freeink-sdk` for hardware abstraction

---

## 8. Known Constraints

| Constraint | Value |
|------------|-------|
| Max article size (RAM) | Large articles streamed from SD |
| Cache location | `/.crosspoint/wikipedia-cache/` |
| Language | Italian Wikipedia (hardcoded) |
| HTTP | TLS 1.2, certificate validation |
| Memory during download | ~46 KB free / ~20 KB maxAlloc |

---

## 9. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (WikipediaActivity protected)
- **Lua Plugins:** `STEROIDS-ADDICTIONS-LUA.md` (unrelated)

---

*Last updated: 2026-09-14*