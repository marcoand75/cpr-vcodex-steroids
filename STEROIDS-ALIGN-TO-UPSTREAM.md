# CPR-vCodex Steroids — Upstream Alignment Guide

> **PURPOSE:** Decision framework for merging upstream vcodex changes into steroids branch, and vice versa. Each feature document has a "Upstream Merge Notes" section — this is the master strategy.

---

## 1. Branch Topology

```
upstream/main (vcodex)
    │
    ├── integration/steroids-vcodex (this branch)
    │       ├── Library V3 (protected)
    │       ├── Reading Stats Streaming (protected)
    │       ├── Fast Restart (protected)
    │       ├── Lua Plugin System (protected)
    │       ├── Home Themes (protected)
    │       ├── Branding (NEVER MERGE)
    │       ├── Wikipedia (medium)
    │       ├── Sleep/Screensaver (low-medium)
    │       ├── Quick Cards (low)
    │       ├── Cover Gen Fixes (low-medium)
    │       ├── Settings Extensions (low)
    │       ├── i18n Extensions (low)
    │       └── Shortcuts (low)
    │
    └── Other forks: CrossInk, crosspet, papyrix
```

---

## 2. Merge Direction Strategy

### 2.1 Upstream → Steroids (Regular)
**Frequency:** Monthly or per upstream release
**Method:** `git merge upstream/main` → resolve conflicts
**Protected Files:** Never auto-resolve; manual review required

| Protected Module | Conflict Resolution |
|------------------|---------------------|
| `LibraryIndex.*` | Keep steroids V3; upstream changes → analyze & port selectively |
| `LibraryActivity.*` | Keep steroids state persistence; upstream UI → port selectively |
| `ReadingStatsStore.*` | Keep streaming + journal; upstream fixes → port to streaming |
| `LuaPlugin.*` | Keep entire subsystem; upstream has no equivalent |
| `HomeActivity/LyraMarcoand75Theme` | Keep steroids theme; upstream home → analyze |
| `Activity/ActivityManager` | Keep fast-restart virtual method; upstream changes → extend |

### 2.2 Steroids → Upstream (Selective)
**Frequency:** Per feature, when stable
**Method:** Cherry-pick individual commits/PRs
**Never Push:** Branding, OTA URLs, Logo assets

---

## 3. Protected Modules — Detailed Rules

### 3.1 Library V3 (`STEROIDS-ADDICTIONS-LIBRARY.md`)
```
UPSTREAM CHANGES TO WATCH:
- LibraryIndex scan algorithm improvements
- Cover generation fixes
- EPUB parser updates
- New file format support

MERGE RULE:
- Upstream scan → compare with steroids incremental scan
- Upstream cover fixes → cherry-pick (high value)
- Upstream parser → port to steroids EpubParser
- NEVER accept upstream flat library → steroids is V3 mixed view
```

### 3.2 Reading Stats Streaming (`STEROIDS-ADDICTIONS-READING-STATS.md`)
```
UPSTREAM CHANGES TO WATCH:
- Stats UI improvements
- New metrics/achievements
- Import/export format changes
- Web editor updates

MERGE RULE:
- Upstream UI → port to steroids activities
- New metrics → add to SummaryJSON + streaming loader
- Format changes → update formatVersion + loader
- NEVER accept full JSON load → steroids is streaming-only
```

### 3.3 Fast Restart (`STEROIDS-ADDICTIONS-FAST-RESTART.md`)
```
UPSTREAM CHANGES TO WATCH:
- Activity lifecycle changes
- New activity types
- Deep sleep / wake improvements

MERGE RULE:
- Upstream activity changes → add exitRestartPlan() override
- New activities → implement exitRestartPlan() (default: {})
- NEVER remove virtual method or ActivityManager coordination
```

### 3.4 Lua Plugin System (`STEROIDS-ADDICTIONS-LUA.md`)
```
UPSTREAM CHANGES TO WATCH:
- None (upstream has no Lua)

MERGE RULE:
- Isolated subsystem — no upstream conflicts expected
- Monitor Lua version updates (5.4.x)
```

---

## 4. Safe Cherry-Pick Candidates (Upstream → Steroids)

| Area | Upstream Commit Pattern | Steroids Target |
|------|------------------------|-----------------|
| Cover generation | Fix corrupt BMP, retry logic | `CoverGenerator.cpp` |
| EPUB parser | New metadata fields, robustness | `EpubParser.cpp` |
| Font system | CJK improvements, loading fixes | `SdCardFontSystem.cpp` |
| Network/OTA | Reliability, timeout fixes | `CrossPointWebServer.cpp` |
| Input handling | Touch/button edge cases | `InputManager.cpp` |
| Power management | Deep sleep, battery calibration | `PowerManager.cpp` |
| Renderer | Partial refresh, dithering | `GfxRenderer.cpp` |

