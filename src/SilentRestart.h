#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

// Where a fast silent reboot should land. Each value maps 1:1 to an RTC boot
// target consumed once in setup(): the boot skips the splash and routes
// straight to the destination activity.
enum class RestartLanding : uint8_t {
  Home,           // home screen
  Apps,           // apps menu
  PluginBrowser,  // plugin browser
  Reader,         // currently-open EPUB (APP_STATE.openEpubPath)
  Ota,            // update screen with a fresh heap (CrossInk network boot pattern)
};

// Declarative exit-restart policy for an activity (see
// Activity::exitRestartPlan()). enabled=false keeps the activity's exit
// behavior exactly as before; enabled=true turns the declared exit paths into
// a fast silent reboot that lands on `landing`.
struct ExitRestartPlan {
  bool enabled = false;
  RestartLanding landing = RestartLanding::Home;
  bool seamless = true;  // true: panel keeps its frame until the target's first paint
};

void silentRestart();          // home screen (WiFi teardown exit; draws "Loading..." popup)
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToOta();     // update screen with a fresh heap (CrossInk network boot pattern)

// Seamless variants (no "Loading..." popup; the panel keeps its frame until the
// destination's first paint). Used by the Lua plugin runtime on exit.
void silentRestartToHome();  // home screen
void silentRestartToApps();  // apps menu

// Lua plugin launchers. The plugin browser and a running plugin are restored
// after the reboot so the Lua VM starts from a clean heap.
void silentRestartToPluginBrowser();
void silentRestartToPlugin(const char* pluginName, bool fromApps, bool returnToPluginBrowser = false);

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();

// Generic entry point: encode the landing target in the RTC token and reboot.
// seamless=true skips the popup so the panel holds its pre-reboot frame until
// the destination's first paint; seamless=false draws the "Loading..." popup
// so any input fired during the reboot window is absorbed by the overlay.
void silentRestartTo(RestartLanding landing, bool seamless);
