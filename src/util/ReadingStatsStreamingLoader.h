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
                            ReadingStatsStore& store,
                            bool (*loadDocument)(ReadingStatsStore&, const JsonDocument&));

}  // namespace ReadingStatsStreamingLoader
