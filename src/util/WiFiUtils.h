#pragma once

// Centralized WiFi helpers for activities that need to cleanly shut down or
// restart the WiFi radio. Keeps activity code free from duplicated
// disconnect/sleep/restart sequences.
//
// Usage:
//   - WiFiUtils::wifiOff()                       // full shutdown, keeps caller alive
//   - WiFiUtils::gracefulDisconnectAndSilentRestart()  // disconnect + silent restart
//   - WiFiUtils::enterStationMode()              // STA mode + disable modem sleep
//   - WiFiUtils::disableNvsAutoPersist()         // suppress SDK NVS credential auto-reconnect
//   - WiFiUtils::abortAutoConnectAndClearNvs()   // disconnect(true,true) for WifiSelectionActivity
//   - WiFiUtils::stopAp()                        // softAPdisconnect(true) for web server teardown
//   - WiFiUtils::disconnect()                    // plain WiFi.disconnect() for scan/connect flows

#include <cstdint>

namespace WiFiUtils {

// Stop NTP and fully power down the WiFi radio. Intended for activities that
// will continue running after WiFi is off and want a clean heap afterwards.
void wifiOff();

// Graceful WiFi teardown followed by a silent restart. Uses the shorter
// settle delay used by the existing network-exit activities.
void gracefulDisconnectAndSilentRestart();

// Prepare WiFi for normal station operation: force STA mode and disable
// modem sleep so the radio stays responsive during network activity.
void enterStationMode();

// Suppress the Arduino core's automatic NVS persistence of WiFi credentials.
// The firmware uses WifiCredentialStore on the SD card as the source of truth,
// so the SDK's hidden nvs.net80211 copy must not auto-reconnect behind the user.
void disableNvsAutoPersist();

// Disable WiFi modem sleep so the radio stays responsive during network
// activity. Used after connection to avoid stalls on weak networks.
void disableModemSleep();

// Abort any in-progress SDK auto-connect and clear the NVS-saved SSID used by
// WifiSelectionActivity before starting a fresh scan/connect cycle.
void abortAutoConnectAndClearNvs();

// Stop the access-point side of WiFi without powering the radio off entirely.
// Used by web-server teardown where the mode may be left for later reuse.
void stopAp();

// Plain disconnect without changing mode or power state. Used by scan and
// connection flows where the caller will immediately re-enter STA mode.
void disconnect();

// Forceful disconnect that also clears the SDK's auto-connect state. Used
// before deep sleep or full power-down where the radio must not reconnect.
void forceDisconnect();

// Switch the WiFi radio fully off. Used by main.cpp boot and deep-sleep paths
// where no further WiFi activity will occur until the next boot.
void powerOff();

// Enable or disable the WiFi stack's automatic reconnect behavior. Used by
// the web server where driver retries are required during transient disconnects.
void setAutoReconnect(bool enabled);

// Start access-point mode. Used by the web server activity when creating a hotspot.
void enterApMode();

}  // namespace WiFiUtils
