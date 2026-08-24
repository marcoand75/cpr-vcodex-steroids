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
 * Only ONE plugin runs at a time (per the silent-restart design). The
 * plugin's entire state is destroyed by the VM shutdown + ESP.restart().
 */
class LuaPluginActivity final : public Activity {
   std::string pluginName_;
  std::string pluginPath_;
   bool launchFromApps_;   // true if launched from Apps hub, false from Home
   bool returnToPluginBrowser_;  // true if should return to PluginBrowser after exit
   bool exitRequested_;
  bool loadError_;
  bool vmInitialized_;

  public:
   LuaPluginActivity(const std::string& pluginName,
                     GfxRenderer& renderer,
                     MappedInputManager& input,
                     bool launchFromApps,
                     bool returnToPluginBrowser = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
