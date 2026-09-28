# CPR-vCodex Steroids — Release Notes

## Rebase & Architecture Update

This release is based on **CPR-vCodex 1.6.0.38** after a full rebase. All Steroids-specific code has been rewritten and integrated against the upstream baseline. The branch topology:

- `master` — Latest Steroids + upstream vcodex integration
- `steroids-before-upstream-vcodex-1.6.0.38` — Pre-rebase historical reference (~670 commits)

### What Changed in This Rebase

- All Steroids implementations were **rewritten from scratch** to align with the new upstream architecture
- Some features may not be fully operational yet — this is a work in progress
- **Notable improvements:** Library speed, Series/Collections management, Filter system
- The device remains stable for daily reading; only new/modified features may need refinement

---

## Feature Overview for Users

### 📚 Library (V3) — Major Overhaul

The library has been completely redesigned for speed and flexibility.

**What you get:**
- **Three view modes:** Flat (all books), Collections (series/grid), Mixed (series tiles + books interleaved)
- **Auto Series Detection:** Books organized into series automatically from EPUB metadata (`calibre:series`) or folder names
- **User Collections:** Create and manage your own book collections via long-press menu
- **Natural Sort:** "Book 2" appears before "Book 10" — not alphabetical chaos
- **Full-Text Search:** Search by title + author across all views, including inside series
- **Smart Filters:** All / Favourites / Latest Read / Unread / Completed / Hidden
- **Cover Generation:** On-demand thumbnails for EPUB, XTC, and TXT files
- **Page Frame Cache:** Instant reload when revisiting books (0.1s vs 2-4s)
- **State Persistence:** Remembers your view mode, filter, sort, search, and scroll position across reboots

**Performance:** ~11 KB RAM budget, never loads full dataset into memory

---

### 📖 Reading Statistics (Streaming)

Track your reading with minimal RAM impact.

**What you get:**
- **Reading Streak:** Consecutive days meeting your daily goal
- **Daily Goal:** Configurable target (default 15 min)
- **Per-Book Stats:** Total time, sessions, progress %, chapters read
- **Achievements:** Milestone badges for books finished, streak days, total hours
- **Manual Correction:** Edit reading time per day directly in the app
- **Import/Export:** JSON files streamed — works even with 50 KB+ libraries without crashing

**Storage:**
- `reading_stats.json` — Full stats (~51 KB typical)
- `reading_stats_summary.json` — Fast path for Home/UI (~6 KB)
- `reading_sessions.jrn` — Binary journal for background sessions

---

### ⚡ Fast Restart

Instant return to Home — no logos, no waiting.

**What you get:**
- Exit Library/Reader/Settings/Apps → seamless restart to Home
- No boot logos, heap defragmented automatically
- Works via RTC silent boot (deep sleep wake)

---

### 🏠 Home Screen (Lyra Marcoand75 Theme)

A rich carousel interface with real-time data panels.

**What you get:**
- **Carousel Navigation:** Center cover + left/right previews, swipe or buttons
- **Data Panels:** Progress bar, reading time, session count, streak badge per book
- **Recent Books:** Up to 10 most recently read
- **Shortcuts Grid:** 7-slot pageable shortcuts (Apps, Settings, Quick Cards, etc.)
- **Sleep Screen Preview:** Your current book cover shown on sleep screen

---

### 🎴 Quick Cards (QR/Barcode/Image Viewer)

Manage and display cards stored on your SD card.

**What you get:**
- **Directory:** Store cards in `/cards/` on SD root
- **Image Cards:** JPEG/PNG auto-converted to BMP for fast e-ink display
- **QR Codes:** Text → QR code rendering (auto version selection)
- **Barcodes:** Code-128 barcode generation
- **Structured Parsing:** Auto-detects WiFi, vCard, MeCard, Geo, mailto, tel, SMS, OTP/2FA, iCal events, URLs
- **Navigation:** Previous/next through cards, fullscreen toggle
- **Create/Delete:** In-screen text entry for new cards, delete with cache cleanup

---

### 🧩 Lua Plugin System

Extend the reader with custom plugins.

**What you get:**
- **Plugin Browser:** Lists `.lua` files from `/.crosspoint/plugins/`
- **Sandboxed API:** Safe access to display, input, storage, network, books, stats
- **Built-in Examples:** Calculator, notes, clock plugins included
- **Shortcut:** Accessible from Home grid and Apps menu

---

### 📰 Wikipedia Offline

Read Wikipedia articles without an internet connection.

**What you get:**
- **Article Cache:** Downloaded ZIM/HTML articles stored on SD
- **Full-Text Search:** Search across all cached articles
- **Offline Reading:** No network required after download
- **Image Support:** Extracted images rendered alongside text
- **Shortcut:** Accessible from Home grid and Apps menu

