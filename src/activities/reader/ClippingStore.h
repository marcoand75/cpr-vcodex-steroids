#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <esp_rom_crc.h>

#include "util/BookIdentity.h"

class ClippingStore {
 public:
  struct Clipping {
    uint16_t spineIndex = 0;
    uint16_t startPage = 0;
    uint16_t endPage = 0;
    uint16_t pageCount = 1;
    uint16_t startWordIndex = 0;
    uint16_t endWordIndex = 0;
    uint16_t wordCount = 0;
    uint16_t paragraphIndex = UINT16_MAX;
    uint32_t absoluteWordStart = UINT32_MAX;  // v2: invariant word index from chapter start
    uint32_t timestamp = 0;
    char chapterTitle[48] = "";
    std::string selectedText;
  };

  void load(const std::string& bookPath) {
    clippings.clear();
    dirty = false;

    if (bookPath.empty()) {
      storagePath.clear();
      return;
    }

    const std::string normalized = BookIdentity::normalizePath(bookPath);
    const uint32_t hash = calculatePathHash(normalized);
    storagePath = "/.crosspoint/clippings/epub_" + std::to_string(hash) + ".bin";

    FsFile file;
    if (!Storage.openFileForRead("CLP", storagePath, file)) {
      return;
    }

    uint8_t version = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 || version > FILE_VERSION) {
      file.close();
      return;
    }
    const bool isV2 = (version >= 2);

