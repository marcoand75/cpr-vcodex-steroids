#include "LuaPluginActivity.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Arduino.h>
#include <cstring>
#include <vector>

#include "HalStorage.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "fontIds.h"
#include "LuaPluginVM.h"
#include "LuaPluginAPI.h"
#include "Logging.h"

static const char* TAG = "LuaPlugin";

LuaPluginActivity::LuaPluginActivity(const std::string& pluginName,
                                      GfxRenderer& renderer,
                                      MappedInputManager& input,
                                      bool launchFromApps,
                                      bool returnToPluginBrowser,
                                      bool launchInProcess)
    : Activity("LuaPlugin", renderer, input),
       pluginName_(pluginName),
       pluginPath_("/custom/" + pluginName + ".lua"),
       launchFromApps_(launchFromApps),
       returnToPluginBrowser_(returnToPluginBrowser),
       launchInProcess_(launchInProcess),
       exitRequested_(false),
       loadError_(false),
       vmInitialized_(false) {}

void LuaPluginActivity::onEnter() {
  Activity::onEnter();

  // --- Read plugin file ---
  if (!Storage.exists(pluginPath_.c_str())) {
    LOG_ERR(TAG, "Plugin file not found: %s", pluginPath_.c_str());
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Plugin not found", true);
    renderer.drawCenteredText(UI_10_FONT_ID, 400, pluginName_.c_str(), true);
    renderer.displayBuffer();
    loadError_ = true;
    // Request immediate exit — loop() will call finish()
    activityManager.requestUpdate();
    return;
  }

  HalFile file = Storage.open(pluginPath_.c_str(), O_RDONLY);
  if (!file) {
    LOG_ERR(TAG, "Cannot open plugin: %s", pluginPath_.c_str());
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Plugin not found", true);
    renderer.drawCenteredText(UI_10_FONT_ID, 400, pluginName_.c_str(), true);
    renderer.displayBuffer();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  const size_t fileSize = file.fileSize();
  if (fileSize == 0) {
    LOG_ERR(TAG, "Plugin file is empty: %s", pluginPath_.c_str());
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Plugin file empty", true);
    renderer.displayBuffer();
    file.close();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  if (fileSize > lua_plugin::MAX_LUA_SOURCE_SIZE) {
    LOG_ERR(TAG, "Plugin too large: %u bytes (max %u)", (unsigned)fileSize,
             (unsigned)lua_plugin::MAX_LUA_SOURCE_SIZE);
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Plugin too large", true);
    renderer.displayBuffer();
    file.close();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // Read file into buffer
  std::vector<uint8_t> buffer(fileSize);
  const size_t bytesRead = file.read(buffer.data(), fileSize);
  file.close();

  if (bytesRead != fileSize) {
    LOG_ERR(TAG, "Short read: %u / %u bytes", (unsigned)bytesRead, (unsigned)fileSize);
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Read error", true);
    renderer.displayBuffer();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // --- Memory safety check ---
  if (!lua_plugin::checkMemoryAvailable()) {
    LOG_ERR(TAG, "Insufficient memory for plugin '%s'", pluginName_.c_str());
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Not enough memory", true);
    renderer.drawCenteredText(UI_10_FONT_ID, 400, "for plugin", true);
    renderer.displayBuffer();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // --- Set context for API bindings before VM init ---
  lua_plugin_set_plugin_name(pluginName_.c_str());
  lua_plugin_set_renderer(&renderer);
  lua_plugin_set_input(&mappedInput);
  lua_plugin_set_settings(&SETTINGS);

  if (!lua_plugin::vmInit()) {
    LOG_ERR(TAG, "VM init failed");
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "VM init failed", true);
    renderer.displayBuffer();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }
  vmInitialized_ = true;

  // --- Load and execute the plugin script ---
  if (!lua_plugin::vmLoad(reinterpret_cast<const char*>(buffer.data()), bytesRead, pluginName_.c_str())) {
    LOG_ERR(TAG, "Script load failed: %s", lua_plugin::getLastError());
    lua_plugin::vmShutdown();
    showErrorScreen("Plugin error:", lua_plugin::getLastError());
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // --- Call init() ---
  if (!lua_plugin::vmRunMain()) {
    LOG_ERR(TAG, "init() failed: %s", lua_plugin::getLastError());
    lua_plugin::vmShutdown();
    showErrorScreen("init() error:", lua_plugin::getLastError());
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  LOG_INF(TAG, "Plugin '%s' running (allocated=%u bytes)", pluginName_.c_str(),
           lua_plugin::getAllocatedBytes());

  // Initial render
  renderer.displayBuffer();
}

void LuaPluginActivity::showErrorScreen(const char* title, const char* msg) {
  renderer.clearScreen(0xFF);
  renderer.drawText(UI_10_FONT_ID, 20, 380, title, true);
  int y = 400;
  if (msg) {
    const char* start = msg;
    for (int line = 0; line < 6 && *start; line++) {
      const char* nl = strchr(start, '\n');
      const size_t len = nl ? static_cast<size_t>(nl - start) : strlen(start);
      char buf[48];
      const size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
      memcpy(buf, start, n);
      buf[n] = '\0';
      renderer.drawText(UI_10_FONT_ID, 20, y, buf, true);
      y += 18;
      if (!nl) break;
      start = nl + 1;
    }
  }
  renderer.displayBuffer();
}

void LuaPluginActivity::loop() {
  if (loadError_) {
    // Clean up VM if it was partially initialised, then exit
    lua_plugin::vmShutdown();
    finish();
    return;
  }

  if (exitRequested_) {
    finish();
    return;
  }

  // Process input — NO mappedInput.update() here: the main loop already calls
  // gpio.update() every frame before activityManager.loop(), and InputManager
  // resets pressedEvents at the start of each update(). A second update() here
  // would wipe the press edges just computed by the main loop, so wasPressed()
  // would always return false. This matches every other activity (Home, Apps,
  // PluginBrowser) which read input state without updating it.
  // (input.waitButton() is the exception — it blocks and polls update() itself.)

  // -------------------------------------------------------------------------
  // Back button — two roles (the device has very few buttons, so a plugin can
  // reuse Back for its own flow):
  //   • SHORT press: delivered to the plugin's onKey() first. The plugin
  //     consumes it (e.g. cancel a sub-screen, back one level) or may call
  //     sys.finish() to exit itself. If the plugin has no onKey, a short
  //     press exits as a safety net.
  //   • LONG press (held >= 1.5 s): always exits the plugin, regardless of
  //     what the plugin does. While Back is held we stop dispatching onKey
  //     so the hold cannot trigger plugin actions.
  // -------------------------------------------------------------------------
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    backPressStartMs_ = millis();
    LOG_DBG(TAG, "Back short press — delivering to plugin '%s'", pluginName_.c_str());
    bool wantsExit = true;
    if (lua_plugin::vmHasFunction("onKey")) {
      if (!lua_plugin::vmCallCallback("onKey", 0)) {
        LOG_ERR(TAG, "onKey() crashed on Back — exiting plugin '%s': %s", pluginName_.c_str(),
                lua_plugin::getLastError());
        showErrorScreen("onKey() error:", lua_plugin::getLastError());
        finish();
        return;
      }
      wantsExit = lua_plugin_wants_exit();
    }
    if (wantsExit) {
      exitRequested_ = true;
    }
    return;
  }

  if (backPressStartMs_ != 0) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
      if (millis() - backPressStartMs_ >= BACK_LONG_PRESS_MS) {
        backPressStartMs_ = 0;
        LOG_INF(TAG, "Back long-press — exiting plugin '%s'", pluginName_.c_str());
        exitRequested_ = true;
      }
      // Still held: do not dispatch onKey while Back is down.
      return;
    }
    // Released before the long-press threshold: the short press was already
    // delivered to the plugin, nothing else to do.
    backPressStartMs_ = 0;
  }

  // Debug trace: any other button press that reaches the plugin this frame.
  if (mappedInput.wasAnyPressed()) {
    LOG_DBG(TAG, "Plugin '%s': button press detected", pluginName_.c_str());
  }

  // Check if plugin self-requested exit (called sys.finish())
  if (lua_plugin_wants_exit()) {
    exitRequested_ = true;
    return;
  }

  // Call onKey() every frame so games can do continuous updates.
  // Plugins that only need event-driven behavior check input.wasPressed()
  // (edge detection) inside onKey() and effectively no-op on idle frames.
  if (lua_plugin::vmHasFunction("onKey")) {
    if (!lua_plugin::vmCallCallback("onKey", 0)) {
      // Lua runtime error inside onKey(). The VM already logged the full
      // traceback to the serial console (LUA_VM tag); show it on screen too,
      // then exit gracefully instead of spamming the log every 10 ms.
      LOG_ERR(TAG, "onKey() crashed — exiting plugin '%s': %s", pluginName_.c_str(),
               lua_plugin::getLastError());
      showErrorScreen("onKey() error:", lua_plugin::getLastError());
      finish();
      return;
    }
    // Check again after the callback
    if (lua_plugin_wants_exit()) {
      exitRequested_ = true;
      return;
    }
  }

  delay(10);
}

void LuaPluginActivity::render(RenderLock&&) {
  // The plugin draws directly to the framebuffer via the Lua API,
  // so we don't need to render anything here for normal operation.
  // Error states are handled in onEnter() with direct renderer calls.
}

void LuaPluginActivity::onExit() {
  Activity::onExit();

  // Give the plugin a chance to run its optional finish() hook (e.g. persist
  // state) on ANY exit path — sys.finish(), long-press Back, load error.
  if (vmInitialized_) {
    if (lua_plugin::vmHasFunction("finish")) {
      lua_plugin::vmCallCallback("finish", 0);
    }
    // Shutdown the Lua VM to reclaim all heap memory
    lua_plugin::vmShutdown();
  }

  LOG_INF(TAG, "Plugin exited. Free heap: %u, MaxAlloc: %u",
           lua_plugin::getFreeHeap(), lua_plugin::getMaxAlloc());

  if (launchInProcess_) {
    // "-- RESTART: no" plugin: no silent reboot. The ActivityManager pop
    // machinery returns to the activity we were pushed on top of (Plugin
    // Browser) and re-renders it.
    LOG_INF(TAG, "In-process exit — returning to previous activity (no reboot)");
    return;
  }

   // Silent restart to the caller (preserves clean heap state)
   if (returnToPluginBrowser_) {
     silentRestartToPluginBrowser();
   } else if (launchFromApps_) {
     silentRestartToApps();
   } else {
     silentRestartToHome();
   }
}
