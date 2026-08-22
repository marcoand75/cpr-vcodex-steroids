# UPSTREAM-ALIGNMENT-REMAINING-PLAN.md

## DETAILED ANALYSIS: SdCardFont Fragmentation-Resistant Bitmap Storage

### Background: The Problem

Steroids runs on the ESP32-C3 (320 KB RAM, no PSRAM). The SdCardFont system
loads glyph bitmaps for large CJK/Unicode characters from SD card into RAM
for fast rendering. The current Steroids implementation allocates bitmap data
for all prewarmed glyphs in a single contiguous block per font style:

```cpp
// Current Steroids PerStyle (simplified)
struct PerStyle {
  EpdGlyph* miniGlyphs = nullptr;
  uint32_t miniGlyphsCap = 0;
  uint8_t* miniBitmap = nullptr;   // SINGLE contiguous allocation
  uint32_t miniBitmapCap = 0;      // Grows via realloc as needed
  // ...
};
```

For a typical CJK page with full-width characters at 2bpp, a single glyph
can require 20-40 KB. After an extended reading session (30+ minutes),
the heap becomes fragmented from allocations/frees in the EPUB parser,
image decoder, and rendering pipeline. When `miniBitmap` needs to grow
(via realloc), a sufficiently large contiguous block may not be available
even if total free memory is adequate, causing prewarm failures that
manifest as missing glyphs or garbled text.

### Upstream Solution (Commit 28af4189, within CPR 1.5.0.20)

#### 1. Chunk-Based Bitmap Allocation (SdCardFont.h + .cpp)

Replaces the single `miniBitmap` pointer with an array of fixed-size chunks:

```cpp
static constexpr uint32_t MINI_BM_CHUNK_SHIFT = 12;  // 2^12 = 4096 bytes
static constexpr uint32_t MINI_BM_CHUNK_SIZE = 1u << MINI_BM_CHUNK_SHIFT;
static constexpr uint32_t MINI_BM_MAX_CHUNKS = 24;  // 96 KB maximum

struct PerStyle {
  // REMOVED:
  // uint8_t* miniBitmap = nullptr;
  // uint32_t miniBitmapCap = 0;

  // ADDED:
  uint8_t* miniBitmapChunks[MINI_BM_MAX_CHUNKS] = {};
  uint32_t miniBitmapChunkCount = 0;
  bool miniMetadataOnly = false;
  bool miniLoadedWithKernLig = false;
};
```

Each glyph's bitmap data is placed at a `dataOffset` within the chunk array.
The chunk index is `dataOffset >> 12` and the offset within the chunk is
`dataOffset & 0xFFF`. Glyphs can span chunk boundaries — the bitmap copy
is split into a head (remainder of current chunk) and tail (beginning of
next chunk).

**Key function: `miniGlyphBitmap()`**
```cpp
const uint8_t* SdCardFont::miniGlyphBitmap(const void* ctx, const uint32_t dataOffset) const {
  const auto* overflowContext = static_cast<const OverflowContext*>(ctx);
  const PerStyle& style = styles_[overflowContext->styleIdx];
  const uint32_t chunkIndex = dataOffset >> MINI_BM_CHUNK_SHIFT;
  // Bounds check — returns nullptr if chunk not allocated
  const uint8_t* chunk = style.miniBitmapChunks[chunkIndex];
  return chunk ? chunk + (dataOffset & (MINI_BM_CHUNK_SIZE - 1)) : nullptr;
}
```

This is called by `GfxRenderer::getGlyphBitmap()` to resolve the bitmap
pointer on-demand during rendering, instead of storing a direct pointer
in the `EpdGlyph` struct.

#### 2. Coverage Query Callback (EpdFontData.h, SdFontFont.cpp)

Upstream adds a `coverageHandler` to `EpdFontData`:
```cpp
// EpdFontData.h
struct EpdFontData {
  // ... existing fields ...
  bool (*coverageHandler)(void* ctx, uint32_t codepoint) = nullptr;
  void* coverageCtx = nullptr;
};

// SdCardFont.cpp
bool SdCardFont::onCoverageQuery(void* ctx, const uint32_t codepoint) {
  const auto* overflow = static_cast<const OverflowContext*>(ctx);
  const SdCardFont* self = overflow->self;
  const auto& s = self->styles_[overflow->styleIdx];
  // Check if codepoint is in any prewarmed miniInterval
  // ... binary search through miniIntervals ...
}
```

This allows `GfxRenderer::getTextWidth()` and other measurement functions
to query whether a codepoint is covered by the prewarmed mini glyph set
WITHOUT triggering an on-demand SD card read. If the font data has a
`coverageHandler`, the renderer uses it; otherwise it falls back to
checking `glyphMissHandler != nullptr`.

