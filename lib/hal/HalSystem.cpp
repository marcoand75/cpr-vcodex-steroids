#include "HalSystem.h"

#include <HalClock.h>
#include <ctime>
#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "esp_debug_helpers.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"

#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u

extern const char CPR_CROSSPOINT_VERSION[];

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
// RTC_NOINIT is undefined on cold boot. Only this exact value proves that the
// panic wrappers captured diagnostics before a watchdog reset.
RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;

extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context)
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }

  // Copied from components/esp_system/port/arch/riscv/panic_arch.c
  uint32_t sp = (uint32_t)((RvExcFrame*)frame)->sp;
  const int per_line = 8;
  int depth = 0;
  for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
    uint32_t* spp = (uint32_t*)(sp + x);
    // panic_print_hex(sp + x);
    // panic_print_str(": ");
    panicStack[depth].sp = sp + x;
    for (int y = 0; y < per_line; y++) {
      // panic_print_str("0x");
      // panic_print_hex(spp[y]);
      // panic_print_str(y == per_line - 1 ? "\r\n" : " ");
      panicStack[depth].spp[y] = spp[y];
    }

    depth++;
    if (depth >= MAX_PANIC_STACK_DEPTH) {
      break;
    }
  }

  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_print_backtrace(frame, core);
}
}

namespace HalSystem {

void begin() {
  // Preserve captured diagnostics only for an actual panic reboot. Ordinary
  // boots clear stale RTC memory left by a previous session.
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);

    // Build a timestamped filename so crash reports are preserved instead of overwritten.
    char timestampedPath[64] = "/logs/crash_report.txt";
    uint32_t epoch = 0;
    if (halClock.isAvailable() && halClock.readUtcEpoch(epoch) && epoch > 1704067200UL) {
      const time_t rawtime = static_cast<time_t>(epoch);
      struct tm timeinfo{};
      if (gmtime_r(&rawtime, &timeinfo) != nullptr) {
        snprintf(timestampedPath, sizeof(timestampedPath),
                 "/logs/crash_report_%04d%02d%02d_%02d%02d%02d.txt",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      }
    }
    if (strcmp(timestampedPath, "/logs/crash_report.txt") == 0) {
      // Fallback when no valid clock is available yet.
      snprintf(timestampedPath, sizeof(timestampedPath),
               "/logs/crash_report_boot_%lu.txt", (unsigned long)millis());
    }

    Storage.mkdir("/logs");

    // Keep writing the legacy /crash_report.txt for CrashActivity / user-facing messages,
    // and also write the timestamped copy so historical reports are not lost.
    const char* legacyPath = "/crash_report.txt";
    bool legacyOk = false;
    auto legacyFile = Storage.open(legacyPath, O_WRITE | O_CREAT | O_TRUNC);
    if (legacyFile) {
      const size_t written = legacyFile.write(panicInfo.c_str(), panicInfo.size());
      legacyFile.close();
      if (written == panicInfo.size()) {
        legacyOk = true;
        LOG_INF("SYS", "Dumped panic info to %s", legacyPath);
      } else {
        LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes) to %s", written,
                panicInfo.size(), legacyPath);
      }
    } else {
      LOG_ERR("SYS", "Failed to open %s for writing", legacyPath);
    }

    bool timestampedOk = false;
    auto file = Storage.open(timestampedPath, O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      const size_t written = file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      if (written == panicInfo.size()) {
        timestampedOk = true;
        LOG_INF("SYS", "Dumped panic info to %s", timestampedPath);
      } else {
        LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes) to %s", written,
                panicInfo.size(), timestampedPath);
      }
    } else {
      LOG_ERR("SYS", "Failed to open %s for writing", timestampedPath);
    }

    if (legacyOk || timestampedOk) {
      panicCaptureMarker = 0;
    }
  }
}

void clearPanic() {
  panicCaptureMarker = 0;
  panicMessage[0] = '\0';
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += std::string("CrossPoint version: ") + CPR_CROSSPOINT_VERSION;
    info += "\n\nPanic reason: " + std::string(panicMessage);
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nStack memory:\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
      if (panicStack[i].sp == 0) {
        break;
      }
      info += "0x" + toHex(panicStack[i].sp) + ": ";
      for (size_t j = 0; j < 8; j++) {
        info += "0x" + toHex(panicStack[i].spp[j]) + " ";
      }
      info += "\n";
    }

    return info;
  }
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP) {
    return true;
  }

  const bool watchdogReset =
      resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  return watchdogReset && panicCaptureMarker == PANIC_CAPTURE_MAGIC;
}

}  // namespace HalSystem