---

### 🌙 Sleep Screen

Customize what appears when the device sleeps.

**What you get:**
- **Modes:** Off, Clock, Cover, Clock+Cover, Custom Image
- **Cover Options:** Current book, recent book, or random
- **Cover Filters:** Grayscale, inverted, sepia, or none
- **Power Button Short Press:** Cycle through sleep modes
- **Power Button Long Press:** Sleep or power off (configurable)

---

### 🖥️ Screensaver

Animated idle display, separate from sleep screen.

**What you get:**
- **Activation:** Starts after configurable idle timeout
- **Animation Types:** Clock, bouncing logo (DVD-style), image slideshow, custom
- **Exit:** Any button or touch returns to previous activity immediately
- **Settings:** Timeout, animation type, custom image folder

---

### 🎨 Branding & Web UI

Your device, your identity.

**What you get:**
- **Boot Logo:** Custom 350×96 "CPR-vCodex Steroids" branding
- **Web Interface:** Browser-based pages for Settings, Flash, and Stats Editor
- **OTA Updates:** Points to `marcoand75/cpr-vcodex-steroids` releases
- **Web Settings Page:** Full control of Library, Sleep, Power Button, Screensaver via browser
- **Stats Editor:** Browser-based JSON editor for reading statistics at `/stats-editor`

---

### 📝 Settings Extensions

More control over your reading experience.

**What you get:**
- **Library Settings:** Layout, filter, sort, view mode, update mode, root directory
- **Sleep Settings:** Mode, cover mode, cover filter, clean refresh toggle
- **Power Button Settings:** Short press and long press actions
- **Screensaver Settings:** Enabled, timeout, type, custom path
- **Reading Stats Settings:** Daily goal, import/export, manual correction
- **Web API:** Batched JSON response (<1s vs 50s before)

---

### 🌐 i18n (24 Languages)

Full localization support.

**What you get:**
- Steroids strings added to all 24 language files
- Language persists as ISO code (e.g., `"language": "IT"`)
- Default to English if not configured

---

### ⌨️ Shortcuts Registry (CTAD)

Quick access to all features.

**What you get:**
- 15 shortcuts: Library, Settings, Apps, Favorites, Flashcards, Dictionary, File Transfer, Sleep, Quick Cards, Wikipedia, Plugins, Screensaver, Reading Stats, Bookmarks, Recent Books
- Null guard prevents crashes from missing shortcuts

---

## Build Instructions

```powershell
# Build default (X4/X3 ESP32-C3)
.\bin\build-vcodex.ps1

# Build release (gh_release)
.\bin\build-vcodex.ps1 -Environment gh_release
```

**Artifacts:** `artifacts/1.6.0.38.dev<N>-<sha>-cpr-vcodex.bin`  
**Flash:** [Auto Flash page](https://marcoand75.github.io/cpr-vcodex-steroids/flash.html) (X4 default, X4 Pro blocked)  
**OTA:** `https://github.com/marcoand75/cpr-vcodex-steroids/releases`

---

## Documentation

Full technical documentation available in the repository:

| Document | Description |
|----------|-------------|
| `STEROIDS-ADDICTIONS-LIBRARY.md` | Library V3 complete spec |
| `STEROIDS-ADDICTIONS-READING-STATS.md` | Streaming stats architecture |
| `STEROIDS-ADDICTIONS-FAST-RESTART.md` | Fast restart implementation |
| `STEROIDS-ADDICTIONS-HOME-THEMES.md` | Lyra Marcoand75 theme |
| `STEROIDS-ADDICTIONS-BRANDING.md` | Branding and web UI |
| `STEROIDS-ADDICTIONS-QUICK-CARDS.md` | Quick cards (QR/barcode) |
| `STEROIDS-ADDICTIONS-COVER-GEN.md` | Cover generation fixes |
| `STEROIDS-ADDICTIONS-SLEEP.md` | Sleep screen implementation |
| `STEROIDS-ADDICTIONS-SCREENSAVER.md` | Screensaver implementation |
| `STEROIDS-ADDICTIONS-LUA.md` | Lua plugin system |
| `STEROIDS-ADDICTIONS-WIKIPEDIA.md` | Wikipedia offline |
| `STEROIDS-ADDICTIONS-SETTINGS.md` | Settings extensions |
| `STEROIDS-ADDICTIONS-I18N.md` | i18n extensions |
| `STEROIDS-ADDICTIONS-SHORTCUTS.md` | Shortcuts registry |
| `STEROIDS-ALIGN-TO-UPSTREAM.md` | Upstream merge strategy |

---

*Last updated: 2026-09-28 | Version: 1.6.0.38-steroids | Branch: master*
