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
