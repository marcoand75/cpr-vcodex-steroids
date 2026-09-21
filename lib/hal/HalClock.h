#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ctime>

#include "HalGPIO.h"

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable tm _cachedUtcTm{};
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

  bool readUtcTmFromChip(struct tm& outUtc, bool forceRefresh) const;
  bool writeUtcTmToChip(const struct tm& utc) const;

 public:
  // Call after gpio.begin() and powerManager.begin() (I2C already initialised for X3)
  void begin();

  // True if the DS3231 RTC is present on this device
  bool isAvailable() const { return _available; }

  // Read the DS3231 as UTC calendar time. Uses a short poll cache unless forceRefresh is set.
  bool readUtcTm(struct tm& outUtc, bool forceRefresh = false) const;

  // Read UTC epoch seconds from the DS3231.
  bool readUtcEpoch(uint32_t& epochSeconds, bool forceRefresh = false) const;

  // Write UTC calendar time to the DS3231 (24-hour mode).
  bool writeUtcTm(const struct tm& utc);

  // Get current UTC hour/minute, using the poll cache when fresh.
  bool getUtcTime(uint8_t& hour, uint8_t& minute, bool forceRefresh = false) const;

  // Sync the DS3231 RTC from an NTP server. Requires WiFi to be connected.
  // Writes full UTC date/time and blocks for up to ~5s while waiting for SNTP.
  // Returns true if the RTC was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