#### 3. TextGetter Callback Pattern (GfxRenderer.h/.cpp, SdCardFont.h/.cpp)

Replaces direct string iteration with a callback-based approach:
```cpp
// GfxRenderer.h
using TextGetter = const char* (*)(const void* ctx, uint32_t index);
void prewarmFallbackText(int fontId, TextGetter getter, const void* ctx,
                        uint32_t textCount, uint8_t styleMask = 0x0F) const;

// SdCardFont.h
using TextGetter = const char* (*)(const void* ctx, uint32_t index);
int prewarm(TextGetter getter, const void* ctx, uint32_t textCount,
            uint8_t styleMask = 0x0F, bool metadataOnly = false,
            bool loadKernLig = true);
```

The existing `prewarm(const char* utf8Text, ...)` is refactored to call
the new overload with `singleTextGetter` as the callback.

#### 4. FrameBufferLoan RAII Pattern (GfxRenderer.h/.cpp)

The most architecturally significant change — a new RAII class that
temporarily loans the frame buffer to the build scratch allocator during
font prewarming, freeing ~150 KB (on X4) of contiguous DRAM:

```cpp
class FrameBufferLoan {
  GfxRenderer& renderer_;
  bool active_ = false;
public:
  explicit FrameBufferLoan(GfxRenderer& renderer);
  ~FrameBufferLoan() { end(); }
  void end();
};

void GfxRenderer::releaseFrameBufferForBuild() {
  if (!frameBuffer) return;
  buildscratch::lend(frameBuffer, frameBufferBytes);
  frameBuffer = nullptr;
}

bool GfxRenderer::restoreFrameBufferAfterBuild() {
  buildscratch::reclaim();
  frameBuffer = display.getFrameBuffer();
  if (frameBuffer) memset(frameBuffer, 0xFF, frameBufferBytes);
  return frameBuffer != nullptr;
}
```

This pattern is used in `wrappedText()`, `drawText()`, and other rendering
functions that call font prewarming. The frame buffer is lent to the build
scratch space so that the prewarm operation can temporarily use that memory
for its own allocations (e.g., reading SD card data), then it's reclaimed
after prewarm completes.

#### 5. std::vector → std::deque Migration

`ensureSdCardFontReady()` changes from `std::vector<std::string>` to
`std::deque<std::string>`. This improves cache locality for the
prewarm loop (deque nodes are allocated contiguously in the page-sized
chunks) and avoids reallocation when adding lines.

#### 6. Built-in Font Removal

Upstream removes 12 Lexend font header files (~24,000 lines of
pre-generated bitmap data). This is acceptable upstream because the
newer EPD display firmware loads fonts from SD card, but Steroids
still ships with built-in fonts (Bookerly, Lexend, NotoSans, OpenDyslexic).

#### 7. Overflow Bitmap Allocation Simplification

The `onGlyphMiss` overflow slot bitmap allocation changes from
capacity-tracking + reuse to always-allocate-temporary-then-commit.
This is simpler and pairs with the chunked mini bitmap storage.

### Scope of Changes (5db401f5 → 3e46941c)

| Component | Files | Add | Del |
|---|---|---|---|
| `lib/EpdFont/` | 15 files | 355 | 318 |
| `lib/GfxRenderer/` | 6 files | 755 | 609 |
| `lib/GfxFont/` | 2 files | 13 | 10 |
| `src/activities/` (rendering callers) | 5 files | 742 | 3064 |
| Built-in font headers | 12 files | 0 | 14,000+ |
| **Total** | **37 files** | **~1,100** | **~27,000** |

### Integration Architecture Diagram

```
Steroids Current (sync SectionBuilder):
  EpubReaderActivity → SectionBuilder.syncBuild()
       → GfxRenderer.drawText() → SdCardFont.prewarm(const char*)
       → GfxRenderer wrappedText() → SdCardFont.prewarm(const char*)
       → FontCacheManager (single contiguous miniBitmap)
  Frame buffer always resident, no lending

Upstream (progressive SectionBuilder + FrameBufferLoan):
  SectionBuilder.asyncBuild() → FrameBufferLoan → buildscratch::lend()
       → GfxRenderer.drawText() → SdCardFont.prewarm(TextGetter, ctx, count)
       → SdCardFont uses chunked miniBitmapChunks (4KB each)
       → onCoverageQuery callback for fast measurement
       → FontCacheManager::restoreFrameBufferAfterBuild → reclaim
  Frame buffer lent during prewarm, reclaimed after
```

### Pro/Con Analysis

#### Option A: Port SdCardFont chunking only (SdCardFont.h/.cpp only)

**PROS:**
- Eliminates the core OOM/failure mode: fragmented heap can no longer
  cause prewarm failures because 4KB chunks are much easier to allocate