    uint16_t count = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count)) {
      file.close();
      return;
    }
    count = std::min(count, static_cast<uint16_t>(MAX_CLIPPINGS));

    std::string title;
    std::string author;
    std::string path;
    if (!readString(file, title) || !readString(file, author) || !readString(file, path)) {
      file.close();
      return;
    }

    clippings.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
      Clipping clipping;
      if (file.read(reinterpret_cast<uint8_t*>(&clipping.spineIndex), sizeof(clipping.spineIndex)) !=
              sizeof(clipping.spineIndex) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.startPage), sizeof(clipping.startPage)) !=
              sizeof(clipping.startPage) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.endPage), sizeof(clipping.endPage)) !=
              sizeof(clipping.endPage) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.pageCount), sizeof(clipping.pageCount)) !=
              sizeof(clipping.pageCount) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.startWordIndex), sizeof(clipping.startWordIndex)) !=
              sizeof(clipping.startWordIndex) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.endWordIndex), sizeof(clipping.endWordIndex)) !=
              sizeof(clipping.endWordIndex) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.wordCount), sizeof(clipping.wordCount)) !=
              sizeof(clipping.wordCount) ||
          file.read(reinterpret_cast<uint8_t*>(&clipping.paragraphIndex), sizeof(clipping.paragraphIndex)) !=
              sizeof(clipping.paragraphIndex)) {
        clippings.clear();
        file.close();
        return;
      }
      // v2: absolute word index from chapter start (invariant across layout changes)
      if (isV2 &&
          file.read(reinterpret_cast<uint8_t*>(&clipping.absoluteWordStart), sizeof(clipping.absoluteWordStart)) !=
              sizeof(clipping.absoluteWordStart)) {
        clippings.clear();
        file.close();
        return;
      }
      if (file.read(reinterpret_cast<uint8_t*>(&clipping.timestamp), sizeof(clipping.timestamp)) !=
              sizeof(clipping.timestamp)) {
        clippings.clear();
        file.close();
        return;
      }

      if (file.read(reinterpret_cast<uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
          sizeof(clipping.chapterTitle)) {
        clippings.clear();
        file.close();
        return;
      }
      clipping.chapterTitle[sizeof(clipping.chapterTitle) - 1] = '\0';

      if (!readString(file, clipping.selectedText)) {
        clippings.clear();
        file.close();
        return;
      }

      clippings.push_back(clipping);
    }

    file.close();
  }

  void save() {
    if (!dirty || storagePath.empty()) {
      return;
    }

    ensureClippingsDirExists();

    FsFile file;
    if (!Storage.openFileForWrite("CLP", storagePath, file)) {
      LOG_ERR("CLP", "Failed to open clippings file for write: %s", storagePath.c_str());
      return;
    }

    const uint16_t count = static_cast<uint16_t>(std::min(clippings.size(), static_cast<size_t>(MAX_CLIPPINGS)));
    bool ok = writePod(file, FILE_VERSION);
    ok = ok && writePod(file, count);

    std::string title;
    std::string author;
    ok = ok && writeString(file, title) && writeString(file, author) && writeString(file, bookPath);

    for (uint16_t i = 0; i < count && ok; ++i) {
      const auto& c = clippings[i];
      ok = ok && writePod(file, c.spineIndex) && writePod(file, c.startPage) && writePod(file, c.endPage) &&
           writePod(file, c.pageCount) && writePod(file, c.startWordIndex) && writePod(file, c.endWordIndex) &&
           writePod(file, c.wordCount) && writePod(file, c.paragraphIndex) && writePod(file, c.absoluteWordStart) &&
           writePod(file, c.timestamp);
      ok = ok && file.write(reinterpret_cast<const uint8_t*>(c.chapterTitle), sizeof(c.chapterTitle)) ==
                     sizeof(c.chapterTitle);
      ok = ok && writeString(file, truncateText(c.selectedText, MAX_TEXT_BYTES));
    }

    ok = ok && file.close();
    if (!ok) {
      LOG_ERR("CLP", "Failed while writing clippings");
      return;
    }

    dirty = false;
  }

  bool add(const Clipping& clipping) {
    if (clippings.size() >= MAX_CLIPPINGS) {
      return false;
    }

    clippings.push_back(clipping);
    dirty = true;
    return true;
  }

  bool remove(size_t index) {
    if (index >= clippings.size()) {
      return false;
    }

    clippings.erase(clippings.begin() + static_cast<std::ptrdiff_t>(index));
    dirty = true;
    return true;
  }

  void clear() {
    if (clippings.empty()) {
      return;
    }
    clippings.clear();
    dirty = true;
  }

  [[nodiscard]] const std::vector<Clipping>& getAll() const { return clippings; }
  [[nodiscard]] bool isEmpty() const { return clippings.empty(); }
  [[nodiscard]] size_t size() const { return clippings.size(); }
  void markDirty() { dirty = true; }
  [[nodiscard]] const std::string& getStoragePath() const { return storagePath; }
  void setBookPath(const std::string& path) {
    bookPath = path;
    if (!path.empty() && storagePath.empty()) {
      const std::string normalized = BookIdentity::normalizePath(path);
      const uint32_t hash = calculatePathHash(normalized);
      storagePath = "/.crosspoint/clippings/epub_" + std::to_string(hash) + ".bin";
    }
  }

 private:
  static constexpr uint8_t FILE_VERSION = 2;
  static constexpr uint16_t MAX_CLIPPINGS = 64;
  static constexpr size_t MAX_TEXT_BYTES = 512;

  std::vector<Clipping> clippings;
  std::string storagePath;
  std::string bookPath;
  bool dirty = false;

  static bool writePod(FsFile& file, const auto& value) {
    return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
  }

  static bool readString(FsFile& file, std::string& out) {
    uint32_t len = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) {
      return false;
    }
    if (len > 65535) {
      return false;
    }
    out.resize(len);
    if (len > 0 && file.read(reinterpret_cast<uint8_t*>(&out[0]), len) != len) {
      return false;
    }
    return true;
  }

  static bool writeString(FsFile& file, const std::string& s) {
    const uint32_t len = static_cast<uint32_t>(std::min(s.size(), static_cast<size_t>(65535)));
    return writePod(file, len) && file.write(reinterpret_cast<const uint8_t*>(s.data()), len) == len;
  }

  static std::string truncateText(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
      return text;
    }
    std::string truncated = text.substr(0, maxBytes);
    while (!truncated.empty() && (static_cast<uint8_t>(truncated.back()) & 0xC0) == 0x80) {
      truncated.pop_back();
    }
    return truncated;
  }

  static uint32_t calculatePathHash(const std::string& path) {
    uint32_t hash = UINT32_MAX;
    for (char c : path) {
      hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&c), 1);
    }
    return hash;
  }

  static bool ensureClippingsDirExists() {
    static bool dirChecked = false;
    if (!dirChecked) {
      Storage.mkdir("/.crosspoint");
      const bool ok = Storage.mkdir("/.crosspoint/clippings");
      dirChecked = true;
      return ok;
    }
    return true;
  }
};
