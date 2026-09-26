#include "util/ReadingStatsStreamingLoader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Stream.h>
#include <Logging.h>

#include "ReadingStatsStore.h"

namespace ReadingStatsStreamingLoader {

class HalFileStream : public Stream {
 public:
  explicit HalFileStream(HalFile& file) : file_(file) {}
  int available() override { return file_.available() + (peekedByte_ >= 0 ? 1 : 0); }
  int read() override {
    if (peekedByte_ >= 0) {
      int b = peekedByte_;
      peekedByte_ = -1;
      return b;
    }
    return file_.read();
  }
  int peek() override {
    if (peekedByte_ < 0) peekedByte_ = file_.read();
    return peekedByte_;
  }
  void flush() override { file_.flush(); }
  size_t write(uint8_t value) override { return file_.write(value); }
 private:
  HalFile& file_;
  int peekedByte_ = -1;
};

bool loadFromFileStreaming(const char* moduleName, const char* path,
                            ReadingStatsStore& store,
                            bool (*loadDocument)(ReadingStatsStore&, const JsonDocument&)) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    return false;
  }
  HalFileStream stream(file);
  JsonDocument doc;
  auto error = deserializeJson(doc, stream);
  file.close();
  if (error || doc.overflowed()) {
    const char* msg = error ? error.c_str() : "overflow";
    LOG_ERR("RST", "Streaming load parse error (%s) for %s: %s", moduleName, path, msg);
    return false;
  }
  return loadDocument(store, doc);
}

}  // namespace ReadingStatsStreamingLoader