---

## 5. Conflict Resolution Workflow

```bash
# 1. Fetch upstream
git fetch upstream

# 2. Merge (will conflict on protected files)
git merge upstream/main

# 3. For each conflicted protected file:
#    a. Keep steroids version as base
#    b. Apply upstream fixes manually
#    c. Run device tests
#    d. Commit resolution

# 4. For non-protected files:
#    - Accept upstream if no steroids customization
#    - Manual merge if steroids has extensions

# 5. Full device validation
pio run -e default -t upload
# Test: boot, library, reader, stats, settings, sleep, web UI
```

---

## 6. Release Checklist (Pre-Release)

- [ ] All protected modules pass device tests
- [ ] RAM/Flash within budget (RAM < 80%, Flash < 90%)
- [ ] OTA update path works (X3/X4)
- [ ] Auto-flash from latest GitHub release (X4 only)
- [ ] Web UI: HomePage, Flash, Settings, Stats Editor
- [ ] All 24 languages complete
- [ ] Steroids branding visible (boot, sleep, web)
- [ ] No X4 Pro artifacts generated
- [ ] Git tag: `1.6.0.38-steroids.<N>`

---

## 7. Feature Status Matrix

| Feature | Upstream Equivalent | Merge Status | Next Sync |
|---------|---------------------|--------------|-----------|
| Library V3 | Library V1 | Protected — Diverged | Manual |
| Reading Stats Streaming | Full JSON load | Protected — Diverged | Manual |
| Fast Restart | None | Protected — New API | Manual |
| Lua Plugins | None | Protected — New Subsystem | N/A |
| Home Themes | Lyra V1 | Protected — Diverged | Manual |
| Branding | vcodex branding | NEVER MERGE | N/A |
| Wikipedia | None | Medium — Isolated | Cherry-pick |
| Sleep Screen | Basic sleep | Low-Medium — Extended | Selective |
| Screensaver | None | Medium — New | Cherry-pick |
| Quick Cards | None | Low — Isolated | Cherry-pick |
| Cover Gen Fixes | Basic cover gen | Low-Medium — Fixes | Cherry-pick |
| Settings Extensions | Basic settings | Low — Additive | Selective |
| i18n Extensions | 24 languages | Low — Additive | Auto |
| Shortcuts | Basic shortcuts | Low — Extended | Selective |

---

## 8. Key Documents Reference

| Document | Purpose |
|----------|---------|
| `STEROIDS-ADDICTIONS.md` | Main index |
| `STEROIDS-ADDICTIONS-LIBRARY.md` | Library V3 complete spec |
| `STEROIDS-ADDICTIONS-READING-STATS.md` | Streaming stats complete spec |
| `STEROIDS-ADDICTIONS-FAST-RESTART.md` | Fast restart complete spec |
| `STEROIDS-ADDICTIONS-HOME-THEMES.md` | Home themes complete spec |
| `STEROIDS-ADDICTIONS-BRANDING.md` | Branding (never merge) |
| `STEROIDS-ADDICTIONS-LUA.md` | Lua plugin system |
| `STEROIDS-ADDICTIONS-WIKIPEDIA.md` | Wikipedia offline |
| `STEROIDS-ADDICTIONS-SLEEP.md` | Sleep screen |
| `STEROIDS-ADDICTIONS-SCREENSAVER.md` | Screensaver |
| `STEROIDS-ADDICTIONS-QUICK-CARDS.md` | Quick cards |
| `STEROIDS-ADDICTIONS-COVER-GEN.md` | Cover generation fixes |
| `STEROIDS-ADDICTIONS-SETTINGS.md` | Settings extensions |
| `STEROIDS-ADDICTIONS-I18N.md` | i18n extensions |
| `STEROIDS-ADDICTIONS-SHORTCUTS.md` | Shortcuts registry |

---

## 9. README.md Update Workflow (During Upstream Merge)

### 9.1 What Must Be Preserved (Steroids Section)
The `README.md` contains a **"CPR-vCodex Steroids"** section (after the main CPR-vCodex content) that must survive upstream merges. This section documents:
- All Steroids features (table with 14 entries)
- Screenshots reference
- Build instructions (steroids-specific)
- Artifact naming
- Documentation links
- Upstream alignment summary

