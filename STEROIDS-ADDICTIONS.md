# CPR-vCodex Steroids — Complete Addons Index

> **Purpose:** This document catalogs all Steroids-specific features added on top of upstream vcodex. Each feature has a dedicated technical reference document for upstream alignment/merging decisions.

---

## Feature Catalog

| # | Feature | Document | Status | Upstream Merge Risk |
|---|---------|----------|--------|---------------------|
| 1 | **Library V3** | `STEROIDS-ADDICTIONS-LIBRARY.md` | ✅ Complete | High (large diff, protected) |
| 2 | **Reading Statistics (Streaming)** | `STEROIDS-ADDICTIONS-READING-STATS.md` | ✅ Complete | Medium |
| 3 | **Fast Restart (Standardized)** | `STEROIDS-ADDICTIONS-FAST-RESTART.md` | ✅ Complete | Low-Medium |
| 4 | **Home Themes (Lyra Marcoand75)** | `STEROIDS-ADDICTIONS-HOME-THEMES.md` | ✅ Complete | Medium |
| 5 | **Steroids Branding & Web UI** | `STEROIDS-ADDICTIONS-BRANDING.md` | ✅ Complete | Low (mostly assets) |
| 6 | **Lua Plugin System** | `STEROIDS-ADDICTIONS-LUA.md` | ✅ Complete | High (VM + new activity) |
| 7 | **Wikipedia Offline** | `STEROIDS-ADDICTIONS-WIKIPEDIA.md` | ✅ Complete | Medium |
| 8 | **Sleep Screen & Power Button** | `STEROIDS-ADDICTIONS-SLEEP.md` | ✅ Complete | Low-Medium |
| 10 | **Screensaver** | `STEROIDS-ADDICTIONS-SCREENSAVER.md` | ✅ Complete | Medium |
| 11 | **Quick Cards** | `STEROIDS-ADDICTIONS-QUICK-CARDS.md` | ✅ Complete | Low |
| 12 | **Cover Generation Improvements** | `STEROIDS-ADDICTIONS-COVER-GEN.md` | ✅ Complete | Low-Medium |
| 13 | **Settings Extensions** | `STEROIDS-ADDICTIONS-SETTINGS.md` | ✅ Complete | Low |
| 14 | **i18n Extensions** | `STEROIDS-ADDICTIONS-I18N.md` | ✅ Complete | Low |

---

## Upstream Alignment Strategy

### Protected Modules (Do Not Merge Blindly)
- `LibraryIndex.*` — Complete rewrite, V3 architecture
- `LibraryActivity.*` — State persistence, mixed view, frame cache
- `ReadingStatsStore.*` — Streaming loader, binary journal, lazy loading
- `LuaPlugin.*` — Entire VM subsystem

### Safe to Cherry-Pick
- Cover generation fixes (deletion handling, retry logic)
- Sleep screen power button cycling
- Settings API optimization (batched JSON)
- Home panel cache invalidation fixes
- i18n string additions

### Branding (Never Merge Upstream)
- Logo assets, boot/sleep text
- OTA URL changes (marcoand75/cpr-vcodex-steroids)
- Web UI rebranding (HomePage, SteroidsSettingsPage)

---

## Key Technical Constraints (All Features)

1. **RAM Budget:** ESP32-C3 = 320 KB usable, no PSRAM
2. **No Full JSON in RAM:** Streaming parsers only (1 KB buffer)
3. **No Heap Allocation in Hot Paths:** Render loop, input loop, parser
4. **Flash Budget:** < 6.5 MB (82% used currently)
5. **OTA/SD Update Path:** Must preserve running slot, settings, recovery

---

## Document Template (Each Feature Doc Contains)

1. **User-Facing Functionality** — What the user sees/does
2. **Technical Architecture** — Data structures, algorithms, RAM/Flash usage
3. **File Inventory** — Every modified/new file with role
4. **Integration Points** — How it hooks into upstream code
5. **Upstream Merge Notes** — Conflicts, protected areas, safe cherry-picks
6. **Testing/Validation** — Device test scenarios, RAM/Flash metrics

---

*Generated: 2026-09-28 | Branch: integration/steroids-vcodex | Base: upstream vcodex 1.6.0.37*