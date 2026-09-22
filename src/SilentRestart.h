#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
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
