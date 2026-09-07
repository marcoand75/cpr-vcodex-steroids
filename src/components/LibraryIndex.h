#pragma once

#include <cstdint>
#include <string>

class GfxRenderer;
struct Rect;

namespace LibraryIndex {

// ---- Fixed-length on-disk record (256 bytes) ----
// Layout:
//   [0-3]   id          uint32_t  unique, stable, never reassigned
//   [4-67]  title       char[64]  UTF-8, null-terminated
//   [68-115] author     char[48]  UTF-8, null-terminated
//   [116-243] path      char[128] absolute SD path
//   [244-247] file_size  uint32_t
//   [248]    flags       uint8_t   bit0=tombstone, bit1=favorite, bit2=opened, bit3=completed
//   [249-252] mtime      uint32_t  file modification timestamp
//   [253-255] reserved   uint8_t[3]
struct __attribute__((packed)) Record {
  uint32_t id;
  char     title[64];
  char     author[48];
  char     path[128];
  uint32_t file_size;
  uint8_t  flags;
  uint32_t mtime;
  uint8_t  reserved[3];

  // Convenience
  bool tombstone() const { return (flags & 0x01) != 0; }
  bool favorite()  const { return (flags & 0x02) != 0; }
  bool opened()    const { return (flags & 0x04) != 0; }
  bool completed() const { return (flags & 0x08) != 0; }

  void setTombstone(bool v) { if (v) flags |= 0x01; else flags &= ~0x01; }
  void setFavorite(bool v)  { if (v) flags |= 0x02; else flags &= ~0x02; }
  void setOpened(bool v)    { if (v) flags |= 0x04; else flags &= ~0x04; }
  void setCompleted(bool v) { if (v) flags |= 0x08; else flags &= ~0x08; }
};
static_assert(sizeof(Record) == 256, "Record must be 256 bytes");

// ---- In-RAM view for one rendered tile ----
struct __attribute__((packed)) BookRef {
  uint32_t id;
  char title[65];   // +1 for safe null-termination
  char author[49];
  char path[129];
  bool isFavorite;
  bool isOpened;
  bool isCompleted;
  bool isHidden;
};
static_assert(sizeof(BookRef) <= 260, "BookRef fits in stack");

// ---- Sort mode (matches CrossPointSettings::LIBRARY_SORT) ----
enum class SortMode {
  TITLE_ASC = 0,
  TITLE_DESC = 1,
  AUTHOR_ASC = 2,
  AUTHOR_DESC = 3,
  RECENT = 4,
  PROGRESS = 5,
  COLLECTIONS = 6,
  MIXED = 7,
};

// ---- Filter mode ----
enum class FilterMode {
  ALL = 0,
  FAVOURITES = 1,
  LATEST_READ = 2,
  UNREAD = 3,
  COMPLETED = 4,
  HIDDEN = 5,
};

// ---- Public API ----
// All functions return false on I/O error and log the reason.
// RAM footprint is documented in each function comment.

// One-time init: creates /.crosspoint/LIBRARY/ if needed, opens dat file.
// RAM: <1 KB.
bool init();

// Returns true if library.dat exists and has at least one non-tombstone record.
bool exists();

// Full SD scan + metadata extraction. Writes library.dat and scan_state.dat.
// Shows progress via popupRect on GfxRenderer (pass zero-size Rect to skip).
// Returns true on success.  Populates outAdded/outRemoved (can be nullptr).
bool scan(GfxRenderer& renderer, const Rect& popupRect, const char* rootDir = "/",
          int* outAdded = nullptr, int* outRemoved = nullptr);

// External merge-sort: rebuilds idx_title.bin and idx_author.bin from
// library.dat. Must be called after scan() when records changed.
// RAM: configurable chunk size (4 KB default, see BUILDFLAGS).
bool buildIndices();

// Build collections index from series.dat (must be called after scan)
bool buildCollectionsIndex();

// Build mixed index from library.dat + series.dat + collections index.
// One entry per standalone book + one entry per collection/series tile.
bool buildMixedIndex();

// Incremental sync: runs scan() only if library.dat is stale or missing.
// Falls back to a fast path when nothing changed.
// RAM: same as scan() + buildIndices().
bool sync(const char* rootDir = "/");

// Writes up to `pageSize` BookRefs into `out`, starting at zero-based
// page `page` (0 = first page).  Returns number of items written (0 on
// end-of-data or error).
// sortMode: which index to use and direction
// searchFilter: if non-null and non-empty, apply full-text substring
//   filter on title AND author (case-insensitive, accent-normalised).
// filterMode: additional static filter (favourites / recent / unread).
// RAM: ~(pageSize * sizeof(BookRef)) + 1 KB I/O buffer.
int queryPage(BookRef* out, int page, int pageSize, SortMode sortMode,
              const char* searchFilter = nullptr, FilterMode filterMode = FilterMode::ALL);

// Mixed view: standalone books + series tiles together
int queryMixed(BookRef* out, int page, int pageSize);
int totalMixed();

// Collections: list unique collections
int queryCollections(BookRef* out, int page, int pageSize);

// Books within a specific collection (by index in idx_collections.bin)
int queryCollectionBooks(BookRef* out, int page, int pageSize, int collectionIdx);

// Number of unique collections
int totalCollections();

// Number of books in a specific collection
int collectionBookCount(int collectionIdx);

// Total number of non-tombstone books (fast, from index header).
int totalBooks();

// Total number of non-tombstone books matching a full-text filter.
// Slower than totalBooks() because it must scan (used for pagination of
// search results).
int totalMatching(const char* searchFilter, FilterMode filterMode = FilterMode::ALL);

// Deletes library.dat, scan_state.dat, and both index files.
// Also removes tmp/ chunks.  Next scan() will rebuild from scratch.
void invalidate();

// Legacy cover path helper (delegates to same logic as before).
// Kept for LibraryActivity compatibility.
std::string thumbPathFor(const std::string& bookPath, int coverW, int coverH);

}  // namespace LibraryIndex
