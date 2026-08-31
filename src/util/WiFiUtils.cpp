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
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void gracefulDisconnectAndSilentRestart() {
  TimeUtils::stopNtp();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

}  // namespace WiFiUtils
