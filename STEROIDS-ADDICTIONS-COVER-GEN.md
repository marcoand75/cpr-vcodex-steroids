# CPR-vCodex Steroids — Cover Generation Improvements

> **SCOPE:** Robust cover thumbnail generation, deletion handling, retry logic, cache alignment with upstream. Low-medium merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **On-Demand Generation** | Covers generated when needed (library scan, home carousel) |
| **Format Support** | EPUB (embedded), XTC (embedded), TXT/MD (title card) |
| **Thumbnail Sizes** | Multiple: 210×340 (carousel), 70×250 (side), raw cache |
| **Progressive Render** | One cover per frame, non-blocking |
| **Placeholder Fallback** | White-title black ribbon when cover missing |
| **Cache Persistence** | Survives reboots, shared across themes |

---

## 2. Technical Architecture

### 2.1 Cover Cache Structure (Upstream-Aligned)
```
~/.crosspoint/
├── epub_<hash32>/          # hash = std::hash(path) 32-bit, max 10 digits
│   └── thumb_<H>.bmp       # e.g., thumb_340.bmp
├── xtc_<hash32>/
│   └── thumb_<H>.bmp
├── txt_<hash32>/
│   └── thumb_<H>.bmp
├── cover-raw/
│   └── <hash>_<WxH>.raw    # Raw framebuffer for side covers
└── home-carousel-cache/
    └── <frameHash>.bin     # Full frame cache
```

### 2.2 Generation Pipeline (`CoverGenerator.cpp`)
```cpp
bool CoverGenerator::generateCover(const std::string& bookPath,
                                    int targetW, int targetH) {
    // 1. Heap guard
    if (ESP.getMaxAllocHeap() < MIN_HEAP_FOR_FORMAT) return false;

    // 2. Parse metadata
    CoverImage img;
    if (isEpub(bookPath)) parseEpubCover(bookPath, img);
    else if (isXtc(bookPath)) parseXtcCover(bookPath, img);
    else generateTitleCard(bookPath, img);

    // 3. Scale + dither (Atkinson, 4-level grayscale)
    scaleAndDither(img, targetW, targetH);

    // 4. Save BMP
    saveBmp(thumbPathFor(bookPath, targetW, targetH), img);
    return true;
}
```

### 2.3 Key Fixes (Steroids)

| Issue | Fix | Commit |
|-------|-----|--------|
| Corrupt BMP not removed | `removeCorruptBmp()` in drawTile, verify after gen | de455c6c |
| Partial BMP deleted during gen | Don't remove if `coverGenActive` | c519ec91 |
| Cover refresh after delete | Trigger `coverGen` on single-book delete | c64f7089 |
| Frame cache race | Never persist frame with placeholder | 6d41984f |
| Upstream cache alignment | `epub_<hash>/thumb_<H>.bmp` verbatim | cover_cache.upstream_alignment |

### 2.4 Progressive Rendering
```cpp
// CoverGenerator::loop()
if (pendingCovers_.empty()) return;

CoverTask task = pendingCovers_.front();
if (generateCover(task.path, task.w, task.h)) {
    pendingCovers_.pop();
    requestUpdate();  // Trigger redraw for this cover
}
// One cover per frame → smooth UI
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/util/CoverGenerator.h/cpp` | Core generation, pipeline, cache |
| `src/components/EpubParser.h/cpp` | EPUB cover extraction |
| `src/components/XtcParser.h/cpp` | XTC cover extraction |
| `src/components/TxtParser.h/cpp` | Title card generation |
| `src/components/themes/lyra/LyraCarouselTheme.cpp` | Carousel cover loading |
| `src/components/themes/lyra/LyraMarcoand75Theme.cpp` | Home cover loading |
| `src/activities/home/HomeActivity.cpp` | Frame cache, cover refresh triggers |
| `src/activities/apps/LibraryActivity.cpp` | Library cover generation |

---

## 4. Upstream Merge Notes

### LOW-MEDIUM RISK
- `CoverGenerator` — Enhanced with fixes
- Cache directory structure (must match upstream verbatim)
- Progressive render pattern

### Conflicts Likely
- `thumbPathFor()` hash scheme (`std::hash` 32-bit)
- `epub_<hash>/` vs `epub_<hash32>/` naming
- Partial BMP handling during generation

### Safe Cherry-Picks (High Value)
- **Corrupt BMP removal + verification loop** (de455c6c)
- **Don't delete partial BMP during active generation** (c519ec91)
- **Trigger coverGen after single-book delete** (c64f7089)
- **Never cache frame with placeholder** (6d41984f)

---

## 5. Validation Checklist

- [ ] EPUB cover extracted and thumbnail saved
- [ ] XTC cover extracted and thumbnail saved
- [ ] TXT/MD generates title card
- [ ] Multiple sizes: 210×340, 70×250, raw
- [ ] Placeholder shown when cover missing
- [ ] Corrupt BMP removed, retry works
- [ ] Partial BMP not deleted during generation
- [ ] Cover refresh after book delete
- [ ] Frame cache never stores placeholder
- [ ] Cache directory structure matches upstream

---

## 6. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-LIBRARY.md` — Library cover integration
- `STEROIDS-ADDICTIONS-HOME-THEMES.md` — Home carousel covers

---

*Last updated: 2026-09-28 | Commits: de455c6c, c519ec91, c64f7089, 6d41984f*