#pragma once

#include <cstddef>
#include <cstdint>

namespace ReadingStatsImportStreaming {

// Streaming import pipeline for reading stats (format v6):
// 1. validate the source JSON structurally (formatVersion + data arrays)
//    with a bounded stream buffer;
// 2. copy it over targetStatsJsonPath (temp file + verified rename);
// 3. regenerate summaryJsonPath by streaming aggregation (compact day vector
//    + per-book badges).
//
// The ~40KB fat reading-stats store is NEVER materialized: peak RAM is the
// stream buffer + the compact day vector + the summary document. Returns
// false when the source is rejected — nothing is modified in that case.
bool importStatsFileStreaming(const char* sourcePath, const char* targetStatsJsonPath,
                              const char* summaryJsonPath, uint32_t referenceDayOrdinal,
                              uint64_t goalReadingMs);

}  // namespace ReadingStatsImportStreaming
