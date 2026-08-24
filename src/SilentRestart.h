#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a WiFi session.

void silentRestart();          // home screen (shows "Loading..." popup)
void silentRestartToReader();  // currently-open EPUB (shows "Loading..." popup)
void silentRestartToHome();    // home screen, seamless — NO popup, no screen flash
void silentRestartToApps();    // apps menu, seamless — NO popup, no screen flash
void silentRestartToPluginBrowser();  // plugin browser, seamless — NO popup, no screen flash
void silentRestartToPlugin(const char* pluginName, bool fromApps, bool returnToPluginBrowser = false);  // Lua plugin activity
