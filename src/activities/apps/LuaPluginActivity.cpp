#include "LuaPluginActivity.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

#include <Arduino.h>
#include <vector>

#include "HalStorage.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "fontIds.h"
#include "LuaPluginVM.h"
#include "LuaPluginAPI.h"

static const char* TAG = "LuaPlugin";

LuaPluginActivity::LuaPluginActivity(const std::string& pluginName,
                                      GfxRenderer& renderer,
                                      MappedInputManager& input,
                                      bool launchFromApps,
                                      bool returnToPluginBrowser)
    : Activity("LuaPlugin", renderer, input),
       pluginName_(pluginName),
       pluginPath_("/custom/" + pluginName + ".lua"),
       launchFromApps_(launchFromApps),
       returnToPluginBrowser_(returnToPluginBrowser),
       exitRequested_(false),
       loadError_(false),
       vmInitialized_(false) {}

void LuaPluginActivity::onEnter() {
  Activity::onEnter();

  // --- Read plugin file ---
  if (!Storage.exists(pluginPath_.c_str())) {
    ESP_LOGE(TAG, "Plugin file not found: %s", pluginPath_.c_str());
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
    ESP_LOGE(TAG, "Cannot open plugin: %s", pluginPath_.c_str());
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
    ESP_LOGE(TAG, "Plugin file is empty: %s", pluginPath_.c_str());
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Plugin file empty", true);
    renderer.displayBuffer();
    file.close();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  if (fileSize > lua_plugin::MAX_LUA_SOURCE_SIZE) {
    ESP_LOGE(TAG, "Plugin too large: %u bytes (max %u)", (unsigned)fileSize,
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
    ESP_LOGE(TAG, "Short read: %u / %u bytes", (unsigned)bytesRead, (unsigned)fileSize);
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_10_FONT_ID, 380, "Read error", true);
    renderer.displayBuffer();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // --- Memory safety check ---
  if (!lua_plugin::checkMemoryAvailable()) {
    ESP_LOGE(TAG, "Insufficient memory for plugin '%s'", pluginName_.c_str());
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
    ESP_LOGE(TAG, "VM init failed");
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
    ESP_LOGE(TAG, "Script load failed: %s", lua_plugin::getLastError());
    renderer.clearScreen(0xFF);
    renderer.drawText(UI_10_FONT_ID, 20, 380, "Plugin error:", true);
    renderer.drawText(UI_10_FONT_ID, 20, 400, lua_plugin::getLastError(), true);
    renderer.displayBuffer();
    lua_plugin::vmShutdown();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  // --- Call init() ---
  if (!lua_plugin::vmRunMain()) {
    ESP_LOGE(TAG, "init() failed: %s", lua_plugin::getLastError());
    renderer.clearScreen(0xFF);
    renderer.drawText(UI_10_FONT_ID, 20, 380, "init() error:", true);
    renderer.drawText(UI_10_FONT_ID, 20, 400, lua_plugin::getLastError(), true);
    renderer.displayBuffer();
    lua_plugin::vmShutdown();
    loadError_ = true;
    activityManager.requestUpdate();
    return;
  }

  ESP_LOGI(TAG, "Plugin '%s' running (allocated=%u bytes)", pluginName_.c_str(),
           lua_plugin::getAllocatedBytes());

  // Initial render
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

  // Process input
  mappedInput.update();

  // Check if plugin self-requested exit (called sys.finish())
  if (lua_plugin_wants_exit()) {
    exitRequested_ = true;
    return;
  }

  // Call onKey() every frame so games can do continuous updates.
  // Plugins that only need event-driven behavior check input.wasPressed()
  // (edge detection) inside onKey() and effectively no-op on idle frames.
  if (lua_plugin::vmHasFunction("onKey")) {
    lua_plugin::vmCallCallback("onKey", 0);
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

  // Shutdown the Lua VM to reclaim all heap memory
  if (vmInitialized_) {
    lua_plugin::vmShutdown();
  }

  ESP_LOGI(TAG, "Plugin exited. Free heap: %u, MaxAlloc: %u",
           lua_plugin::getFreeHeap(), lua_plugin::getMaxAlloc());

   // Silent restart to the caller (preserves clean heap state)
   if (returnToPluginBrowser_) {
     silentRestartToPluginBrowser();
   } else if (launchFromApps_) {
     silentRestartToApps();
   } else {
     silentRestartToHome();
   }
}