### 9.2 Historical Note: Branch Rebase on Upstream vcodex 1.6.0.38

**Date:** 2026-09-28  
**Event:** `master` branch rebased onto upstream CPR-vCodex 1.6.0.38

Prior to this operation, two divergent branches existed:
- `origin/master` — contained ~670 steroid-specific commits (pre-rebase work)
- `integration/steroids-vcodex` — contained the latest steroid feature work (660+ commits)

Both branches shared a common ancestor but had diverged in their commit histories. The steroids feature work in `integration/steroids-vcodex` was force-pushed to `origin/master`, effectively making it the new canonical master.

**To preserve the old master history:**
```bash
git branch steroids-before-upstream-vcodex-1.6.0.38 8325282b
```

This branch points to the old master (`8325282b`) and contains all commits that were not part of `integration/steroids-vcodex`. It serves as a historical record of the pre-rebase state.

**Current branch topology:**
```
origin/master              → 5dc78a0f (latest steroids + upstream integration)
origin/integration/steroids-vcodex → 5dc78a0f (same as master)
steroids-before-upstream-vcodex-1.6.0.38 → 8325282b (old master, preserved)
```

When performing future upstream merges, always target `master` and use the workflow in §9.3.

### 9.2 Merge Conflict Resolution for README.md
```bash
# During upstream merge, README.md will conflict heavily.
# Resolution strategy:

# 1. Keep steroids section as a block (lines after "---" separator at end of CPR-vCodex content)
# 2. Accept upstream changes to CPR-vCodex section (top portion)
# 3. Manually re-apply steroids section if lost

# If upstream rewrote entire README structure:
# - Copy steroids section to temp file
# - Accept upstream README
# - Append steroids section at end (before final disclaimer)
```

### 9.3 Steroids Section Template (Keep This Block)
```markdown
---

# CPR-vCodex Steroids

> **This branch (`integration/steroids-vcodex`)** extends CPR-vCodex with additional UI features, branding, and experimental subsystems. It is a personal fork by **marcoand75** targeting the Xteink X4.

<p align="center">
  <img src="./docs/logo.png" alt="CPR-vCodex Steroids logo" width="350" />
  <br />
  <sub>Steroids branding: "CPR-vCodex Steroids" on boot, sleep, and web UI</sub>
</p>

## Steroids Feature Additions

| Feature | Description | Status |
|---------|-------------|--------|
| **Steroids Branding** | Custom 350×96 logo on boot/sleep; web UI (HomePage, Flash, Settings, Stats Editor); OTA points to `marcoand75/cpr-vcodex-steroids` | ✅ Complete |
| **Library V3** | Mixed view (series tiles + standalone books); user collections (manual CRUD); auto series from EPUB `calibre:series` + folder fallback; case-insensitive natural sort; page frame cache | ✅ Complete |
| **Reading Statistics (Streaming)** | Incremental JSON loader (1 KB buffer); binary journal (`reading_sessions.jrn`, 32 B/record) for detached sessions; `summary.json` fast path (~6 KB) for Home/UI; import/export streaming (~20 KB peak RAM); lazy full-store load | ✅ Complete |
| **Fast Restart (Standardized)** | Declarative `exitRestartPlan()` per activity; `ActivityManager::exitWithFastRestart()`; RTC silent boot to Home; Reader/Library/Settings/Apps all seamless | ✅ Complete |
| **Home Theme: Lyra Marcoand75** | Carousel with data panels (progress, time, sessions, reading days); frame cache; cover generation integration; summary.json fast path | ✅ Complete |
| **Lua Plugin System** | Lua 5.4 VM; plugin browser (`/.crosspoint/plugins/`); sandboxed `cpr` API (display, input, storage, books, stats); separate activity stack | ✅ Complete |
| **Wikipedia Offline** | ZIM/HTML article cache; full-text search; article rendering with images | ✅ Complete |
| **Sleep Screen & Power Button** | Modes: Off, Clock, Cover, Clock+Cover, Custom; cover filters (grayscale/inverted/sepia); short-press cycles mode; long-press sleep/power off | ✅ Complete |
| **Screensaver** | Idle-time animated: clock, bouncing logo, slideshow; separate from sleep screen; any input exits | ✅ Complete |
| **Quick Cards** | Flashcard app with SM-2 spaced repetition; CSV import; review modes; stats | ✅ Complete |
| **Cover Generation Fixes** | Corrupt BMP removal + retry; partial BMP protection during gen; cover refresh after delete; frame cache never stores placeholder | ✅ Complete |
| **Settings Extensions** | Library/Sleep/Power/Screensaver categories; web API batched JSON (<1s vs 50s); ISO code language persistence | ✅ Complete |
| **i18n (24 Languages)** | `STR_STEROIDS`, `STR_CPR_VCODEX_STEROIDS` added to all 24 language files | ✅ Complete |
| **Shortcuts Registry (CTAD)** | 15 shortcuts (Library, Settings, Apps, Favorites, Flashcards, Dictionary, File Transfer, Sleep, Quick Cards, Wikipedia, Plugins, Screensaver, Reading Stats, Bookmarks, Recent Books); null guard | ✅ Complete |

## Screenshots (Steroids)

<p align="center">
  <img src="./docs/images/screenshots.png" alt="CPR-vCodex Steroids overview" width="1000" />
</p>

## Steroids Build

```powershell
# Build default (X4/X3 ESP32-C3)
.\bin\build-vcodex.ps1

