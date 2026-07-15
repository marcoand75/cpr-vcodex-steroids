/**
 * CoverDebugLog.h
 *
 * Simple append-only SD-card logger for diagnosing cover/thumbnail failures.
 * Writes timestamped entries to /cover_debug.log on the SD card.
 *
 * The macro is a no-op in release builds.  Define COVER_DEBUG in
 * platformio.ini build_flags to enable:
 *   -DCOVER_DEBUG
 *
 * Usage:
 *   COVER_LOG("EBP", "load ok: heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
 */

#pragma once

#ifndef COVER_DEBUG
#define COVER_LOG(tag, fmt, ...) ((void)0)
#else

#include <HalStorage.h>

#include <common/FsApiConstants.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kCoverLogPath = "/cover_debug.log";

inline void sdcardAppendLog(const char* tag, const char* fmt, ...) {
  char buf[256];
  const unsigned long ms = millis();
  const int off = std::snprintf(buf, sizeof(buf), "[%8lu] %-6s ", ms, tag);
  if (off < 0 || static_cast<size_t>(off) >= sizeof(buf)) return;

  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
  va_end(args);

  // Append: create if not exists, open for write, seek to EOF before writing.
  HalFile f = Storage.open(kCoverLogPath, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    Storage.mkdir("/.crosspoint");
    f = Storage.open(kCoverLogPath, O_CREAT | O_WRONLY | O_APPEND);
    if (!f) return;
  }
  f.write(reinterpret_cast<const uint8_t*>(buf), std::strlen(buf));
  f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  f.close();
}

}  // namespace

#define COVER_LOG(tag, fmt, ...) sdcardAppendLog(tag, fmt, ##__VA_ARGS__)

#endif  // COVER_DEBUG
