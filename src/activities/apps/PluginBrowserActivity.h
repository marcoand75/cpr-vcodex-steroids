#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "activities/util/ConfirmationActivity.h"

/**
 * PluginBrowserActivity
 *
 * Scans /custom/ for *.lua files, parses -- NAME:, -- DESC:, -- ICON: headers,
 * and displays a list. When the user confirms a plugin, triggers a silent
 * restart to launch it via LuaPluginActivity.
 */
class PluginBrowserActivity final : public Activity {
  struct PluginEntry {
    std::string filename;   // e.g. "hello_world.lua"
    std::string path;       // e.g. "/custom/hello_world.lua"
    std::string name;       // e.g. "Hello World"
    std::string description;
    std::string icon;       // icon name from header (for future use)
    bool fastReboot = true; // "-- RESTART: no" → launch in-process, no silent reboot
  };

  std::vector<PluginEntry> pluginList_;
  int selectedIndex_ = 0;

  void scanPlugins();
  void parsePluginHeaders(PluginEntry& entry);
  bool togglePluginReboot(PluginEntry& plugin);
  bool deletePlugin(const PluginEntry& plugin);

  // Long-press / context menu state
  bool showingMenu_ = false;
  int menuIndex_ = 0;
  static constexpr unsigned long kPluginLongPressMs = 800;

  // Long-press tracking for Confirm
  bool confirmHeld_ = false;
  bool longPressFired_ = false;
  unsigned long confirmPressStartMs_ = 0;

  // Cached layout data to avoid per-frame allocations.
  std::vector<int> cachedHeights_;
  std::vector<int> cachedTops_;
  std::vector<std::vector<std::string>> cachedDescLines_;
  int cachedTotal_ = 0;
  int cachedContentH_ = 0;
  int cachedFirst_ = 0;
  int cachedLast_ = 0;

  void rebuildLayoutCache(int pw, int ph);

 public:
  explicit PluginBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PluginBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
