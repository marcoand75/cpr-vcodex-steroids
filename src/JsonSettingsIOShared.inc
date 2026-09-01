#pragma once
// Shared internal helpers for JsonSettingsIO.cpp and JsonSettingsIOSteroids.cpp
// Included (not compiled separately) by both translation units.
//
// Prerequisites: the including file must already have included:
//   <ArduinoJson.h>, <HalStorage.h>, <Logging.h>, <ObfuscationUtils.h>,
//   <Stream.h>, "CrossPointSettings.h", "util/CprVcodexLogs.h"

constexpr uint8_t FONT_FAMILY_SCHEMA_VERSION = 2;
constexpr uint8_t FONT_SIZE_SCHEMA_VERSION = 2;
constexpr uint8_t UI_THEME_SCHEMA_VERSION = 3;
constexpr uint8_t TEXT_DARKNESS_SCHEMA_VERSION = 2;
constexpr uint8_t FLASHCARD_STUDY_MODE_SCHEMA_VERSION = 2;

class HalFileStream : public Stream {
 public:
  explicit HalFileStream(HalFile& file) : file(file) {}

  int available() override { return file.available() + (peekedByte >= 0 ? 1 : 0); }

  int read() override {
    if (peekedByte >= 0) {
      const int byte = peekedByte;
      peekedByte = -1;
      return byte;
    }
    return file.read();
  }

  int peek() override {
    if (peekedByte < 0) {
      peekedByte = file.read();
    }
    return peekedByte;
  }

  void flush() override { file.flush(); }

  size_t write(uint8_t value) override { return file.write(value); }

 private:
  HalFile& file;
  int peekedByte = -1;
};

static bool copyVerifiedJsonTempToTarget(const char* moduleName, const std::string& tempPath,
                                  const std::string& targetPath, const size_t expectedSize) {
  HalFile source;
  if (!Storage.openFileForRead(moduleName, tempPath.c_str(), source)) {
    return false;
  }

  HalFile target;
  if (!Storage.openFileForWrite(moduleName, targetPath.c_str(), target)) {
    source.close();
    return false;
  }

  uint8_t buffer[256];
  size_t copied = 0;
  bool complete = true;
  while (true) {
    const int readBytes = source.read(buffer, sizeof(buffer));
    if (readBytes < 0) {
      complete = false;
      break;
    }
    if (readBytes == 0) {
      break;
    }

    const size_t written = target.write(buffer, static_cast<size_t>(readBytes));
    if (written != static_cast<size_t>(readBytes)) {
      complete = false;
      break;
    }
    copied += written;
  }

  target.flush();
  target.close();
  source.close();

  if (!complete || copied != expectedSize) {
    Storage.remove(targetPath.c_str());
    return false;
  }

  HalFile verification;
  if (!Storage.openFileForRead(moduleName, targetPath.c_str(), verification)) {
    Storage.remove(targetPath.c_str());
    return false;
  }
  const bool sizeMatches = verification.fileSize64() == expectedSize;
  verification.close();
  if (!sizeMatches) {
    Storage.remove(targetPath.c_str());
    return false;
  }

  return true;
}

static bool promoteJsonTempFile(const char* moduleName, const char* tempPath, const char* targetPath,
                         const size_t expectedSize) {
  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {
    Storage.remove(tempPath);
    LOG_ERR(moduleName, "Could not remove JSON file before replace: %s", targetPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not remove JSON file before replace: ") + targetPath);
    return false;
  }

  if (!Storage.rename(tempPath, targetPath)) {
    LOG_ERR(moduleName, "Could not rename JSON temp file to final path: %s; trying checked copy", targetPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("JSON temp rename failed; trying checked copy for ") + targetPath);

    if (!copyVerifiedJsonTempToTarget(moduleName, tempPath, targetPath, expectedSize)) {
      LOG_ERR(moduleName, "Could not promote JSON temp file to final path: %s", targetPath);
      CPR_VCODEX_LOG_EVENT(moduleName,
                           std::string("Could not promote JSON temp file; kept it for recovery: ") + tempPath);
      return false;
    }

    Storage.remove(tempPath);
    LOG_DBG(moduleName, "Recovered JSON replacement via checked copy: %s", targetPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Recovered JSON replacement via checked copy: ") + targetPath);
  }

  return true;
}

class JsonStreamWriter {
 public:
  explicit JsonStreamWriter(HalFile& file) : file_(file) {}

  void literal(const char* text) {
    if (!ok_ || !text) return;
    const size_t length = std::strlen(text);
    expectedBytes_ += length;
    const size_t written = file_.write(text, length);
    writtenBytes_ += written;
    if (written != length) ok_ = false;
  }

  void value(const std::string& text) { writeScalar(JsonString(text.data(), text.size(), true)); }

  template <typename T>
  void value(const T value) {
    writeScalar(value);
  }

  bool ok() const { return ok_; }
  size_t expectedBytes() const { return expectedBytes_; }
  size_t writtenBytes() const { return writtenBytes_; }

 private:
  template <typename T>
  void writeScalar(const T value) {
    if (!ok_) return;

    JsonDocument scalar;
    if (!scalar.set(value) || scalar.overflowed()) {
      ok_ = false;
      return;
    }

    const size_t expected = measureJson(scalar);
    const size_t written = serializeJson(scalar, file_);
    expectedBytes_ += expected;
    writtenBytes_ += written;
    if (written != expected) ok_ = false;
  }

  HalFile& file_;
  size_t expectedBytes_ = 0;
  size_t writtenBytes_ = 0;
  bool ok_ = true;
};

static bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  const std::string targetPath = path ? path : "";
  const std::string tempPath = targetPath + ".tmp";

  if (targetPath.empty()) {
    LOG_ERR(moduleName, "Missing JSON path for write");
    CPR_VCODEX_LOG_EVENT(moduleName, "Missing JSON path for write");
    return false;
  }

  if (doc.overflowed()) {
    LOG_ERR(moduleName, "JSON document overflowed before write: %s", targetPath.c_str());
    // Do NOT allocate std::string here — when the JSON doc has overflowed the
    // heap is likely fragmented and a string concatenation would OOM-crash.
    CPR_VCODEX_LOG_EVENT(moduleName, "Refused to write overflowed JSON document");
    return false;
  }

  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite(moduleName, tempPath.c_str(), file)) {
    LOG_ERR(moduleName, "Could not open JSON file for write: %s", tempPath.c_str());
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not open JSON temp file for write: ") + tempPath);
    return false;
  }

  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written == 0 || written != expected) {
    Storage.remove(tempPath.c_str());
    LOG_ERR(moduleName, "Incomplete JSON write for %s: %u/%u bytes", targetPath.c_str(),
            static_cast<unsigned>(written), static_cast<unsigned>(expected));
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Incomplete JSON write for ") + targetPath + ": " +
                                         std::to_string(written) + "/" + std::to_string(expected) + " bytes");
    return false;
  }

  return promoteJsonTempFile(moduleName, tempPath.c_str(), targetPath.c_str(), written);
}

