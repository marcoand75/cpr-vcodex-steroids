#pragma once

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"  // for Rect

class GfxRenderer;

// Persistent cache for the Library activity. The cache is the source of
// truth for the on-device library view -- the SD card is only walked on cold
// open, manual refresh, or after an inbound transfer invalidates the cache.
//
// Storage layout:
//   /.crosspoint/library.bin v3
//     u8  version (== 3)
//     u16 count
//     repeat `count` records:
//       u16 + bytes : path           (required, non-empty)
//       u16 + bytes : title          (display label for grid tiles)
//       u16 + bytes : author         (empty when unknown)
//     Footer (v3 only):
//       u32 offsets[count]           (byte offset of each record from start of file)
//
// Records are written in display order so loaders never need to re-sort.
//
// All I/O operations are lazy: the caller loads only the pages it needs via
// loadPage().  There is no "load all entries" path in the API -- the device
// simply does not have enough RAM for a full library (1000+ entries).
// The sync/scan functions write directly to the v3 cache file without
// materializing all entries in RAM at once.
namespace LibraryCache {

// Slim in-RAM record. Cover thumbnail path is computed on demand from
// `path` via a deterministic hash -- never stored in the cache file.
// Kept to just path/title/author to minimize per-entry RAM on the
// constrained ESP32-C3 (large libraries hold many of these at once).
struct Entry {
  std::string path;
  std::string title;
  std::string author;
};

// Maximum number of Entry objects held in memory at any time (a window
// of consecutive entries).  Larger values reduce SD reads at the cost of
// per-entry heap (~140 bytes each).  64 entries ≈ 9 KB.
constexpr int kEntryWindow = 64;

// Compute the thumbnail BMP path for an entry at the given grid cell size.
std::string thumbPathFor(const std::string& path, int coverW, int coverH);

// Generate a cover thumbnail at the exact grid dimensions. Short-circuits
// when the BMP file already exists.
// Returns true if a thumb is now available on disk.
bool generateCoverForBook(const std::string& path, int coverW, int coverH);

// True iff the cache file exists on disk.
bool exists();

// Read the cache header and return the number of entries.  Returns 0
// when the file is missing or corrupt.
int getCount();

// Load a contiguous range of entries [start, start+count) into `out`.
// Returns the number of entries actually loaded (may be < count near the
// end of the library, or 0 on I/O error).
// count should not exceed kEntryWindow.
int loadPage(std::vector<Entry>& out, int start, int count);

// Overwrite the cache file with `entries` in their current order, writing
// v3 format with an offset footer.
// This is used only by removeBook() and low-level testing; normal
// sync/scan write directly to the cache without building the full vector.
bool save(const std::vector<Entry>& entries);

// Delete the cache file. Idempotent.
void invalidate();

// Remove the entry whose `path` matches and rewrite the cache file.
// Uses the v3 offset index for path lookup without loading all entries.
bool removeBook(const std::string& path);

// Incremental sync: compare the cached library against the real SD content.
// - Reads the cached offset index into RAM (4 × count bytes).
// - Walks the SD subtree under `rootDir`.
// - For each SD book, looks up its path in the cached offset index by
//   loading each cached path from file via seek (one at a time).
// - When a cached match is found, copies its title/author (also loaded
//   from file).
// - For newly discovered books, parses metadata.
// - Removes cached entries whose files no longer exist on SD.
// - Writes the merged result directly to the cache file in v3 format.
//   No Entry vector is materialized in RAM; sync streams to disk.
//
// Falls back to a full scan() when the cache file is missing or read fails.
//
// `rootDir` restricts the SD walk to a subtree (default "/" = entire card).
// Progress is shown via `popupRect` only when a full scan fallback occurs;
// incremental sync is fast and runs without a popup.
bool sync(GfxRenderer* renderer, const Rect* popupRect, const char* rootDir = "/", int maxBooks = 10000);

// Full SD walk: enumerate every ebook under `rootDir`, parse metadata via
// the cheap `Epub::load(true, true)` or `Xtc::load()` / `Txt::load()` path,
// sort, and persist to the cache file in v3 format.
// Cover thumbnails are NOT generated here — they are created on-demand
// per visible page via `generateCoverForBook()`.
//
// Progress is reported by filling `popupRect` — the caller is responsible
// for drawing the surrounding popup chrome before invoking.  `maxBooks`
// caps the result (default 10000).
bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir = "/", int maxBooks = 10000);

}  // namespace LibraryCache
