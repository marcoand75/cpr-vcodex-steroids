#pragma once

#include <string>
#include <cstdint>

class Epub;
class StorageBase;

// Preserve the current reading position for a book by reading progress data
// from either the stable or legacy path. Returns the raw progress bytes on
// success, or an empty string on failure.
std::string preserveBookReadingPosition(const std::string& bookPath, const std::string& stableBookId);

// Restore reading position for a book by writing progress data to the cache dir.
// Should be called after cache directory has been recreated.
bool restoreBookReadingPosition(const std::string& cachePath, const std::string& progressData);

// Clear the rendering/layout cache for a single book while preserving reading
// position and reading stats. Uses the same backup/restore logic as the
// in-reader DELETE_CACHE handler.
void clearBookCache(const std::string& path);

bool isBookCacheDirectoryName(const char* name);
