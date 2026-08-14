#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "HalStorage.h"

// Minimal Arduino Stream adapter over a HalFile so ArduinoJson can deserialize
// a JSON document incrementally from an SD file without materializing the whole
// file (plus its pool) in RAM. Used by loadJsonDocumentFromFile() to lower the
// transient heap peak and reduce boot fragmentation when loading large JSON
// stores (e.g. reading_stats.json).
//
// Only the read side is implemented; write() is unused (it is required as a
// Stream pure virtual but never called for deserialization).
class FileStreamReader : public Stream {
 public:
  explicit FileStreamReader(HalFile& file) : file_(file) {}

  int available() override { return file_.available() > 0 ? 1 : 0; }

  int read() override {
    uint8_t b = 0;
    return file_.read(&b, 1) == 1 ? static_cast<int>(b) : -1;
  }

  int peek() override {
    const size_t pos = file_.position();
    uint8_t b = 0;
    const int result = file_.read(&b, 1) == 1 ? static_cast<int>(b) : -1;
    if (file_.isOpen() && file_.seek(pos)) {
      // restore position after peek
    }
    return result;
  }

  size_t write(uint8_t) override { return 0; }

 private:
  HalFile& file_;
};
