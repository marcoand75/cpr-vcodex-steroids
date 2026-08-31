#pragma once

// Centralized WiFi helpers for activities that need to cleanly shut down or
// restart the WiFi radio. Keeps activity code free from duplicated
// disconnect/sleep/restart sequences.
//
// Usage:
//   - WiFiUtils::wifiOff()                       // full shutdown, keeps caller alive
//   - WiFiUtils::gracefulDisconnectAndSilentRestart()  // disconnect + silent restart

#include <cstdint>

namespace WiFiUtils {

// Stop NTP and fully power down the WiFi radio. Intended for activities that
// will continue running after WiFi is off and want a clean heap afterwards.
void wifiOff();

// Graceful WiFi teardown followed by a silent restart. Uses the shorter
// settle delay used by the existing network-exit activities.
void gracefulDisconnectAndSilentRestart();

}  // namespace WiFiUtils
