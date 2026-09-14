#pragma once

#include <Arduino.h>
#include <Logging.h>

namespace LibraryPerf {

#if LOG_LEVEL >= 2
inline unsigned long nowMs() { return millis(); }

inline void logElapsed(const char* label, unsigned long startMs) {
  LOG_DBG("LIB-PERF", "%s: %lu ms", label, millis() - startMs);
}

struct ScopedTimer {
  const char* label;
  unsigned long start;
  ScopedTimer(const char* lbl) : label(lbl), start(nowMs()) {}
  ~ScopedTimer() { logElapsed(label, start); }
};
#else
inline unsigned long nowMs() { return 0; }
inline void logElapsed(const char* /*label*/, unsigned long /*startMs*/) {}

struct ScopedTimer {
  ScopedTimer(const char*) {}
};
#endif

} // namespace LibraryPerf
