/**
 * LibraryDebugLog.h
 *
 * USB-serial logger for diagnosing library sync/scan/paging failures.
 * Prints timestamped entries via printf (USB CDC serial on ESP32-C3).
 *
 * The macro is a no-op in release builds.  Define LIB_DEBUG in
 * platformio.ini build_flags to enable:
 *   -DLIB_DEBUG
 *
 * Usage:
 *   LIB_LOG("BSC", "scan: start heap=%u", ESP.getFreeHeap());
 */

#pragma once

#ifndef LIB_DEBUG
#define LIB_LOG(tag, fmt, ...) ((void)0)
#else

#include <cstdarg>
#include <cstdio>

namespace {

inline void libraryDebugLog(const char* tag, const char* fmt, ...) {
  const unsigned long ms = millis();
  printf("[%8lu] %-6s ", ms, tag);

  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  printf("\n");
}

}  // namespace

#define LIB_LOG(tag, fmt, ...) libraryDebugLog(tag, fmt, ##__VA_ARGS__)

#endif  // LIB_DEBUG