- ~100 lines of C++ in SdCardFont.cpp, ~67 lines in SdCardFont.h
- No changes to rendering pipeline, file collection types, or callers
- Can be tested in isolation (just load a font and prewarm)
- Backward compatible: chunk array can be populated from the same
  `singleTextGetter` path, so existing callers need no changes

**CONS:**
- Loses the **primary benefit**: `FrameBufferLoan` pattern that frees
  ~150 KB during prewarm. Without lending the frame buffer, the chunk
  allocation savings are modest (heap fragmentation is reduced but the
  overall memory pressure during rendering is unchanged)
- `miniGlyphBitmap()` requires `glyphMissCtx` to carry the `OverflowContext`
  — this must match Steroids' existing EpdFontData layout
- `onCoverageQuery` and `TextGetter` callback are NOT ported, so
  text measurement functions (`getTextWidth`, `wrappedText`) still use
  the old glyph-miss-driven path — they won't benefit from the coverage
  check optimization
- `buildAdvanceTable(std::deque)` stays as `std::vector` in Steroids —
  no cache locality improvement for advance table
- Partial port creates a maintenance burden: Steroids diverges from both
  old upstream (1.5.0.5) AND new upstream (1.5.0.22) in the SdCardFont
  internal representation

**VERDICT**: Marginal improvement at best. The chunking is the least
important part of this change — the FrameBufferLoan is the real win.
Not recommended as a standalone port.

#### Option B: Full SdCardFont + GfxRenderer + FontCacheManager port

**PROS:**
- Maximum benefit: FrameBufferLoan frees contiguous frame buffer memory
  for prewarm allocations
- TextGetter callback enables prewarming from reader's line vector without
  copying to a single string
- onCoverageQuery accelerates text measurement
- Chunk-based storage eliminates fragmentation-related prewarm failures
- Full alignment with upstream reduces future merge burden

**CONS:**
- **MASSIVE**: 37 files, 27,000 lines changed, 710 net insertions
- **Rendering pipeline redesign**: FrameBufferLoan requires auditing
  every `drawText`/`wrappedText`/`drawIcon`/`drawBitmap` call site to
  ensure frame buffer lending is safe (the rendering code must not
  access the frame buffer while it's lent)
- **Built-in font removal breaks Steroids**: Updstream removes all 12
  Lexend font .h files; Steroids still ships Bookerly, Lexend, NotoSans,
  OpenDyslexic as built-ins
- **Steroids EPUB engine divergence**: CrossInk's synchronous
  SectionBuilder vs upstream's progressive builder — the FrameBufferLoan
  pattern was designed around the progressive build's incremental
  prewarming needs; Steroids' synchronous build prewarms once for the
  whole section, which changes the memory lending dynamics
- **BitmapHelpers changes**: The `invertMonochromeBitmap` change from
  fixed buffer to `std::vector<uint8_t>` affects all icon rendering,
  including Steroids' customized UI theme icons
- **Cannot be tested in simulation**: Requires actual X4 device with
  fragmented heap scenario, CJK text rendering, and extended reading
  session to validate

**VERDICT**: Too risky for current Steroids architecture. The
FrameBufferLoan integration with CrossInk's synchronous SectionBuilder
is an open question that needs hardware validation.

#### Option C: Defer until Steroids migrates to progressive SectionBuilder

**PROS:**
- When/if Steroids adopts the upstream progressive EPUB engine, the full
  SdCardFont + FrameBufferLoan port becomes natural
- No risk to current stable reading experience
- Can pick up the benefit later when the rendering pipeline is more aligned

**CONS:**
- Misses the fragmentation protection for current users
- May not happen (Steroids' SectionBuilder divergence is intentional)

### Current Assessment

The SdCardFont chunking is tightly coupled with the FrameBufferLoan
pattern in GfxRenderer. Without FrameBufferLoan, the chunking provides
minimal benefit. With FrameBufferLoan, it requires auditing the entire
rendering pipeline for frame-buffer-safety.

**Recommendation**: DEFER. The risk/reward ratio is unfavorable for
Steroids' current architecture. The chunked allocation should be
revisited when/if the EPUB SectionBuilder is modernized to the
progressive model.

---

## REMAINING DEFERRED ITEMS

### 2. HAL Crash Detection (PANIC_CAPTURE_MAGIC)
- **Diff**: 25 insertions / 6 deletions in `lib/hal/HalSystem.cpp`
- **Risk**: MEDIUM (watchdog reset detection needs device testing on X4)
- **Approach**: Port as-is, test on device

### 3. Web Server Serial Number
- **Status**: COMPLETED (already ported — see commit f467593a)

### 4. FirmwareFlasher runningPartitionChipId()
- **Status**: COMPLETED (already ported — see commit f467593a)
