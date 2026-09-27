#include "util/ReadingStatsStreamingLoader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Stream.h>
#include <Logging.h>

#include "ReadingStatsStore.h"
#include "util/CprVcodexLogs.h"

#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

namespace ReadingStatsStreamingLoader {

namespace {

// Byte-level scanner over HalFile. Keeps RAM minimal: reads 1 byte at a time,
// no full-file buffer, no DynamicJsonDocument for parsing.
struct ByteScanner {
  HalFile* file = nullptr;
  char byte = 0;
  bool hasByte = false;

  int readByte() {
    char b[1];
    int r = file->read(b, 1);
    return (r > 0) ? static_cast<unsigned char>(b[0]) : -1;
  }

  int next() {
    if (hasByte) {
      hasByte = false;
      int c = static_cast<unsigned char>(byte);
      byte = 0;
      return c;
    }
    return readByte();
  }

  void unget(int c) {
    hasByte = true;
    byte = static_cast<char>(c);
  }

  int skipSpace() {
    int c;
    for (;;) {
      c = next();
      if (c < 0) return c;
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        return c;
      }
    }
  }
};

// Assumes opening '"' has already been consumed. Reads content until closing '"'.
bool readQuotedString(ByteScanner& s, std::string& out) {
  out.clear();
  for (;;) {
    int c = s.next();
    if (c < 0) return false;
    if (c == '"') return true;
    if (c == '\\') {
      int esc = s.next();
      if (esc < 0) return false;
      switch (esc) {
        case '"': case '\\': case '/': out.push_back(static_cast<char>(esc)); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': // consume 4 hex digits (no full Unicode decode for RAM)
          for (int i = 0; i < 4; ++i) {
            if (s.next() < 0) return false;
          }
          break;
        default:
          out.push_back(static_cast<char>(esc));
          break;
      }
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
}

// Skip a quoted string (opening '"' already consumed).
bool skipQuotedString(ByteScanner& s) {
  for (;;) {
    int c = s.next();
    if (c < 0) return false;
    if (c == '"') return true;
    if (c == '\\') {
      if (s.next() < 0) return false;
    }
  }
}

// Skip JSON value (object, array, string, number, literal) without allocating.
bool skipValue(ByteScanner& s) {
  int c = s.skipSpace();
  if (c < 0) return false;

  if (c == '"') {
    return skipQuotedString(s);
  }
  if (c == '{' || c == '[') {
    int depth = 1;
    while (depth > 0) {
      int sc = s.skipSpace();
      if (sc < 0) return false;
      if (sc == '"') {
        if (!skipQuotedString(s)) return false;
      } else if (sc == '{' || sc == '[') {
        depth++;
      } else if (sc == '}' || sc == ']') {
        depth--;
      }
      // numbers / true / false / null / commas are just skipped by the loop
    }
    return true;
  }
  // Number, true, false, null: read until delimiter
  for (;;) {
    if (c < 0) return false;
    if (c == ',' || c == '}' || c == ']') {
      s.unget(c);
      return true;
    }
    c = s.next();
  }
}

// Read unsigned integer (positive only).
bool readUint32(ByteScanner& s, uint32_t& out) {
  int c = s.skipSpace();
  if (c < '0' || c > '9') {
    if (c >= 0) s.unget(c);
    return false;
  }
  uint32_t v = 0;
  for (;;) {
    if (c >= '0' && c <= '9') {
      // simple overflow protection
      if (v > (UINT32_MAX - (c - '0')) / 10) return false;
      v = v * 10 + static_cast<uint32_t>(c - '0');
      c = s.next();
    } else {
      if (c >= 0) s.unget(c);
      out = v;
      return true;
    }
  }
}

bool readUint64(ByteScanner& s, uint64_t& out) {
  int c = s.skipSpace();
  if (c < '0' || c > '9') {
    if (c >= 0) s.unget(c);
    return false;
  }
  uint64_t v = 0;
  for (;;) {
    if (c >= '0' && c <= '9') {
      if (v > (UINT64_MAX - (c - '0')) / 10) return false;
      v = v * 10 + static_cast<uint64_t>(c - '0');
      c = s.next();
    } else {
      if (c >= 0) s.unget(c);
      out = v;
      return true;
    }
  }
}

// Array of strings -> dest
bool parseStringArray(ByteScanner& s, std::vector<std::string>& dest) {
  int c = s.skipSpace();
  if (c != '[') return false;
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '"') return false;
    std::string str;
    if (!readQuotedString(s, str)) return false;
    dest.push_back(std::move(str));

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    return false;
  }
  return true;
}

// Array of ReadingDayStats
bool parseReadingDayArray(ByteScanner& s, std::vector<ReadingDayStats>& dest) {
  int c = s.skipSpace();
  if (c != '[') return false;
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '{') return false;

