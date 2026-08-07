# CPR-vCodex Steroids — Release Notes

> **From:** `006c0110` (Image Rendering Settings)  
> **To:** `495d582a` (Recents Panel + Layout Shift)  
> **19 commits** — features, fixes, and UI enhancements

---

## 🙏 Thanks

UI/UX enhancements and bug reports by **[@malv-ryx](https://github.com/malv-ryx)**
and the [r/CPRvCodex](https://www.reddit.com/user/Alternative-Day-9470/) community.
Your detailed issue reports made these improvements possible!

---

## ✨ New Features

### 📚 Library Shelf — Hide & Delete Books ([#42](https://github.com/marcoand75/cpr-vcodex-steroids/issues/42))

Long-press any book in the Library to open a new context menu with two powerful options:

- **Hide from Shelf / Show on Shelf** — temporarily hide books without deleting them.
  Hidden books are excluded from all standard views (All, Favourites, Recent, Unread, Completed).
  A dedicated **Hidden** filter lets you view and manage hidden books.
- **Delete Book File** — permanently deletes the book from SD card + its cache + cover thumbnail.
  Reading stats, bookmarks, and clippings are **never touched**. A confirmation dialog warns
  about permanent deletion and, when the library is in Manual update mode, reminds you to
  run a manual scan afterwards.

Hidden books are saved in `/.crosspoint/hidden_books.json` and persist across reboots.

### 🖼️ Image Rendering Tuning

New **Display** settings section (Settings → Display) for fine-tuning how book covers
and screensavers are rendered on your e‑ink screen:

| Setting | Default | What it does |
|---------|---------|-------------|
| Image Dithering | ON | Smooths grayscale via error-diffusion |
| Gamma LUT | ON | Gamma correction before quantization |
| Dither Algorithm | Atkinson | Atkinson or Floyd‑Steinberg |
| Black Threshold | 50 | Gray level below which = pure black |
| Dark Gray Threshold | 120 | Gray level below which = dark gray |
| Light Gray Threshold | 200 | Gray level below which = light gray |
| Gamma Value | 1.5 | Gamma correction multiplier |

Numeric values use **fast edit mode**: Up/Down short = ±1, long = ±5, Select to confirm.
Default (calibrated) values are shown next to each setting.

### ⚡ Library Page Navigation

Long-press **Left** or **Right** on the Library shelf to jump an entire page at once:

| Button | Short Press | Long Press (hold 0.8s) |
|--------|-------------|------------------------|
| Left | Previous book | **Previous page** (P−) |
| Right | Next book | **Next page** (P+) |
| Up | Row up | Sort popup |
| Down | Row down | Filter popup |

Button labels have been updated to `Left/P−` / `Right/P+` (EN) and `Sx/P−` / `Dx/P+` (IT).

### 📊 Cover Generation Progress ([#39](https://github.com/marcoand75/cpr-vcodex-steroids/issues/39))

When the Library regenerates missing covers (after clearing cache or re-indexing):

- A **white progress bar** appears on each cover tile as it's being generated
- The **selection frame** moves to the currently processing book so you can see which one is being worked on
- A global **"X / Y Loading…"** counter appears below the book title
- The grid remains visible throughout — no more blank screen during generation!

---

## 🎨 UI/UX Improvements

### 🏠 Home Dashboard Restructure ([#46](https://github.com/marcoand75/cpr-vcodex-steroids/issues/46))

The Lyra MarcoAnd75 dashboard has been completely reorganized:

```
┌──────────────────────────────────────────┐
│  Book Title + Author                     │
├────────────────────┬─────────────────────┤
│  BOOK STATS        │  GLOBAL STATS       │
│  Read: 8h48m       │  Today: 1h8m        │
│  Sessions: 23      │  Goal: 30m  ✓       │
│  Days: 12          │  Curr. Streak: 9d   │
│  Left: ~2h5m       │  Books Read: 3      │
├────────────────────┴─────────────────────┤
│  Progress bar  81%                       │
└──────────────────────────────────────────┘
```

**What changed:**
- Footer panel (Streak / Read / ETA) **removed entirely**
- **ETA** (time left to finish) moved inside BOOK STATS
- **Streak** and **Books Read** moved inside GLOBAL STATS
- **Days** (distinct reading days for current book) added as new metric
- All labels clarified: `Book Time` → `Read`, `ETA` → `Left`, `Streak` → `Curr. Streak`, `Read` → `Books Read`
- Goal checkmark (✓) appears when daily reading goal is reached

### 🔝 Home Header — Recents Panel

A new styled panel in the top‑left corner of the Home screen shows `Latest Recents (N)`
with the current number of books in the carousel. It uses the same cyberpunk style
as the BOOK STATS / GLOBAL STATS panels for a cohesive look.

The entire carousel has been shifted **12px down** to accommodate the new panel.

### 📖 Library Header Text Truncation ([#45](https://github.com/marcoand75/cpr-vcodex-steroids/issues/45))

Long book titles in the Library header no longer get cut off with ugly `..` truncation.
All header text (title, author, info line) now uses proper Unicode ellipsis (`…`) with
**8px minimum margins** on both sides, UTF‑8 safe, and the font style used for
measurement matches the actual draw style — preventing overflow at the right edge.

### 🏷️ Cover Menu Labels ([#43](https://github.com/marcoand75/cpr-vcodex-steroids/issues/43))

Renamed to be clear and non‑alarming:

| Before | After (EN) | After (IT) |
|--------|------------|------------|
| Delete cover thumbnail | Re‑generate this cover | Rigenera questa copertina |
| Delete page covers | Re‑generate page covers | Rigenera copertine pagina |
| Delete all covers | Re‑generate all book covers | Rigenera tutte le copertine |

Page and all‑cover deletions now show a **confirmation dialog**. After confirming
(or cancelling), missing covers are automatically regenerated.

Confirmation dialogs now support **multi‑line text** for long messages — no more truncation.

### ⚠️ Clear Reading Cache Dialog ([#37](https://github.com/marcoand75/cpr-vcodex-steroids/issues/37))

The alarmist warning *"All reading progress will be lost!"* has been corrected:

- **Before:** "This will clear all cached book data. All reading progress will be lost!"
- **After:** "This will clear cached book rendering data. Your reading position and stats will be preserved."

### 🔘 Side Button Hints Fix ([#44](https://github.com/marcoand75/cpr-vcodex-steroids/issues/44))

Side button hint boxes (the "Up/Sort" and "Down/Filter" labels on the right edge)
were too short, causing text overflow. Height increased from 78px to 100px,
vertically centered, and aligned to the physical buttons on the X4.

---

## 🛠️ Technical Details

| Area | Detail |
|------|--------|
| New files | `src/HiddenBooksStore.h/.cpp`, `lib/GfxRenderer/ImageRenderConfig.h/.cpp` |
| Modified files | 20+ files across library, home, themes, i18n, settings, and index |
| I18N keys added | 15 new keys (EN + IT) |
| RAM impact | ~40 bytes for runtime globals |
| Flash impact | ~2 KB for new store + panel rendering |
| Cache version | MarcoAnd75 bumped to v4 |

---

## 📦 Files Modified

`src/HiddenBooksStore.{h,cpp}`, `src/activities/apps/LibraryActivity.{h,cpp}`,
`src/activities/home/BookContextMenuActivity.{h,cpp}`, `src/activities/home/HomeActivity.{h,cpp}`,
`src/components/LibraryIndex.{h,cpp}`, `src/components/themes/lyra/LyraMarcoand75Theme.{h,cpp}`,
`src/components/themes/lyra/LyraTheme.{h,cpp}`, `src/components/themes/BaseTheme.{h,cpp}`,
`src/activities/util/ConfirmationActivity.{h,cpp}`, `src/CrossPointSettings.h`,
`src/JsonSettingsIOSteroids.cpp`, `src/SettingsList.cpp`, `src/activities/settings/SettingsActivity.{h,cpp}`,
`src/main.cpp`, `src/network/CrossPointWebServer.cpp`, `src/network/html/AppSettingsPage.html`,
`src/network/html/SteroidsSettingsPage.html`, `lib/GfxRenderer/ImageRenderConfig.{h,cpp}`,
`lib/GfxRenderer/DitheringConfig.h`, `lib/GfxRenderer/BitmapHelpers.{h,cpp}`,
`lib/GfxRenderer/Bitmap.cpp`, `lib/JpegToBmpConverter/JpegToBmpConverter.cpp`,
`lib/PngToBmpConverter/PngToBmpConverter.cpp`, `lib/Epub/Epub/converters/DitherUtils.h`,
`lib/I18n/translations/english.yaml`, `lib/I18n/translations/italian.yaml`

---

*CPR-vCodex Steroids — stable reading first, careful improvements second.*
