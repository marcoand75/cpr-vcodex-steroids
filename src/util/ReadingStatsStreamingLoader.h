#pragma once

#include <HalStorage.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstddef>

#include "ReadingStatsStore.h"

// Custom utility for streaming reading_stats.json load.
// Keeps upstream JsonSettingsIO.cpp changes minimal; this file is
// steroids-specific and safe to drop during upstream merge.

namespace ReadingStatsStreamingLoader {

// Multi-pass filter load: reads the stream once per array key,
// using a small StaticJsonDocument filtered to only that array.
// This avoids loading the full 80 KB document into a single
// DynamicJsonDocument.
bool loadFromFileStreaming(const char* moduleName, const char* path,
                            ReadingStatsStore& store,
                            bool (*loadDocument)(ReadingStatsStore&, const JsonDocument&));

}  // namespace ReadingStatsStreamingLoader