    ReadingDayStats day{};
    bool hasDay = false, hasMs = false;

    for (;;) {
      int first = s.skipSpace();
      if (first == '}') break;
      if (first != '"') return false;

      std::string key;
      if (!readQuotedString(s, key)) return false;

      int colonC = s.skipSpace();
      if (colonC != ':') return false;

      if (key == "dayOrdinal") {
        hasDay = true;
        if (!readUint32(s, day.dayOrdinal)) return false;
      } else if (key == "readingMs") {
        hasMs = true;
        if (!readUint64(s, day.readingMs)) return false;
      } else {
        if (!skipValue(s)) return false;
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') continue;
      if (afterVal == '}') break;
      return false;
    }

    if (hasDay && hasMs && day.dayOrdinal != 0) {
      dest.push_back(day);
    }

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    return false;
  }
  return true;
}

// Array of session log entries
bool parseSessionArray(ByteScanner& s, std::vector<ReadingSessionLogEntry>& dest) {
  int c = s.skipSpace();
  if (c != '[') return false;
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '{') return false;

    ReadingSessionLogEntry entry{};
    bool hasDay = false, hasMs = false;
    std::string bookIdStr, pathStr;

    for (;;) {
      int first = s.skipSpace();
      if (first == '}') break;
      if (first != '"') return false;

      std::string key;
      if (!readQuotedString(s, key)) return false;

      int colonC = s.skipSpace();
      if (colonC != ':') return false;

      if (key == "dayOrdinal") {
        hasDay = true;
        if (!readUint32(s, entry.dayOrdinal)) return false;
      } else if (key == "sessionMs") {
        hasMs = true;
        if (!readUint32(s, entry.sessionMs)) return false;
      } else if (key == "bookId") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, bookIdStr)) return false;
      } else if (key == "path") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, pathStr)) return false;
      } else {
        if (!skipValue(s)) return false;
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') continue;
      if (afterVal == '}') break;
      return false;
    }

    if (hasDay && hasMs && entry.dayOrdinal != 0 && entry.sessionMs != 0) {
      entry.bookId = std::move(bookIdStr);
      entry.path = std::move(pathStr);
      dest.push_back(std::move(entry));
    }

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    return false;
  }
  return true;
}