# Build release (gh_release)
.\bin\build-vcodex.ps1 -Environment gh_release
```

**Artifacts:** `artifacts/1.6.0.38.dev<N>-<sha>-cpr-vcodex.bin`  
**Flash:** [Auto Flash page](https://marcoand75.github.io/cpr-vcodex-steroids/flash.html) (X4 default, X4 Pro blocked)  
**OTA:** `https://github.com/marcoand75/cpr-vcodex-steroids/releases`

## Steroids Documentation

| Document | Purpose |
|----------|---------|
| `STEROIDS-ADDICTIONS.md` | Main index of all Steroids features |
| `STEROIDS-ADDICTIONS-LIBRARY.md` | Library V3 complete spec |
| `STEROIDS-ADDICTIONS-READING-STATS.md` | Streaming stats + journal + import |
| `STEROIDS-ADDICTIONS-FAST-RESTART.md` | Declarative fast restart |
| `STEROIDS-ADDICTIONS-HOME-THEMES.md` | Lyra Marcoand75 theme |
| `STEROIDS-ADDICTIONS-BRANDING.md` | Logo, web UI, OTA URLs |
| `STEROIDS-ADDICTIONS-LUA.md` | Lua plugin system |
| `STEROIDS-ADDICTIONS-WIKIPEDIA.md` | Wikipedia offline |
| `STEROIDS-ADDICTIONS-SLEEP.md` | Sleep screen + power button |
| `STEROIDS-ADDICTIONS-SCREENSAVER.md` | Screensaver |
| `STEROIDS-ADDICTIONS-QUICK-CARDS.md` | Flashcard SM-2 |
| `STEROIDS-ADDICTIONS-COVER-GEN.md` | Cover generation fixes |
| `STEROIDS-ADDICTIONS-SETTINGS.md` | Settings + web API |
| `STEROIDS-ADDICTIONS-I18N.md` | 24 languages |
| `STEROIDS-ADDICTIONS-SHORTCUTS.md` | CTAD shortcut registry |
| `STEROIDS-ALIGN-TO-UPSTREAM.md` | Upstream merge strategy |

---

## Upstream Alignment

See [`STEROIDS-ALIGN-TO-UPSTREAM.md`](./STEROIDS-ALIGN-TO-UPSTREAM.md) for the complete merge strategy. Key principles:

- **Protected (never auto-merge):** LibraryIndex, LibraryActivity, ReadingStatsStore, LuaPlugin, HomeActivity/LyraMarcoand75Theme, Activity/ActivityManager (fast restart)
- **Branding:** Never merge upstream (logo, OTA URLs, web UI text)
- **Safe cherry-picks:** Cover gen fixes, sleep power button, settings API batching, home panel cache fixes, i18n strings
- **README updates:** See merge workflow in `STEROIDS-ALIGN-TO-UPSTREAM.md` §9
```

### 9.4 Post-Merge Validation Checklist
- [ ] CPR-vCodex section (top) reflects upstream changes
- [ ] Steroids section (bottom) intact with all 14 features
- [ ] Logo image path correct (`./docs/logo.png`)
- [ ] Screenshots image path correct (`./docs/images/screenshots.png`)
- [ ] All doc links in table point to existing `.md` files
- [ ] OTA/Flash URLs point to `marcoand75/cpr-vcodex-steroids`
- [ ] No X4 Pro references in steroids section
- [ ] Version/build info matches current HEAD

---

*Last updated: 2026-09-28 | Branch: integration/steroids-vcodex | HEAD: 1ae55a93*