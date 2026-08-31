#include "WiFiUtils.h"

#include <WiFi.h>

#include "CrossPointState.h"
#include "SilentRestart.h"
#include "util/TimeUtils.h"

#include <Arduino.h>

namespace WiFiUtils {

void wifiOff() {
  TimeUtils::stopNtp();
  WiFi.disconnect(false);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

void gracefulDisconnectAndSilentRestart() {
  TimeUtils::stopNtp();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void enterStationMode() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
}

void disableNvsAutoPersist() {
  WiFi.persistent(false);
}

void disableModemSleep() {
  WiFi.setSleep(false);
}

void abortAutoConnectAndClearNvs() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
}

void stopAp() {
  WiFi.softAPdisconnect(true);
}

void disconnect() {
  WiFi.disconnect();
}

void forceDisconnect() {
  WiFi.disconnect(true);
}

void powerOff() {
  WiFi.mode(WIFI_OFF);
}

void setAutoReconnect(bool enabled) {
  WiFi.setAutoReconnect(enabled);
}

void enterApMode() {
  WiFi.mode(WIFI_AP);
}

}  // namespace WiFiUtils