static bool loadJsonDocumentFromFile(const char* moduleName, const char* path, JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for read: %s", path);
    if (Storage.exists(path)) {
      CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not open JSON file for read: ") + path);
    }
    return false;
  }

  HalFileStream stream(file);
  auto error = deserializeJson(doc, stream);
  file.close();
  if (error || doc.overflowed()) {
    const char* message = error ? error.c_str() : "document overflow";
    LOG_ERR(moduleName, "JSON parse error: %s", message);
#ifndef CPR_DISABLE_EVENT_LOGS
    const std::string reportBody =
        std::string("File: ") + path + "\nModule: " + moduleName + "\nError: " + message + "\n";
    std::string outPath;
    if (CPR_VCODEX_WRITE_REPORT("json_error", reportBody, &outPath)) {
      CPR_VCODEX_LOG_EVENT(moduleName, std::string("Saved JSON parse error report to ") + outPath);
    }
#endif
    return false;
  }
  return true;
}

static uint8_t migrateStoredUiTheme(const uint8_t rawUiTheme, const uint8_t schemaVersion, const uint8_t currentDefault,
                             bool* needsResave) {
  if (schemaVersion >= UI_THEME_SCHEMA_VERSION) {
    const uint8_t clampedTheme =
        rawUiTheme < static_cast<uint8_t>(CrossPointSettings::UI_THEME_COUNT) ? rawUiTheme : currentDefault;
    if (clampedTheme != rawUiTheme && needsResave) *needsResave = true;
    return clampedTheme;
  }

  // Legacy/theme-consolidation migration:
  // - 0 (Classic) -> Lyra
  // - 2/3 (Extended/Custom) -> Lyra vCodex
  // - 1 is ambiguous: in the current 2-theme schema it already means Lyra vCodex,
  //   while in older schemas it meant Lyra. Prefer preserving the newer stored value.
  uint8_t migratedTheme = currentDefault;
  switch (rawUiTheme) {
    case 0:
      migratedTheme = CrossPointSettings::LYRA;
      break;
    case 1:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    case 2:
    case 3:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    default:
      migratedTheme = currentDefault;
      break;
  }

  if (migratedTheme != rawUiTheme && needsResave) *needsResave = true;
  return migratedTheme;
}

static uint8_t migrateStoredFlashcardStudyMode(const uint8_t rawMode, const uint8_t schemaVersion,
                                        const uint8_t currentDefault, bool* needsResave) {
  if (schemaVersion >= FLASHCARD_STUDY_MODE_SCHEMA_VERSION) {
    const uint8_t clampedMode =
        rawMode < static_cast<uint8_t>(CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT) ? rawMode : currentDefault;
    if (clampedMode != rawMode && needsResave) *needsResave = true;
    return clampedMode;
  }

  // Legacy mapping before Due existed:
  // - 0 -> Scheduled
  // - 1 -> Infinite
  uint8_t migratedMode = currentDefault;
  switch (rawMode) {
    case 0:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_SCHEDULED;
      break;
    case 1:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_INFINITE;
      break;
    default:
      migratedMode = currentDefault;
      break;
  }

  if (migratedMode != rawMode && needsResave) *needsResave = true;
  return migratedMode;
}

static void migrateLegacyStatsShortcut(CrossPointSettings& settings, const JsonDocument& doc, bool* needsResave) {
  const bool hasLegacyStatsShortcut =
      !doc["statsShortcut"].isNull() || !doc["statsShortcutOrder"].isNull() || !doc["statsShortcutVisible"].isNull();
  if (!hasLegacyStatsShortcut) {
    return;
  }

  const bool legacyVisible = settings.statsShortcutVisible != 0;
  const auto legacyLocation = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(settings.statsShortcut);
  if (legacyVisible &&
      (legacyLocation == CrossPointSettings::SHORTCUT_HOME || legacyLocation == CrossPointSettings::SHORTCUT_APPS)) {
    settings.readingStatsShortcut = settings.statsShortcut;
    settings.readingStatsShortcutOrder = settings.statsShortcutOrder;
    settings.readingStatsShortcutVisible = 1;
  }

  settings.statsShortcutVisible = 0;
  if (needsResave) *needsResave = true;
}
// end static helpers


