/**
 * CoverDebugLog.h
 *
 * USB-serial logger for diagnosing cover/thumbnail failures.
 * Prints timestamped entries via printf (USB CDC serial on ESP32-C3).
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

#include <cstdarg>
#include <cstdio>

namespace {

inline void coverDebugLog(const char* tag, const char* fmt, ...) {
  const unsigned long ms = millis();
  printf("[%8lu] %-6s ", ms, tag);

  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  printf("\n");
}

}  // namespace

#define COVER_LOG(tag, fmt, ...) coverDebugLog(tag, fmt, ##__VA_ARGS__)

#endif  // COVER_DEBUG