// Books array (now also parses knownPaths + readingDays)
bool parseBooksArray(ByteScanner& s, std::vector<ReadingBookStats>& dest) {
  int c = s.skipSpace();
  if (c != '[') {
    LOG_ERR("RST", "parseBooksArray: expected '[' got %d", c);
    return false;
  }
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '{') {
      LOG_ERR("RST", "parseBooksArray: expected '{', got %d", after);
      return false;
    }

    ReadingBookStats book{};
    std::string bookIdStr, pathStr;

    for (;;) {
      int first = s.skipSpace();
      if (first == '}') break;
      if (first != '"') {
        LOG_ERR("RST", "parseBooksArray: expected key quote, got %d", first);
        return false;
      }

      std::string key;
      if (!readQuotedString(s, key)) {
        LOG_ERR("RST", "parseBooksArray: failed reading key");
        return false;
      }

      int colonC = s.skipSpace();
      if (colonC != ':') {
        LOG_ERR("RST", "parseBooksArray: expected ':' after key=%s got %d", key.c_str(), colonC);
        return false;
      }

      if (key == "bookId") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, bookIdStr)) {
          LOG_ERR("RST", "parseBooksArray: failed reading bookId");
          return false;
        }
      } else if (key == "path") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, pathStr)) {
          LOG_ERR("RST", "parseBooksArray: failed reading path");
          return false;
        }
      } else if (key == "title") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, book.title)) {
          LOG_ERR("RST", "parseBooksArray: failed reading title");
          return false;
        }
      } else if (key == "author") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, book.author)) {
          LOG_ERR("RST", "parseBooksArray: failed reading author");
          return false;
        }
      } else if (key == "coverBmpPath") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, book.coverBmpPath)) {
          LOG_ERR("RST", "parseBooksArray: failed reading coverBmpPath");
          return false;
        }
      } else if (key == "chapterTitle") {
        int q = s.skipSpace();
        if (q != '"') return false;
        if (!readQuotedString(s, book.chapterTitle)) {
          LOG_ERR("RST", "parseBooksArray: failed reading chapterTitle");
          return false;
        }
      } else if (key == "totalReadingMs") {
        if (!readUint64(s, book.totalReadingMs)) {
          LOG_ERR("RST", "parseBooksArray: failed reading totalReadingMs");
          return false;
        }
      } else if (key == "sessions") {
        if (!readUint32(s, book.sessions)) {
          LOG_ERR("RST", "parseBooksArray: failed reading sessions");
          return false;
        }
      } else if (key == "lastSessionMs") {
        if (!readUint32(s, book.lastSessionMs)) {
          LOG_ERR("RST", "parseBooksArray: failed reading lastSessionMs");
          return false;
        }
      } else if (key == "firstReadAt") {
        if (!readUint32(s, book.firstReadAt)) {
          LOG_ERR("RST", "parseBooksArray: failed reading firstReadAt");
          return false;
        }
      } else if (key == "lastReadAt") {
        if (!readUint32(s, book.lastReadAt)) {
          LOG_ERR("RST", "parseBooksArray: failed reading lastReadAt");
          return false;
        }
      } else if (key == "completedAt") {
        if (!readUint32(s, book.completedAt)) {
          LOG_ERR("RST", "parseBooksArray: failed reading completedAt");
          return false;
        }
      } else if (key == "lastProgressPercent") {
        uint32_t v = 0;
        if (!readUint32(s, v)) {
          LOG_ERR("RST", "parseBooksArray: failed reading lastProgressPercent");
          return false;
        }
        book.lastProgressPercent = static_cast<uint8_t>(v);
      } else if (key == "chapterProgressPercent") {
        uint32_t v = 0;
        if (!readUint32(s, v)) {
          LOG_ERR("RST", "parseBooksArray: failed reading chapterProgressPercent");
          return false;
        }
        book.chapterProgressPercent = static_cast<uint8_t>(v);
      } else if (key == "completed") {
        int cLiteral = s.skipSpace();
        if (cLiteral == 't') {
          if (s.next() != 'r' || s.next() != 'u' || s.next() != 'e') {
            LOG_ERR("RST", "parseBooksArray: invalid true literal");
            return false;
          }
          book.completed = true;
        } else if (cLiteral == 'f') {
          if (s.next() != 'a' || s.next() != 'l' || s.next() != 's' || s.next() != 'e') {
            LOG_ERR("RST", "parseBooksArray: invalid false literal");
            return false;
          }
          book.completed = false;
        } else {
          LOG_ERR("RST", "parseBooksArray: unexpected completed token %d", cLiteral);
          return false;
        }
      } else if (key == "knownPaths") {
        if (!parseStringArray(s, book.knownPaths)) {
          LOG_ERR("RST", "parseBooksArray: failed parsing knownPaths");
          return false;
        }
      } else if (key == "readingDays") {
        if (!parseReadingDayArray(s, book.readingDays)) {
          LOG_ERR("RST", "parseBooksArray: failed parsing readingDays");
          return false;
        }
      } else {
        // avgSecondsPerForwardPage, paceSampleCount, unknown fields…
        if (!skipValue(s)) {
          LOG_ERR("RST", "parseBooksArray: failed skipping unknown key=%s", key.c_str());
          return false;
        }
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') continue;
      if (afterVal == '}') break;
      LOG_ERR("RST", "parseBooksArray: after key=%s expected ',' or '}', got %d", key.c_str(), afterVal);
      return false;
    }

    book.bookId = std::move(bookIdStr);
    book.path = std::move(pathStr);
    if (!book.path.empty()) {
      dest.push_back(std::move(book));
    }

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    LOG_ERR("RST", "parseBooksArray: expected ',' or ']' between books, got %d", afterElem);
    return false;
  }
  return true;
}

} // anonymous namespace

