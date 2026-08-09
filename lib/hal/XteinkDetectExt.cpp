#include "XteinkDetectExt.h"

#include <Arduino.h>
#include <Logging.h>

// ============================================================================
// UC81xx half‑duplex bit‑bang probe (EPD pins, BEFORE SPI.begin claims them)
//
// IMPORTANT: Do NOT use the global SS/DC/MOSI/SCK macros here — they conflict
// with ESP32‑Arduino defines on some toolchains.  Always read the pin numbers
// from BoardConfig::ACTIVE.display (which matches the EPD pin #defines).
// ============================================================================

namespace {

inline int8_t pin(int8_t id) { return id; }

void epdWriteByte(int8_t mosiPin, int8_t sclkPin, uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(mosiPin, (b & 0x80) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(sclkPin, HIGH);
    delayMicroseconds(1);
    digitalWrite(sclkPin, LOW);
    b <<= 1;
  }
}

uint8_t epdReadByte(int8_t mosiPin, int8_t sclkPin) {
  uint8_t b = 0;
  for (uint8_t i = 0; i < 8; i++) {
    delayMicroseconds(1);
    b = static_cast<uint8_t>((b << 1) | (digitalRead(mosiPin) == HIGH ? 1 : 0));
    digitalWrite(sclkPin, HIGH);
    delayMicroseconds(1);
    digitalWrite(sclkPin, LOW);
  }
  return b;
}

void epdCmdRead(int8_t csPin, int8_t dcPin, int8_t mosiPin, int8_t sclkPin,
                uint8_t cmd, uint8_t* out, uint8_t len) {
  pinMode(mosiPin, OUTPUT);
  digitalWrite(csPin, LOW);
  digitalWrite(dcPin, LOW);
  delayMicroseconds(1);
  epdWriteByte(mosiPin, sclkPin, cmd);
  digitalWrite(dcPin, HIGH);
  pinMode(mosiPin, INPUT_PULLUP);
  delayMicroseconds(1);
  for (uint8_t i = 0; i < len; i++) out[i] = epdReadByte(mosiPin, sclkPin);
  digitalWrite(csPin, HIGH);
  pinMode(mosiPin, OUTPUT);
}

bool runDisplayProbePass(uint8_t ver[5], uint8_t* flg, uint8_t rstLowMs) {
  const auto& dp = BoardConfig::ACTIVE.display;

  int8_t csPin  = dp.cs;
  int8_t dcPin  = dp.dc;
  int8_t rstPin = dp.rst;
  int8_t busyPin = dp.busy;
  int8_t mosiPin = dp.mosi;  // bidirectional SDA in half-duplex mode
  int8_t sclkPin = dp.sclk;

  pinMode(csPin, OUTPUT);  digitalWrite(csPin, HIGH);
  pinMode(sclkPin, OUTPUT); digitalWrite(sclkPin, LOW);
  pinMode(dcPin, OUTPUT);   digitalWrite(dcPin, LOW);
  pinMode(mosiPin, OUTPUT);
  if (busyPin >= 0) pinMode(busyPin, INPUT);

  if (rstPin >= 0) {
    pinMode(rstPin, OUTPUT);
    digitalWrite(rstPin, HIGH); delay(2);
    digitalWrite(rstPin, LOW);  delay(rstLowMs);
    digitalWrite(rstPin, HIGH);
  }
  delay(30);

  uint8_t flgByte = 0;
  epdCmdRead(csPin, dcPin, mosiPin, sclkPin, 0x71, &flgByte, 1);
  epdCmdRead(csPin, dcPin, mosiPin, sclkPin, 0x70, ver, 5);

  if (flg) *flg = flgByte;

  if (flgByte == 0x00 || flgByte == 0xFF) return false;
  if ((flgByte & 0x01) != 0x01) return false;

  for (int i = 1; i < 5; i++)
    if (ver[i] != ver[0]) return true;
  return false;
}

void releaseDisplayPins() {
  const auto& dp = BoardConfig::ACTIVE.display;
  pinMode(dp.sclk, INPUT); pinMode(dp.mosi, INPUT);
  pinMode(dp.cs, INPUT_PULLUP); pinMode(dp.dc, INPUT);
  if (dp.rst >= 0) pinMode(dp.rst, INPUT);
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

namespace freeink {

X3DisplayVerdict detectX3DisplayController(uint8_t verBytes[5], uint8_t* flg) {
  uint8_t ver1[5] = {0}, ver2[5] = {0};
  uint8_t flg1 = 0;

  // Two-pass probe with escalating reset: 1 ms then 50 ms
  bool pass1 = runDisplayProbePass(ver1, &flg1, 1);
  if (!pass1) { delay(2); pass1 = runDisplayProbePass(ver1, &flg1, 50); }
  delay(2);
  bool pass2 = runDisplayProbePass(ver2, nullptr, pass1 ? 50 : 1);

  releaseDisplayPins();

  bool verAgree = (memcmp(ver1, ver2, 5) == 0);
  bool confirmed = pass1 && pass2 && verAgree;

  if (verBytes) memcpy(verBytes, confirmed ? ver2 : ver1, 5);
  if (flg) *flg = flg1;

  if (confirmed) return X3DisplayVerdict::Uc8279Confirmed;
  if (!pass1 && !pass2) return X3DisplayVerdict::Uc8253Assumed;
  return X3DisplayVerdict::Inconclusive;
}

bool applyXteinkDisplayController() {
  // Only relevant on X4 (SSD1677 factory default). No-op on other boards.
  if (BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::SSD1677) {
    return false;
  }

  uint8_t ver[5] = {0};
  uint8_t flg = 0;
  const X3DisplayVerdict verdict = detectX3DisplayController(ver, &flg);

  if (verdict != X3DisplayVerdict::Uc8279Confirmed) return false;

  // Promote SSD1677 to UC8279 (this SDK version doesn't have UC8179/UC8279
  // in the DisplayController enum, so we keep SSD1677 and let the driver's
  // setDisplayX3() / internal heuristics handle the difference).
  LOG_INF("HW", "UltraChip UC81xx panel detected (VER=%02X%02X%02X%02X%02X FLG=%02X)",
          ver[0], ver[1], ver[2], ver[3], ver[4], flg);
  return true;
}

}  // namespace freeink
