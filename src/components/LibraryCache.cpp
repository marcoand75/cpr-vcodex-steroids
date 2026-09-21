#include "LibraryCache.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "LibraryIndex.h"

namespace LibraryCache {

std::string thumbPathFor(const std::string& path, int coverW, int coverH) {
  return LibraryIndex::thumbPathFor(path, coverW, coverH);
}

bool exists() {
  return LibraryIndex::exists();
}

bool load(std::vector<Entry>& out) {
  out.clear();
  HalFile f = Storage.open("/.crosspoint/LIBRARY/library.dat");
  if (!f) return false;
  const size_t fsz = f.size();
  if (fsz < sizeof(LibraryIndex::Record)) { f.close(); return false; }
  const int count = static_cast<int>(fsz / sizeof(LibraryIndex::Record));
  out.reserve(count);
  LibraryIndex::Record rec;
  for (int i = 0; i < count; ++i) {
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(LibraryIndex::Record)) != static_cast<int>(sizeof(LibraryIndex::Record))) break;
    if (rec.tombstone()) continue;
    Entry e;
    e.path.assign(rec.path, strnlen(rec.path, sizeof(rec.path)));
    e.title.assign(rec.title, strnlen(rec.title, sizeof(rec.title)));
    e.author.assign(rec.author, strnlen(rec.author, sizeof(rec.author)));
    out.push_back(std::move(e));
  }
  f.close();
  LOG_DBG("LIB", "LibraryCache::load: %u entries from library.dat", out.size());
  return true;
}

bool save(const std::vector<Entry>&) {
  // Saving is handled by LibraryIndex::scan/buildIndices.
  // This function exists for backward compat but is no longer used.
  return true;
}

void invalidate() {
  LibraryIndex::invalidate();
}

bool removeBook(const std::string& path) {
  // Mark the record as tombstone in library.dat
  HalFile f = Storage.open("/.crosspoint/LIBRARY/library.dat");
  if (!f) return false;
  LibraryIndex::Record rec;
  while (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(LibraryIndex::Record)) == static_cast<int>(sizeof(LibraryIndex::Record))) {
    if (strncmp(rec.path, path.c_str(), sizeof(rec.path)) == 0 && !rec.tombstone()) {
      rec.setTombstone(true);
      const size_t pos = f.position() - sizeof(LibraryIndex::Record);
      f.seek(pos);
      f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(LibraryIndex::Record));
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

bool sync(std::vector<Entry>& out, const char* rootDir, int) {
  // sync now delegates to LibraryIndex::scan + buildIndices, then load.
  // For backward compat, we do a quick check and fall through.
  if (!LibraryIndex::exists()) return false;

  // TODO: incremental sync — for now, load() is the fast path.
  return load(out);
}

bool scan(GfxRenderer& renderer, const Rect& popupRect, std::vector<Entry>& out,
          const char* rootDir, int) {
  if (!LibraryIndex::scan(renderer, popupRect, rootDir)) return false;
  if (!LibraryIndex::buildIndices()) return false;
  return load(out);
}

}  // namespace LibraryCache
