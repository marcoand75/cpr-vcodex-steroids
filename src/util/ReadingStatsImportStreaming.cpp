#include "util/ReadingStatsImportStreaming.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "ReadingStatsStore.h"

namespace ReadingStatsImportStreaming {

namespace {

// Declarable streamer budget (same semantics as the JSON loader buffer).
constexpr size_t kReadStreamBytes = 1024;

struct ByteScanner {
  HalFile* file = nullptr;
  char buf[kReadStreamBytes];
  size_t pos = 0;
  size_t len = 0;

  int refill() {
    pos = 0;
    len = file->read(reinterpret_cast<uint8_t*>(buf), kReadStreamBytes);
    return static_cast<int>(len);
  }

  int next() {
    if (pos >= len) {
      if (refill() <= 0) {
        return -1;
      }
    }
    return static_cast<unsigned char>(buf[pos++]);
  }

  void unget(int c) {
    if (pos > 0) {
      --pos;
      buf[pos] = static_cast<char>(c);
    }
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
        case 'u':
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

bool skipValue(ByteScanner& s) {
  int c = s.skipSpace();
  if (c < 0) return false;

  if (c == '"') {
    for (;;) {
      c = s.next();
      if (c < 0) return false;
      if (c == '"') return true;
      if (c == '\\') {
        if (s.next() < 0) return false;
      }
    }
  }
  if (c == '{' || c == '[') {
    int depth = 1;
    while (depth > 0) {
      int sc = s.skipSpace();
      if (sc < 0) return false;
      if (sc == '"') {
        for (;;) {
          int q = s.next();
          if (q < 0) return false;
          if (q == '"') break;
          if (q == '\\') {
            if (s.next() < 0) return false;
          }
        }
      } else if (sc == '{' || sc == '[') {
        depth++;
      } else if (sc == '}' || sc == ']') {
        depth--;
      }
    }
    return true;
  }
  for (;;) {
    if (c < 0) return false;
    if (c == ',' || c == '}' || c == ']') {
      s.unget(c);
      return true;
    }
    c = s.next();
  }
}

bool readUint32(ByteScanner& s, uint32_t& out) {
  int c = s.skipSpace();
  if (c < '0' || c > '9') {
    if (c >= 0) s.unget(c);
    return false;
  }
  uint32_t v = 0;
  for (;;) {
    if (c >= '0' && c <= '9') {
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

// Array of day objects -> compact vector (peak RAM: entries * 12 bytes).
bool parseReadingDayArray(ByteScanner& s, std::vector<ReadingDayStats>& dest) {
  int c = s.skipSpace();
  if (c != '[') {
    LOG_ERR("RST", "parseReadingDayArray: expected '[' got %d", c);
    return false;
  }
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '{') {
      LOG_ERR("RST", "parseReadingDayArray: expected '{', got %d", after);
      return false;
    }

    ReadingDayStats day{};
    bool hasDay = false, hasMs = false;

    for (;;) {
      int first = s.skipSpace();
      if (first == '}') break;
      if (first != '"') {
        LOG_ERR("RST", "parseReadingDayArray: expected key quote, got %d", first);
        return false;
      }

      std::string key;
      if (!readQuotedString(s, key)) {
        LOG_ERR("RST", "parseReadingDayArray: failed reading key");
        return false;
      }

      int colonC = s.skipSpace();
      if (colonC != ':') {
        LOG_ERR("RST", "parseReadingDayArray: expected ':' after key=%s got %d", key.c_str(), colonC);
        return false;
      }

      if (key == "dayOrdinal") {
        hasDay = true;
        if (!readUint32(s, day.dayOrdinal)) {
          LOG_ERR("RST", "parseReadingDayArray: failed reading dayOrdinal");
          return false;
        }
      } else if (key == "readingMs") {
        hasMs = true;
        if (!readUint64(s, day.readingMs)) {
          LOG_ERR("RST", "parseReadingDayArray: failed reading readingMs");
          return false;
        }
      } else {
        if (!skipValue(s)) {
          LOG_ERR("RST", "parseReadingDayArray: skipValue failed for key=%s", key.c_str());
          return false;
        }
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') continue;
      if (afterVal == '}') break;
      LOG_ERR("RST", "parseReadingDayArray: after key=%s expected ',' or '}', got %d", key.c_str(), afterVal);
      return false;
    }

    if (hasDay && hasMs && day.dayOrdinal != 0) {
      dest.push_back(day);
    }

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    LOG_ERR("RST", "parseReadingDayArray: expected ',' or ']' between days, got %d", afterElem);
    return false;
  }
  return true;
}

struct ImportCounters {
  uint32_t formatVersion = 0;
  bool hasVersion = false;
  bool hasData = false;
  size_t books = 0;
  size_t days = 0;
  size_t sessions = 0;
};

// Structural walk of the root object: reads formatVersion, counts array
// elements (skipValue per element — no data materialization).
bool validateStatsStreaming(ByteScanner& s, ImportCounters& counters) {
  int rootC = s.skipSpace();
  if (rootC != '{') {
    LOG_ERR("RST", "Import validate: root not object");
    return false;
  }

  for (;;) {
    int firstKey = s.skipSpace();
    if (firstKey == '}') break;
    if (firstKey != '"') {
      LOG_ERR("RST", "Import validate: invalid root token %d", firstKey);
      return false;
    }

    std::string key;
    if (!readQuotedString(s, key)) {
      LOG_ERR("RST", "Import validate: failed reading key");
      return false;
    }

    int colonC = s.skipSpace();
    if (colonC != ':') {
      LOG_ERR("RST", "Import validate: expected ':' after key=%s got %d", key.c_str(), colonC);
      return false;
    }

    int valFirst = s.skipSpace();
    if (valFirst < 0) {
      LOG_ERR("RST", "Import validate: unexpected EOF after key=%s", key.c_str());
      return false;
    }
    s.unget(valFirst);

    if (key == "formatVersion") {
      counters.hasVersion = true;
      if (!readUint32(s, counters.formatVersion)) {
        LOG_ERR("RST", "Import validate: bad formatVersion");
        return false;
      }
    } else if (key == "readingDays" || key == "legacyReadingDays" || key == "sessionLog" || key == "books") {
      counters.hasData = true;
      int c = s.skipSpace();
      if (c != '[') {
        LOG_ERR("RST", "Import validate: key=%s expected '['", key.c_str());
        return false;
      }
      size_t count = 0;
      for (;;) {
        int after = s.skipSpace();
        if (after == ']') break;
        if (after < 0) {
          LOG_ERR("RST", "Import validate: unexpected EOF in array key=%s", key.c_str());
          return false;
        }
        s.unget(after);
        if (!skipValue(s)) {
          LOG_ERR("RST", "Import validate: skipValue failed for key=%s element=%u", key.c_str(), static_cast<unsigned>(count));
          return false;
        }
        ++count;
        int afterElem = s.skipSpace();
        if (afterElem == ',') continue;
        if (afterElem == ']') break;
        LOG_ERR("RST", "Import validate: malformed array for key=%s", key.c_str());
        return false;
      }
      if (key == "books") counters.books = count;
      else if (key == "readingDays") counters.days = count;
      else if (key == "sessionLog") counters.sessions = count;
    } else {
      if (!skipValue(s)) {
        LOG_ERR("RST", "Import validate: skipValue failed for key=%s", key.c_str());
        return false;
      }
    }

    int afterVal = s.skipSpace();
    if (afterVal == ',') continue;
    if (afterVal == '}') break;
    LOG_ERR("RST", "Import validate: unexpected token %d after key=%s", afterVal, key.c_str());
    return false;
  }

  if (!counters.hasVersion || counters.formatVersion == 0 || counters.formatVersion > 6) {
    LOG_ERR("RST", "Import validate: unsupported formatVersion %u", static_cast<unsigned>(counters.formatVersion));
    return false;
  }
  if (!counters.hasData || (counters.books == 0 && counters.days == 0)) {
    LOG_ERR("RST", "Import validate: no recognized data (books=%u days=%u sessions=%u)", static_cast<unsigned>(counters.books), static_cast<unsigned>(counters.days), static_cast<unsigned>(counters.sessions));
    return false;
  }
  return true;
}

// Per-book aggregation entry (RAM: one badge record; strings transient).
bool parseBooksSummaryLite(ByteScanner& s, std::vector<SummaryJSON::BookBadge>& badges, uint32_t& finishedCount) {
  int c = s.skipSpace();
  if (c != '[') {
    LOG_ERR("RST", "parseBooksSummaryLite: expected '[' got %d", c);
    return false;
  }
  for (;;) {
    int after = s.skipSpace();
    if (after == ']') break;
    if (after != '{') {
      LOG_ERR("RST", "parseBooksSummaryLite: expected '{', got %d", after);
      return false;
    }

    SummaryJSON::BookBadge badge;

    for (;;) {
      int first = s.skipSpace();
      if (first == '}') break;
      if (first != '"') {
        LOG_ERR("RST", "parseBooksSummaryLite: expected key quote, got %d", first);
        return false;
      }

      std::string key;
      if (!readQuotedString(s, key)) {
        LOG_ERR("RST", "parseBooksSummaryLite: failed reading key");
        return false;
      }

      int colonC = s.skipSpace();
      if (colonC != ':') {
        LOG_ERR("RST", "parseBooksSummaryLite: expected ':' after key=%s got %d", key.c_str(), colonC);
        return false;
      }

      if (key == "bookId") {
        int q = s.skipSpace();
        if (q != '"') {
          LOG_ERR("RST", "parseBooksSummaryLite: bookId expected string got %d", q);
          return false;
        }
        if (!readQuotedString(s, badge.bookId)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed reading bookId");
          return false;
        }
      } else if (key == "path") {
        int q = s.skipSpace();
        if (q != '"') {
          LOG_ERR("RST", "parseBooksSummaryLite: path expected string got %d", q);
          return false;
        }
        if (!readQuotedString(s, badge.path)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed reading path");
          return false;
        }
      } else if (key == "totalReadingMs") {
        uint64_t v = 0;
        if (!readUint64(s, v)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed reading totalReadingMs");
          return false;
        }
        badge.totalReadingMs = v;
      } else if (key == "sessions") {
        uint32_t v = 0;
        if (!readUint32(s, v)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed reading sessions");
          return false;
        }
        badge.sessions = v;
      } else if (key == "lastProgressPercent") {
        uint32_t v = 0;
        if (!readUint32(s, v)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed reading lastProgressPercent");
          return false;
        }
        badge.progressPercent = static_cast<uint8_t>(v > 100 ? 100 : v);
      } else if (key == "completed") {
        int c2 = s.skipSpace();
        if (c2 == 't') {
          // consume "rue"
          if (s.next() != 'r' || s.next() != 'u' || s.next() != 'e') {
            LOG_ERR("RST", "parseBooksSummaryLite: invalid true literal");
            return false;
          }
          badge.completed = true;
        } else if (c2 == 'f') {
          for (int i = 0; i < 4; ++i) {
            if (s.next() < 0) {
              LOG_ERR("RST", "parseBooksSummaryLite: truncated false literal");
              return false;
            }
          }
          badge.completed = false;
        } else {
          LOG_ERR("RST", "parseBooksSummaryLite: unexpected completed token %d", c2);
          return false;
        }
      } else if (key == "readingDays") {
        std::vector<ReadingDayStats> tmp;
        if (!parseReadingDayArray(s, tmp)) {
          LOG_ERR("RST", "parseBooksSummaryLite: failed parsing readingDays");
          return false;
        }
        badge.readingDaysCount = static_cast<uint32_t>(tmp.size());
      } else {
        if (!skipValue(s)) {
          LOG_ERR("RST", "parseBooksSummaryLite: skipValue failed for key=%s", key.c_str());
          return false;
        }
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') continue;
      if (afterVal == '}') break;
      LOG_ERR("RST", "parseBooksSummaryLite: after key=%s expected ',' or '}', got %d", key.c_str(), afterVal);
      return false;
    }

    if (!badge.bookId.empty() || !badge.path.empty()) {
      badges.push_back(badge);
      if (badge.completed) ++finishedCount;
    }

    int afterElem = s.skipSpace();
    if (afterElem == ',') continue;
    if (afterElem == ']') break;
    LOG_ERR("RST", "parseBooksSummaryLite: expected ',' or ']' between books, got %d", afterElem);
    return false;
  }
  return true;
}

bool writeSummaryFile(const SummaryJSON& json, const char* summaryPath) {
  JsonDocument doc;
  JsonObject summary = doc["summary"].to<JsonObject>();
  summary["totalReadingMs"] = json.global.totalReadingMs;
  summary["todayReadingMs"] = json.global.todayReadingMs;
  summary["recent7ReadingMs"] = json.global.recent7ReadingMs;
  summary["recent30ReadingMs"] = json.global.recent30ReadingMs;
  summary["currentStreakDays"] = json.global.currentStreakDays;
  summary["maxStreakDays"] = json.global.maxStreakDays;
  summary["booksFinishedCount"] = json.global.booksFinishedCount;
  summary["goalReadingMs"] = json.global.goalReadingMs;
  summary["dailyAverageMs"] = json.global.dailyAverageMs;
  summary["referenceDayOrdinal"] = json.global.referenceDayOrdinal;

  JsonArray badges = doc["bookBadges"].to<JsonArray>();
  for (const auto& badge : json.bookBadges) {
    JsonObject obj = badges.add<JsonObject>();
    obj["bookId"] = badge.bookId;
    obj["path"] = badge.path;
    obj["completed"] = badge.completed;
    obj["progressPercent"] = badge.progressPercent;
    obj["totalReadingMs"] = badge.totalReadingMs;
    obj["sessions"] = badge.sessions;
    obj["readingDaysCount"] = badge.readingDaysCount;
  }

  String serialized;
  serializeJson(doc, serialized);
  return Storage.writeFile(summaryPath, serialized);
}

}  // namespace

bool importStatsFileStreaming(const char* sourcePath, const char* targetStatsJsonPath,
                              const char* summaryJsonPath, uint32_t referenceDayOrdinal,
                              uint64_t goalReadingMs) {
  if (!sourcePath || !targetStatsJsonPath || !summaryJsonPath) return false;
  if (!Storage.exists(sourcePath)) {
    LOG_ERR("RST", "Import streaming: source missing");
    return false;
  }

  // --- Pass 1: structural validation (no data materialization) ---
  {
    HalFile file;
    if (!Storage.openFileForRead("RST", sourcePath, file)) {
      LOG_ERR("RST", "Import validate: open failed");
      return false;
    }
    ByteScanner s;
    s.file = &file;
    ImportCounters counters;
    const bool ok = validateStatsStreaming(s, counters);
    file.close();
    if (!ok) return false;
  }

  // --- Copy: source -> temp(target) -> verified rename ---
  {
    const std::string tempPath = std::string(targetStatsJsonPath) + ".tmp";
    HalFile source;
    HalFile temp;
    if (!Storage.openFileForRead("RST", sourcePath, source)) {
      LOG_ERR("RST", "Import copy: source open failed");
      return false;
    }
    if (Storage.exists(tempPath.c_str())) {
      Storage.remove(tempPath.c_str());
    }
    if (!Storage.openFileForWrite("RST", tempPath.c_str(), temp)) {
      source.close();
      LOG_ERR("RST", "Import copy: temp open failed");
      return false;
    }
    char chunk[kReadStreamBytes];
    bool copyOk = true;
    size_t copiedBytes = 0;
    const size_t sourceSize = source.fileSize64();
    for (;;) {
      const int got = source.read(reinterpret_cast<uint8_t*>(chunk), kReadStreamBytes);
      if (got < 0) {
        copyOk = false;
        break;
      }
      if (got == 0) break;
      if (temp.write(reinterpret_cast<uint8_t*>(chunk), static_cast<size_t>(got)) != static_cast<size_t>(got)) {
        copyOk = false;
        break;
      }
      copiedBytes += static_cast<size_t>(got);
    }
    temp.flush();
    temp.close();
    source.close();
    if (!copyOk) {
      Storage.remove(tempPath.c_str());
      LOG_ERR("RST", "Import copy: short write");
      return false;
    }
    if (copiedBytes != sourceSize) {
      Storage.remove(tempPath.c_str());
      LOG_ERR("RST", "Import copy: byte count mismatch copied=%u expected=%u", static_cast<unsigned>(copiedBytes), static_cast<unsigned>(sourceSize));
      return false;
    }
    if (Storage.exists(targetStatsJsonPath)) {
      Storage.remove(targetStatsJsonPath);
    }
    if (!Storage.rename(tempPath.c_str(), targetStatsJsonPath)) {
      Storage.remove(tempPath.c_str());
      LOG_ERR("RST", "Import copy: rename failed");
      return false;
    }
  }

  // --- Streaming summary regeneration from the copied file ---
  {
    HalFile file;
    if (!Storage.openFileForRead("RST", targetStatsJsonPath, file)) {
      LOG_ERR("RST", "Import summary: open failed");
      return false;
    }
    // Peek first 16 bytes for debugging
    char peekBuf[16];
    file.read(reinterpret_cast<uint8_t*>(peekBuf), 16);
    file.seek(0);
    LOG_DBG("RST", "Import summary: file header=%.16s", peekBuf);

    ByteScanner s;
    s.file = &file;

    std::vector<ReadingDayStats> days;
    std::vector<SummaryJSON::BookBadge> books;
    uint32_t finishedCount = 0;
    bool parseOk = true;

    // Check root object opening
    int rootFirst = s.skipSpace();
    LOG_DBG("RST", "Import summary: root first token=%d (%c)", rootFirst, rootFirst >= 32 && rootFirst < 127 ? rootFirst : '?');
    if (rootFirst != '{') {
      LOG_ERR("RST", "Import summary: root not object (got %d)", rootFirst);
      file.close();
      return false;
    }

    for (;;) {
      int firstKey = s.skipSpace();
      if (firstKey == '}') {
        LOG_DBG("RST", "Import summary: end of root object");
        break;
      }
      if (firstKey != '"') {
        LOG_ERR("RST", "Import summary: unexpected token %d (expected key)", firstKey);
        parseOk = false;
        break;
      }

      std::string key;
      if (!readQuotedString(s, key)) {
        LOG_ERR("RST", "Import summary: failed reading key");
        parseOk = false;
        break;
      }
      LOG_DBG("RST", "Import summary: processing key=%s", key.c_str());

      int colonC = s.skipSpace();
      if (colonC != ':') {
        LOG_ERR("RST", "Import summary: expected ':' after key=%s got %d", key.c_str(), colonC);
        parseOk = false;
        break;
      }

      int valFirst = s.skipSpace();
      if (valFirst < 0) {
        LOG_ERR("RST", "Import summary: unexpected EOF after key=%s", key.c_str());
        parseOk = false;
        break;
      }
      s.unget(valFirst);

      if (key == "readingDays") {
        if (!parseReadingDayArray(s, days)) {
          LOG_ERR("RST", "Import summary: parse failed in readingDays");
          parseOk = false;
          break;
        }
        LOG_DBG("RST", "Import summary: finished readingDays, days=%u", static_cast<unsigned>(days.size()));
      } else if (key == "books") {
        if (!parseBooksSummaryLite(s, books, finishedCount)) {
          LOG_ERR("RST", "Import summary: parse failed in books");
          parseOk = false;
          break;
        }
        LOG_DBG("RST", "Import summary: finished books, badges=%u", static_cast<unsigned>(books.size()));
      } else {
        if (!skipValue(s)) {
          LOG_ERR("RST", "Import summary: skipValue failed for key=%s", key.c_str());
          parseOk = false;
          break;
        }
        LOG_DBG("RST", "Import summary: finished skipValue for key=%s", key.c_str());
      }

      int afterVal = s.skipSpace();
      if (afterVal == ',') {
        LOG_DBG("RST", "Import summary: next key expected after key=%s", key.c_str());
        continue;
      }
      if (afterVal == '}') {
        LOG_DBG("RST", "Import summary: end of object after key=%s", key.c_str());
        break;
      }
      LOG_ERR("RST", "Import summary: unexpected token %d after key=%s", afterVal, key.c_str());
      parseOk = false;
      break;
    }
    file.close();
    if (!parseOk) {
      LOG_ERR("RST", "Import summary: parse failed (days=%u badges=%u)", static_cast<unsigned>(days.size()), static_cast<unsigned>(books.size()));
      return false;
    }

    // Merge duplicate ordinals + sort (mirrors normalizeReadingDays).
    std::sort(days.begin(), days.end(), [](const ReadingDayStats& left, const ReadingDayStats& right) {
      return left.dayOrdinal < right.dayOrdinal;
    });
    std::vector<ReadingDayStats> merged;
    for (const auto& day : days) {
      if (!merged.empty() && merged.back().dayOrdinal == day.dayOrdinal) {
        merged.back().readingMs += day.readingMs;
      } else {
        merged.push_back(day);
      }
    }

    SummaryJSON json;
    json.global.referenceDayOrdinal = referenceDayOrdinal;
    json.global.goalReadingMs = goalReadingMs;
    for (const auto& book : books) {
      if (book.totalReadingMs == 0 && !book.completed) continue;
      json.bookBadges.push_back(book);
    }

    if (referenceDayOrdinal != 0) {
      const uint32_t start7DayOrdinal = (referenceDayOrdinal >= 6) ? (referenceDayOrdinal - 6) : 0;
      const uint32_t start30DayOrdinal = (referenceDayOrdinal >= 29) ? (referenceDayOrdinal - 29) : 0;

      std::vector<uint32_t> eligibleDays;
      eligibleDays.reserve(merged.size());

      for (const auto& day : merged) {
        if (day.dayOrdinal == referenceDayOrdinal) {
          json.global.todayReadingMs = day.readingMs;
        }
        if (day.dayOrdinal >= start7DayOrdinal && day.dayOrdinal <= referenceDayOrdinal) {
          json.global.recent7ReadingMs += day.readingMs;
        }
        if (day.dayOrdinal >= start30DayOrdinal && day.dayOrdinal <= referenceDayOrdinal) {
          json.global.recent30ReadingMs += day.readingMs;
        }
        if (day.readingMs >= goalReadingMs) {
          eligibleDays.push_back(day.dayOrdinal);
        }
      }

      if (!eligibleDays.empty()) {
        json.global.maxStreakDays = 1;
        uint32_t currentMaxStreak = 1;
        for (size_t index = 1; index < eligibleDays.size(); ++index) {
          if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
            currentMaxStreak++;
          } else {
            currentMaxStreak = 1;
          }
          json.global.maxStreakDays = std::max(json.global.maxStreakDays, currentMaxStreak);
        }

        const uint32_t latestEligibleDay = eligibleDays.back();
        const bool streakIsStillAlive = latestEligibleDay == referenceDayOrdinal ||
                                        (referenceDayOrdinal > 0 && latestEligibleDay + 1 == referenceDayOrdinal);
        if (streakIsStillAlive) {
          json.global.currentStreakDays = 1;
          for (size_t index = eligibleDays.size() - 1; index > 0; --index) {
            if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
              json.global.currentStreakDays++;
              continue;
            }
            break;
          }
        }
      }
    }

    for (const auto& day : merged) {
      json.global.totalReadingMs += day.readingMs;
    }
    json.global.booksFinishedCount = finishedCount;
    json.global.dailyAverageMs = json.global.recent30ReadingMs > 0 ? json.global.recent30ReadingMs / 30 : 0;

    if (!writeSummaryFile(json, summaryJsonPath)) {
      LOG_ERR("RST", "Import summary: write failed");
      return false;
    }
  }

  LOG_DBG("RST", "Streaming import complete: stats + summary regenerated");
  return true;
}

}  // namespace ReadingStatsImportStreaming
