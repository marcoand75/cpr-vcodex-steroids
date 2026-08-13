<div align="center">

# CPR-vCodex Steroids

[![Board Support](https://img.shields.io/badge/Hardware-Xteink%20X4%20%7C%20X3-blue.svg)](https://github.com/malv-ryx/cpr-vcodex-steroids)
[![Upstream Base](https://img.shields.io/badge/Upstream%20Base-CPR--vCodex%201.5.0.9-brightgreen.svg)](https://github.com/franssjz/cpr-vcodex)
[![License](https://img.shields.io/badge/License-MIT-orange.svg)](./LICENSE)
[![Release](https://img.shields.io/github/v/release/marcoand75/cpr-vcodex-steroids?color=purple)](https://github.com/marcoand75/cpr-vcodex-steroids/releases)

> **CPR-vCodex Steroids is a personal fork of [CPR-vCodex](https://github.com/franssjz/cpr-vcodex)** (which itself is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)), focused on enhancing the reading experience with a full e-book library manager, cyber-style carousel themes, an offline Wikipedia app, Quick Cards, layout-independent bookmarks and clippings, a dual-mode e-ink screensaver, multi-device (X3/X4) support, and deep device customization - while preserving all reading analytics, statistics, and stability improvements from upstream [CPR-vCodex](https://github.com/franssjz/cpr-vcodex).

<img width="2048" height="1143" alt="CPR-vCodex Steroids Overview" src="https://github.com/user-attachments/assets/56fffc8e-4fae-40f4-8ed5-9b9755002980" />

<img width="1023" height="644" alt="CPR-vCodex Steroids E-Ink Dashboard" src="https://github.com/user-attachments/assets/f93e6962-332f-4d47-9d58-cbf81428ce19" />

<img width="1684" height="390" alt="CPR-vCodex Steroids Banner" src="https://github.com/user-attachments/assets/80641a63-ed4a-48d9-af12-69ad6038d76e" />

</div>

---

## 📌 Table of Contents

- [What's Different in CPR-vCodex Steroids](#-whats-different-in-cpr-vcodex-steroids)
- [Feature Summary Matrix](#-feature-summary-matrix)
- [Detailed Feature Showcase](#-detailed-feature-showcase)
  - [📚 Full E-Book Library v2 & Shelf Tools](#-full-e-book-library-v2--shelf-tools)
  - [📖 Book Context Menu & Metadata Viewer](#-book-context-menu--metadata-viewer)
  - [🌐 Wikipedia App (Offline Wikitext Reader)](#-wikipedia-app-offline-wikitext-reader)
  - [🃏 Quick Cards Viewer](#-quick-cards-viewer)
  - [✂️ Clipping (Highlights) System](#-clipping-highlights-system)
  - [🔖 Bookmarks v4 (Layout-Independent)](#-bookmarks-v4-layout-independent)
  - [🎨 Lyra Carousel & Cyber Data Panel](#-lyra-carousel--cyber-data-panel)
  - [🛡️ Screensaver & Sleep System](#-screensaver--sleep-system)
  - [🖼️ Image Rendering & Display Tuning](#-image-rendering--display-tuning)
  - [🟢 Guide Dots & EPUB Render Modes](#-guide-dots--epub-render-modes)
  - [📊 Reading Time Left in Status Bar](#-reading-time-left-in-status-bar)
  - [🖥️ Multi-Device Hardware Support (X3 & X4)](#-multi-device-hardware-support-x3--x4)
  - [⚡ Performance & Memory Optimizations](#-performance--memory-optimizations)
  - [🧹 Zero Merge-Conflict Settings Architecture](#-zero-merge-conflict-settings-architecture)
- [Upstream CPR-vCodex Foundation](#-upstream-cpr-vcodex-foundation)
  - [Reading Analytics Suite & Achievements](#reading-analytics-suite)
  - [Sync Day & Date Model](#sync-day-and-date-model)
  - [StarDict Dictionaries](#stardict-dictionaries)
  - [SD Card Fonts](#sd-card-fonts)
  - [Flashcards Study Modes](#flashcards-study-modes)
- [Universal SD Card Layout Guide](#-universal-sd-card-layout-guide)
- [Easy Installation & Flashing](#-easy-installation--flashing)
- [Building from Source](#-building-from-source)
- [Repository Documentation Index](#-repository-documentation-index)
- [Credits & License](#-credits--license)

---

## ⚡ What's Different in CPR-vCodex Steroids

This fork builds on top of **CPR-vCodex 1.5.0.9**, inheriting **all** upstream [CPR-vCodex](https://github.com/franssjz/cpr-vcodex) features, including the full reading analytics suite (stats, heatmaps, day details, reading profile, daily goals, streaks, and achievements), Sync Day, dictionary support, flashcards, bookmarks, SD-card fonts, KOReader Sync, Bionic Reading, dark mode, and more.

On top of that, **Steroids** adds a substantial set of original features developed across releases:

- **📚 Full E-Book Library v2** - a scalable grid library (collections/series, covers, filters, and binary database streaming scans) supporting 3,000+ EPUB, XTC, TXT, and Markdown files.
- **🙈 Shelf Management & File Ops** - hide books from the shelf without deleting files (`/.crosspoint/hidden_books.json`), delete book files directly from the context menu, and jump pages (`P−` / `P+`) by long-pressing Left/Right button.
- **📖 Wikipedia App** - search, preview summaries, and read full articles offline (wikitext → markdown cache `.wiki`), configured per-language (`en`, `it`, `fr`, `de`, ...).
- **🃏 Quick Cards** - view images (BMP/JPEG/PNG), vCard/Wi-Fi/geo QR codes, and Code-128 barcodes stored in `/cards/` on the SD card using a cyberpunk panel UI.
- **✂️ Clippings & Highlights** - layout-independent text selection (absolute word index), Kindle-style automatic exports to `/My Clippings.txt`, and a standalone Clippings App with a readable preview panel.
- **🔖 Bookmarks v4** - layout-independent (absolute word index); bookmarks highlight page anchor words with a subtle light-gray dither and jump accurately across font or alignment changes.
- **🖼️ Display & Image Rendering Controls** - fine-tune 2-bit grayscale dithering (Atkinson / Floyd-Steinberg), Gamma LUT (1.5), and quantization thresholds (`Settings → Display`) using fast numeric controls.
- **🗺️ Lyra Cyber-Style Home** - a 5 book cover carousel style and stats data panels, reading-time ETA badges, theme cache separation on a special and exclusive custom `Lyra Marcoand75` theme.
- **🛡️ Screensaver / Sleep System** - a dual-mode e-ink screensaver (folder picker, transparent PNG compositing, reader + general modes, sleep screen rotation, safe power button handling, and zero SD-write sleep entry for X3).
- **📊 Pace-Based Reading Status Bar** - "time left" estimates displayed directly in the reader status bar (`Chapter` or `Book`).
- **🟢 Guide Dots & EPUB Render Modes** - optional guide bullets (`•`) between words; Default, Balanced, and Light render modes with isolated section caches.
- **🕹️ Configurable Controls** - side and front button long-press mapping (Bookmarks, Clippings, Chapter, Orientation); Select button long-press timer toggle.
- **🖥️ Multi-Device Support (X3 / X4)** - `freeink-sdk` integration; runtime board detection via I2C fingerprinting (BQ27220, DS3231, QMI8658); UC8279d panel detection; and a thread-safe SPI bus mutex.
- **🌐 Web Portal & OTA** - split device, app, and steroids settings, dynamic font loading, and fork release OTA manifest management.
- **⚙️ Dedicated Settings JSON Split** - 37 Steroids-only settings stored in `settings-steroids.json` + `JsonSettingsIOSteroids.cpp`, leaving upstream `settings.json` byte-identical for zero merge conflicts.
- **🔄 Silent Restart** - seamless `ESP.restart()` when returning to Home from Library or Wikipedia reclaims fragmented heap (`maxAlloc` ~70 KB → ~105 KB, saving ~1,088 ms of boot overhead).

---

## 📊 Feature Summary Matrix

| Feature | Category | Description |
|---|---|---|
| **Full Library App v2** | 📚 Library | Grid-based browser (2×2, 3×3, 4×4), binary database index (`library.dat`), 3,000+ book capacity, constant ~11 KB RAM |
| **Hide & Delete Books** | 📚 Library | Hide books from the shelf without deleting files (`hidden_books.json`); delete book files + covers directly from the context menu |
| **Library Page Jump** | 📚 Library | Long-press Left/Right side buttons (hold 0.8s) to jump pages (`P−` / `P+`) |
| **Collections & Series** | 📚 Library | Extracts Calibre `series`/`series_index` and EPUB3 `belongs-to-collection` metadata; paginated series view |
| **Book Context Menu** | 📖 Navigation | 9-12 item long-press menu with stats, OPF metadata viewer, favorites, cache, and cover operations |
| **Wikipedia App** | 📖 Apps | Search and read Wikipedia offline (wikitext → markdown `.wiki` cache); follows active UI language |
| **Quick Cards** | 🃏 Apps | Image, QR code, and barcode viewer for cards stored in `/cards/` on SD; cyberpunk UI |
| **Clippings (Highlights)** | ✂️ Reading | Select text, save clippings, view list, jump to page, preview panel, auto-export to `/My Clippings.txt` |
| **Bookmarks v4** | 🔖 Reading | Absolute word index for layout-independent bookmarks; page-level anchor highlight; snippet fallback |
| **Cyber Data Panel** | 🎨 Theme | Two-column book/stats panel on `Lyra Marcoand75` home (Time, Sessions, Progress, ETA, Today, Goal, Streak) |
| **Reading Time ETA** | 🎨 Theme | Estimated remaining reading time on cover badges (e.g. `"35% ~2h 15m"`) across all Lyra themes |
| **Priority Ribbons** | 🎨 Theme | Completed (✓), Favorite (♥), and Opened (●) indicators rendered as corner overlays |
| **Screensaver System** | 🛡️ Screensaver | Dual-mode screensaver, folder picker, battery-protection deep sleep, PNG compositing, 0-write sleep entry |
| **Image Rendering Tuning**| 🖼️ Rendering | Dithering (Atkinson / Floyd-Steinberg), Gamma LUT (1.5), Black/Dark/Light gray quantization thresholds |
| **Status Bar Time Left** | 📊 Reading | Pace-based time remaining (Chapter or Book) in reader status bar |
| **Guide Dots** | 🟢 Reading | Bullet markers (`•`) between words with configurable minimum spacing (Standard 16px / Large 32px) |
| **EPUB Render Modes** | 📐 Reading | Default / Balanced / Light modes with isolated caches per mode; automatic fallback chain |
| **Multi-Device (X3 / X4)** | 🖥️ Hardware | `freeink-sdk` integration; runtime hardware detection; UC8279/UC8179 panel fingerprinting; SPI bus mutex |
| **Configurable Long-Press**| 🕹️ Controls | Side and front button long-press for bookmark, clipping, chapter, and orientation actions |
| **Settings JSON Split** | 🧹 Architecture| 37 Steroids-only settings in `settings-steroids.json`; zero merge conflicts with upstream `settings.json` |
| **Silent Heap Restart** | 🔄 System | `ESP.restart()` on Back-to-Home from heavy apps reclaims heap (~70 KB → ~105 KB) with 0-flash fast boot |
| **Italian i18n Overhaul** | 🇮🇹 i18n | 924 keys fully aligned with English; 84 new translations, 14 corrections |

*All CPR-vCodex upstream features (reading stats, heatmaps, achievements, dictionaries, flashcards, bookmarks, SD fonts, KOReader Sync, Bionic Reading, dark mode, sync day, etc.) are **fully included**.*

---

## 📱 Detailed Feature Showcase

### 📚 Full E-Book Library v2 & Shelf Tools

A complete library browser rewritten for performance and scalability on ESP32 hardware:
- **Configurable grid layout**: 2×2, 3×3, and 4×4 grid layouts selectable in Settings.
- **Scalable binary database (`library.dat`)**: Fixed-record structure with external merge-sort indices; maintains a constant ~11 KB RAM footprint regardless of library size (supports 3,000+ books).
- **Shelf Management**:
  - **Hide from Shelf / Show on Shelf**: Hide books from standard shelf views without deleting the underlying files. Managed via a dedicated **Hidden** filter and stored in `/.crosspoint/hidden_books.json`.
  - **Delete Book File**: Permanently delete the book file, rendering cache, and cover thumbnail directly from the context menu (reading statistics, bookmarks, and clippings remain preserved).
- **Page Jump Navigation**: Hold the Left or Right side buttons for 0.8 seconds to jump an entire page at a time (`P−` / `P+`).
- **Cover Generation Progress UI**: Real-time progress bars on cover tiles, active selector tracking of the processing item, and a global `X / Y Loading...` counter.
- **Collections & Series**: Parses Calibre series tags (`calibre:series`) and EPUB3 `belongs-to-collection` metadata for series browsing.
- **Format support**: EPUB, XTC, TXT, and Markdown files.
- **Incremental startup sync**: `scan_state.dat` comparison with zero e-ink refreshes when files are unchanged.

### 📖 Book Context Menu & Metadata Viewer
Long-press any book in the Home Carousel or Library grid to open a context menu:
- **Open Book**, **Reading Stats**, **Add/Remove Favorites**, **Mark Completed**, **Hide from Shelf**, **Delete Book File**.
- **View Metadata**: Displays title, author, publisher, publication date, language, rights, identifier, and full EPUB OPF description.
- **Cache & Cover Operations**: Clear EPUB section cache, and regenerate individual covers, page covers, or the entire cover cache with confirmation dialogs.

### 🌐 Wikipedia App (Offline Wikitext Reader)
Search and read Wikipedia articles directly on your e-reader:
- **Offline cache (`.wiki`)**: Downloaded wikitext is transformed into markdown and cached on SD for offline reading.
- **Multi-language**: Requests follow the active UI language setting (`en`, `it`, `fr`, `de`, `es`, ...).
- **Dedicated reader**: Uses `WikiTxtReaderActivity` for clean e-ink presentation.

### 🃏 Quick Cards Viewer
View reference cards stored in `/cards/` on the SD card:
- **Format support**: BMP, JPEG, PNG images with automatic scaling and caching.
- **Structured QR Field Parser**: Parses and formats Wi-Fi credentials, vCard contacts, MeCards, geo-coordinates, email links, phone numbers, SMS templates, OTP 2FA tokens, calendar events, and web URLs.
- **Code-128 Barcodes**: Generates barcode views for quick scanning.

### ✂️ Clipping (Highlights) System
Select, store, and manage book passages:
- **Text selection**: Inverted cursor navigation across words and paragraphs with case-insensitive search and punctuation-aware highlighting.
- **Layout-independent**: Saved as absolute word indices (`/.crosspoint/clippings/epub_<hash>.bin`).
- **Auto-export**: Automatically appends highlights to `/My Clippings.txt` on the SD card in Kindle-compatible format.
- **Clippings App & Preview Panel**: Browse clippings grouped by book; open a dedicated preview panel to read chapter, page, and text snippets without losing your reading position.

### 🔖 Bookmarks v4 (Layout-Independent)
- **Absolute word index**: Bookmarks save the exact word offset from chapter start, remaining valid across font size, family, and layout changes.
- **Page anchor highlight**: Bookmarked pages display a subtle light-gray dithered highlight over the anchor word.
- **Snippet fallback**: Backward compatible with older v1-v3 snippet matching.

### 🎨 Lyra Carousel & Cyber Data Panel
- **Two-Column Data Panel**: Displays book stats (Reading Time, Sessions, Reading Days, ETA) and global stats (Today, Goal, Curr. Streak, Books Read).
- **Reading Time ETA**: Displays estimated remaining reading time (e.g. `"35% ~2h 15m"`) on Lyra themes when pace telemetry is available.
- **Theme Cache Separation**: Independent cache folders for `LyraCarouselTheme`, `LyraMarcoand75`, and base themes prevent cross-theme graphics corruption.

### 🛡️ Screensaver & Sleep System
- **Dual-mode operation**: General screensaver folder for overall device sleep, plus a dedicated Reader Screensaver folder when triggered inside a book.
- **Folder Picker with Preview**: Choose custom photo folders directly on-device.
- **Transparent PNG Compositing**: Framebuffer snapshot saved to SD (`/.crosspoint/screensaver-caller.tmp`), allowing transparent PNG overlays over the active screen.
- **0-SD-Write Sleep Entry**: When "Cycle screensaver on tap" is disabled, zero SD card writes occur during sleep entry, eliminating 30-second wake delays on slow SD cards (particularly on X3 hardware).
- **OOM Prevention**: PNG decoder instances and font caches are released before the reader resumes, preventing out-of-memory wake crashes.

### 🖼️ Image Rendering & Display Tuning
Fine-tune image rendering behavior under `Settings → Display`:
- **Image Dithering & Gamma LUT**: Enable/disable error-diffusion dithering and gamma correction.
- **Dither Algorithm**: Toggle between **Atkinson** and **Floyd-Steinberg**.
- **Quantization Thresholds**: Adjustable Black (50), Dark Gray (120), and Light Gray (200) thresholds.
- **Gamma Value**: Configurable multiplier (default 1.5).
- **Fast Edit Mode**: Adjust values by ±1 on a short press and ±5 on a long press.

### 🟢 Guide Dots & EPUB Render Modes
- **Guide Dots**: Bullet markers (`•`) inserted between words with configurable minimum spacing (Standard 16px / Large 32px) for improved visual tracking.
- **EPUB Render Modes**: Choose Default, Balanced, or Light rendering. Each mode maintains isolated section caches (`sections/1_balanced.bin`), preventing cache invalidation loops.

### 📊 Reading Time Left in Status Bar
- **Pace-based estimates**: Calculates time remaining in current chapter or entire book (e.g. `"45 min"` or `"2h 10m"`) using natural page-turn telemetry.
- Positioned on the left side of the reader status bar, avoiding battery icon overlap.

### 🖥️ Multi-Device Hardware Support (X3 & X4)
Powered by [`freeink-sdk`](https://github.com/Free-Ink/freeink-sdk):
- **Runtime Board Detection**: I2C probe identifies hardware at boot (X4 vs X3) via RTC, IMU, and gauge sensors.
- **UC8279d / UC8179 / SSD1677 Panel Fingerprinting**: Automatic e-ink controller identification.
- **Thread-Safe SPI**: `HalSpiBus` recursive mutex prevents bus collisions during async e-ink refreshes.

### ⚡ Performance & Memory Optimizations
Targeted optimizations for ESP32-C3 constrained memory (~380 KB usable RAM):
- 🧠 **Font Decompressor Lazy Init**: Saves ~48 KB heap at boot until compressed font formats are actually requested.
- 🗑️ **Render Memory Release**: `GfxRenderer::freeUnusedRenderMemory()` releases up to 48 KB of BW grayscale buffers before cover generation and sleep routines.
- 🔄 **Silent Restart**: Returning to Home from heavy memory operations performs a seamless `ESP.restart()` (saving ~1,088 ms of boot stages), raising `maxAlloc` from ~70 KB to ~105 KB.
- 🔌 **File Transfer Exit**: Exiting File Transfer without active Wi-Fi operations returns cleanly to Home without triggering a device reboot.

### 🧹 Zero Merge-Conflict Settings Architecture
- **Dedicated JSON File**: 37 Steroids-only settings stored in `settings-steroids.json` handled by `JsonSettingsIOSteroids.cpp`.
- **Upstream Protection**: Keeps upstream `settings.json` and `JsonSettingsIO.cpp` byte-identical, eliminating merge conflicts during upstream syncs.
- **Automatic Migration**: One-shot migration backs up original settings to `/.crosspoint/settings-steroids.json.bak`.

---

## ⬇️ Upstream CPR-vCodex Foundation

CPR-vCodex Steroids incorporates the complete feature set of **CPR-vCodex 1.5.0.9**:

### Reading Analytics Suite
- **Comprehensive metrics**: Track started books, completed titles, total reading duration, daily sessions, streak continuity, and daily goals.
- **Views**: `Reading Stats` hub, `Reading Heatmap` (monthly calendar), `Reading Day` detail, and `Reading Profile`.
- **Manual Corrections**: Adjust per-book reading time (add/subtract minutes for specific dates) and edit start dates.
- **Achievements**: Unlock milestones for reading streaks, books finished, time milestones, and bookmark usage.

### Sync Day and Date Model
- NTP date synchronization over Wi-Fi (`Home > Sync Day`) establishes a trusted reference date on hardware lacking a sleep-resilient real-time clock.
- Falls back to the last saved reference date to maintain coherent daily streaks and heatmap tracking across reboots.

### StarDict Dictionaries
- StarDict dictionary support stored in `/dictionaries/<language>/`.
- Fast lookup cache (`.cpridx`) generated automatically.
- Look up words directly while reading EPUBs, view your search history, and switch between monolingual or translation dictionaries.

### SD Card Fonts
- Custom `.cpfont` families loaded from `/fonts/<Family>/` on microSD.
- Manage and download fonts via device settings or manual zip extraction (includes `ChareInk`, `Bookerly`, `Noto Sans`, `Lexend`).

### Flashcards Study Modes
- Offline CSV deck study (`front,back` format).
- Four review modes: **Due**, **Scheduled**, **Infinite**, and **Sequential**.
- Session summaries, accuracy tracking, and deck statistics.

---

## 💾 Universal SD Card Layout Guide

Organize your microSD card for maximum compatibility:

```text
microSD Root (SD:/)
├── cards/                      # Quick Cards (PNG, JPEG, BMP images, QR & barcode text files)
├── dictionaries/               # StarDict dictionaries
│   ├── english/                #   e.g. .ifo, .idx, .dict files
│   └── spanish/
├── fonts/                      # Custom .cpfont font families
│   └── ChareInk/               #   ChareInk_12.cpfont ... ChareInk_18.cpfont
├── sleep/                      # Custom screensaver & sleep images (PNG, BMP, JPEG)
├── if_found.txt                # Optional contact info displayed by 'If Found' app
└── My Clippings.txt            # Auto-generated highlights export file
```

---

## 🌐 Easy Installation & Flashing

### Web Auto Flash
1. Download the latest `*.bin` release from [GitHub Releases](https://github.com/malv-ryx/cpr-vcodex-steroids/releases).
2. Connect your Xteink X4 or X3 via USB-C in Chrome or Edge.
3. Open [xteink.dve.al](https://xteink.dve.al/) or the GitHub Pages flasher.
4. Select `Flash firmware from file`, choose the `.bin`, and complete installation.

### Local SD Card Flashing
Copy the release `.bin` file to the root of your SD card, then navigate to `Settings > System > SD Card Firmware Update` on the device to flash locally without a computer.

---

## 🛠️ Building from Source

### Prerequisites
- **PlatformIO Core** (`pio`) or VS Code with PlatformIO extension
- **Python 3.8+**

### Command Line Workflow

Preferred commands (from repository root):

```powershell
# Default local build
python -X utf8 -m platformio run -e default -j 16

# Release build
python -X utf8 -m platformio run -e gh_release -j 16

# Run pre-release validation checks
python -X utf8 scripts/pre_release_check.py --tag 1.5.0.9-dev1-cpr-vcodex-steroids

# Sync auto-flash firmware manifest
python -X utf8 scripts/sync_autoflash_firmware.py --repo malv-ryx/cpr-vcodex-steroids
```

*Note: If `pio` is available in your PATH, `pio run -e default` and `pio run -e gh_release` are equivalent.*

---

## 📖 Repository Documentation Index

For deeper technical details, developer guidance, and maintenance workflows:

| File | Purpose |
|---|---|
| [`AGENTS.md`](./AGENTS.md) | Agent guide, codebase overview, and skill routing rules |
| [`STEROIDS-ADDICTIONS.md`](./STEROIDS-ADDICTIONS.md) | **Single source of truth** for all Steroids apps, sleep modes, and enhancements |
| [`STEROIDS-ALIGN-TO-UPSTREAM.md`](./STEROIDS-ALIGN-TO-UPSTREAM.md) | Step-by-step workflow for merging upstream releases while preserving Steroids features |
| [`QUICK-CARDS.md`](./QUICK-CARDS.md) | Hardware SDK migration (`freeink-sdk`), X3/X4 panel details, and Quick Cards specification |
| [`USER_GUIDE.md`](./USER_GUIDE.md) | End-user manual for device apps and settings |
| [`SCOPE.md`](./SCOPE.md) | Project design principles and stability scope |
| [`CHANGELOG.md`](./CHANGELOG.md) | Release notes and detailed commit log history |

---

## 🙏 Credits & License

### Attribution
- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** - the upstream baseline project.
- **[franssjz](https://github.com/franssjz)** - creator of **CPR-vCodex**, the fork this project builds upon.
- **[Free-Ink / uxjulia](https://github.com/Free-Ink/freeink-sdk)** - for `freeink-sdk` and CrossInk hardware compatibility.
- **[zgredex](https://github.com/zgredex)** & **[erickosanchezj](https://github.com/erickosanchezj)** - `Lyra Carousel` Home theme design and adaptation.

### License
This project is licensed under the **MIT License** - see the [LICENSE](./LICENSE) file for details.

*Disclaimer: CPR-vCodex Steroids is an independent community project and is not affiliated with XTEINK or hardware manufacturers.*
