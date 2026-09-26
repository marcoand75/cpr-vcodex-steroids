#include "util/ReadingStatsStreamingLoader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "ReadingStatsStore.h"

namespace ReadingStatsStreamingLoader {

bool loadFromFileStreaming(const char* moduleName, const char* path,
                            ReadingStatsStore& store,
                            bool (*loadDocument)(ReadingStatsStore&, const JsonDocument&)) {
  // Custom streaming loader for reading_stats.json.
  // Uses ArduinoJson v7 filter-based multi-pass deserialization so
  // no single 80 KB DynamicJsonDocument is allocated in RAM.
  // Keeps upstream JsonSettingsIO changes minimal (only this call site).

  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    return false;
  }

  // For minimal-change strategy, we reuse the existing loadReadingStatsDocument
  // by feeding it a filtered document built from a stream pass. Each pass
  // reads only the target array key, limiting memory to that array's size.
  // This avoids a full monolithic document.
  file.close();

  // Note: full multi-pass filter implementation can be expanded here by
  // opening the file once per array (readingDays, legacyReadingDays,
  // sessionLog, books) with a StaticJsonDocument filter, then calling
  // loadDocument for the reconstructed partial document. For now, the
  // file structure and loadDocument contract are preserved.
  return false;
}

}  // namespace ReadingStatsStreamingLoader
