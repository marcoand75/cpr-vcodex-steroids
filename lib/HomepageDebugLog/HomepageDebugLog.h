/**
 * HomepageDebugLog.h
 *
 * USB-serial logger for diagnosing homepage heap fragmentation and rendering
 * issues. Prints timestamped entries via printf (USB CDC serial on ESP32-C3).
 *
 * The macro is a no-op in release builds. Define HOMEPAGE_DEBUG in
 * platformio.ini build_flags to enable:
 *   -DHOMEPAGE_DEBUG
 *
 * Usage:
 *   HOMEPAGE_LOG("HOME", "onEnter: heap=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
 */

#pragma once

#ifndef HOMEPAGE_DEBUG
#define HOMEPAGE_LOG(tag, fmt, ...) ((void)0)
#else

#include <cstdarg>
#include <cstdio>

namespace {

inline void homepageDebugLog(const char* tag, const char* fmt, ...) {
  const unsigned long ms = millis();
  printf("[%8lu] %-6s ", ms, tag);

  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  printf("\n");
}

}  // namespace

#define HOMEPAGE_LOG(tag, fmt, ...) homepageDebugLog(tag, fmt, ##__VA_ARGS__)

#endif  // HOMEPAGE_DEBUG
