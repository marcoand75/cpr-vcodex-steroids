#pragma once

#include <HalStorage.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstddef>

#include "ReadingStatsStore.h"

// Custom streaming loader for reading_stats.json (format v6).
// Approach (strada B): manual incremental byte scanner over HalFile.
// No DynamicJsonDocument, no full-file String buffer. Peak RAM stays
// well under 32 KB: only a 1-byte stream buffer + current array element
// + the growing store vectors (unavoidable user data).
// Keeps upstream JsonSettingsIO.cpp untouched (single call site only).
namespace ReadingStatsStreamingLoader {

bool loadFromFileStreaming(const char* moduleName, const char* path,
                           ReadingStatsStore& store);

// Streaming import pipeline: validate the source stats JSON (bounded stream
// buffer, the ~40KB fat store is never materialized), copy it over
// targetStatsJsonPath (temp + verified rename), then regenerate summaryJsonPath
// by streaming aggregation (compact day vector + per-book badges). Returns
// false when the source is rejected — nothing is modified in that case.
bool importStatsFileStreaming(const char* sourcePath, const char* targetStatsJsonPath,
                              const char* summaryJsonPath, uint32_t referenceDayOrdinal,
                              uint64_t goalReadingMs);

}  // namespace ReadingStatsStreamingLoader