bool loadFromFileStreaming(const char* moduleName, const char* path,
                           ReadingStatsStore& store) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    LOG_ERR("RST", "Loader open failed: %s", path);
    return false;
  }

  ByteScanner s;
  s.file = &file;

  auto logRam = [&]() {
    LOG_DBG("RST", "Loader RAM: free=%u largest=%u",
            static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT)));
  };
  logRam();

  int rootC = s.skipSpace();
  if (rootC != '{') {
    file.close();
    LOG_ERR("RST", "Loader root not object (got %d) for %s", rootC, path);
    return false;
  }

  // Populate store vectors directly during parsing to avoid temporary copies.
  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = false;

  bool hasFormatVersion = false;
  uint32_t formatVersion = 1;
  bool foundDataArray = false;

  for (;;) {
    int firstKey = s.skipSpace();
    if (firstKey == '}') break;
    if (firstKey != '"') {
      file.close();
      LOG_ERR("RST", "Loader invalid root token: %d (expected key)", firstKey);
      return false;
    }

    std::string keyStr;
    if (!readQuotedString(s, keyStr)) {
      file.close();
      return false;
    }

    int colonC = s.skipSpace();
    if (colonC != ':') {
      file.close();
      return false;
    }

    int valFirst = s.skipSpace();
    if (valFirst < 0) {
      file.close();
      return false;
    }
    s.unget(valFirst);

    if (keyStr == "formatVersion") {
      uint32_t v = 0;
      if (!readUint32(s, v)) {
        file.close();
        LOG_ERR("RST", "Loader invalid formatVersion");
        return false;
      }
      formatVersion = v;
      hasFormatVersion = true;
    } else if (keyStr == "readingDays") {
      foundDataArray = true;
      if (!parseReadingDayArray(s, store.readingDays)) {
        file.close();
        LOG_ERR("RST", "Loader parseReadingDayArray failed");
        return false;
      }
    } else if (keyStr == "legacyReadingDays") {
      foundDataArray = true;
      if (!parseReadingDayArray(s, store.legacyReadingDays)) {
        file.close();
        LOG_ERR("RST", "Loader parseLegacyReadingDayArray failed");
        return false;
      }
    } else if (keyStr == "sessionLog") {
      foundDataArray = true;
      if (!parseSessionArray(s, store.sessionLog)) {
        file.close();
        LOG_ERR("RST", "Loader parseSessionArray failed");
        return false;
      }
    } else if (keyStr == "books") {
      foundDataArray = true;
      if (!parseBooksArray(s, store.books)) {
        file.close();
        LOG_ERR("RST", "Loader parseBooksArray failed");
        return false;
      }
    } else {
      if (!skipValue(s)) {
        file.close();
        LOG_ERR("RST", "Loader skipValue failed for key=%s", keyStr.c_str());
        return false;
      }
    }

    int afterVal = s.skipSpace();
    if (afterVal == ',') continue;
    if (afterVal == '}') break;
    if (afterVal < 0) {
      file.close();
      return false;
    }
    file.close();
    LOG_ERR("RST", "Loader unexpected token after value: %d", afterVal);
    return false;
  }

  file.close();
  logRam();
  LOG_DBG("RST", "Loader manual parse complete: format=%u days=%zu legacy=%zu books=%zu sessions=%zu free=%u",
          formatVersion, store.readingDays.size(), store.legacyReadingDays.size(),
          store.books.size(), store.sessionLog.size(), static_cast<unsigned>(ESP.getFreeHeap()));

  if (formatVersion == 0 || formatVersion > 6) {
    LOG_ERR("RST", "Loader unsupported formatVersion: %u", formatVersion);
    return false;
  }
  if (!foundDataArray) {
    LOG_ERR("RST", "Loader no recognized data arrays found");
    return false;
  }

  // Post-processing: replicate loadReadingStatsDocument logic directly on store.
  static constexpr const char* ARRAY_KEYS[] = {"readingDays", "legacyReadingDays", "sessionLog", "books"};
  bool missingCurrentArray = false;
  for (const char* key : ARRAY_KEYS) {
    if (key == std::string("readingDays") && store.readingDays.empty()) missingCurrentArray = true;
    if (key == std::string("legacyReadingDays") && store.legacyReadingDays.empty()) missingCurrentArray = true;
    if (key == std::string("sessionLog") && store.sessionLog.empty()) missingCurrentArray = true;
    if (key == std::string("books") && store.books.empty()) missingCurrentArray = true;
  }
  store.dirty = missingCurrentArray;

  if (formatVersion >= 2) {
    if (formatVersion < 6 && store.legacyReadingDays.empty()) {
      store.legacyReadingDays = store.readingDays;
    }
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    if (store.sessionLog.size() > ReadingSessionLog::MAX_ENTRIES) {
      store.sessionLog.erase(store.sessionLog.begin(),
                             store.sessionLog.begin() + static_cast<std::ptrdiff_t>(store.sessionLog.size() - ReadingSessionLog::MAX_ENTRIES));
      store.dirty = true;
    }
  } else {
    store.dirty = true;
  }

  for (auto& book : store.books) {
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
  }

  if (formatVersion < 6) {
    store.convertLegacyReadingDaysToUnassigned();
    store.dirty = true;
  }
  store.rebuildAggregatedReadingDays();

  if (formatVersion >= 6) {
    std::vector<ReadingDayStats> declaredReadingDays = store.readingDays;
    auto normalizeDays = [](std::vector<ReadingDayStats>& days) {
      std::sort(days.begin(), days.end(), [](const ReadingDayStats& left, const ReadingDayStats& right) {
        return left.dayOrdinal < right.dayOrdinal;
      });
      size_t writeIndex = 0;
      for (const auto& day : days) {
        if (day.dayOrdinal == 0 || day.readingMs == 0) continue;
        if (writeIndex > 0 && days[writeIndex - 1].dayOrdinal == day.dayOrdinal) {
          days[writeIndex - 1].readingMs += day.readingMs;
        } else {
          days[writeIndex++] = day;
        }
      }
      days.resize(writeIndex);
    };
    normalizeDays(declaredReadingDays);

    bool aggregateMismatch = declaredReadingDays.size() != store.readingDays.size();
    for (const auto& declaredDay : declaredReadingDays) {
      const auto rebuiltIt =
          std::lower_bound(store.readingDays.begin(), store.readingDays.end(), declaredDay.dayOrdinal,
                           [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
      const bool hasRebuiltDay =
          rebuiltIt != store.readingDays.end() && rebuiltIt->dayOrdinal == declaredDay.dayOrdinal;
      const uint64_t rebuiltMs = hasRebuiltDay ? rebuiltIt->readingMs : 0;
      if (rebuiltMs != declaredDay.readingMs) {
        aggregateMismatch = true;
      }
      if (declaredDay.readingMs > rebuiltMs) {
        store.legacyReadingDays.push_back(ReadingDayStats{declaredDay.dayOrdinal, declaredDay.readingMs - rebuiltMs});
      }
    }

    if (aggregateMismatch) {
      normalizeDays(store.legacyReadingDays);
      store.rebuildAggregatedReadingDays();
      store.dirty = true;
      CPR_VCODEX_LOG_EVENT("RST", "Reconciled reading stats aggregate totals without discarding stored data");
    }
  }

  std::stable_sort(store.sessionLog.begin(), store.sessionLog.end(),
                   [](const ReadingSessionLogEntry& left, const ReadingSessionLogEntry& right) {
                     return left.dayOrdinal < right.dayOrdinal;
                   });
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

} // namespace ReadingStatsStreamingLoader
