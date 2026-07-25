#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "HalGPIO.h"

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable uint16_t _cachedYear = 2000;
  mutable uint8_t _cachedMonth = 1;
  mutable uint8_t _cachedDay = 1;
  mutable bool _hasCachedTime = false;
  mutable bool _hasCachedDate = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;

 public:
  void begin();

  bool isAvailable() const { return _available; }

  bool getTime(uint8_t& hour, uint8_t& minute) const;
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;
  bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const {
    return getDate(year, month, day, hour, minute);
  }
  bool formatDate(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48) const;
  bool syncFromNTP();

 private:
  bool getDate(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const;
  bool writeDateTimeToRTC(uint16_t year, uint8_t month, uint8_t day, uint8_t weekday, uint8_t hour, uint8_t minute,
                          uint8_t second);
};
