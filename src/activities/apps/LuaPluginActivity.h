#pragma once

#include <string>

#include "../Activity.h"

/**
 * LuaPluginActivity
 *
 * Loads and executes a Lua plugin (.lua script from /custom/) inside the
 * LuaPluginVM (40 KB sandboxed VM). Uses the silent-restart lifecycle:
 *
 *   Launch from PluginBrowser → silentRestartToPlugin(name, fromApps)
 *   → ESP.restart() → setup() routes to goToPlugin() → this activity
 *   → onEnter(): init VM, load script, call Lua init()
 *   → loop():     poll buttons, dispatch to Lua onKey() callbacks
 *   → onExit():   vmShutdown(), then silentRestartToApps/Home()
 *
 * When a plugin declares `-- RESTART: no` it is launched in-process instead
 * (ActivityManager::goToPluginInProcess → pushActivity). In that case the
 * Plugin Browser stays on the activity stack, launchInProcess_ is true, and
 * onExit() simply returns so popActivity() lands back on the browser —
 * no ESP.restart() at all.
 *
 * Only ONE plugin runs at a time (per the silent-restart design). The
 * plugin's entire state is destroyed by the VM shutdown + ESP.restart().
 */
class LuaPluginActivity final : public Activity {
   std::string pluginName_;
  std::string pluginPath_;
   bool launchFromApps_;   // true if launched from Apps hub, false from Home
   bool returnToPluginBrowser_;  // true if should return to PluginBrowser after exit
   bool launchInProcess_;  // true if launched via pushActivity (no reboot)
   bool exitRequested_;
  bool loadError_;
  bool vmInitialized_;

  // Long-press Back detection (short Back press is delivered to the plugin).
  unsigned long backPressStartMs_ = 0;
  static constexpr unsigned long BACK_LONG_PRESS_MS = 1500;

  void showErrorScreen(const char* title, const char* msg);

  public:
   LuaPluginActivity(const std::string& pluginName,
                     GfxRenderer& renderer,
                     MappedInputManager& input,
                     bool launchFromApps,
                     bool returnToPluginBrowser = false,
                     bool launchInProcess = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
