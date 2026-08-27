#pragma once

struct lua_State;

/**
 * Registers the plugin API functions (lcd.*, input.*, sys.*, fs.*) into the
 * given Lua state. Called once during vmInit() from LuaPluginVM.cpp.
 *
 * The API bindings assume that the renderer and input manager have been
 * set via setContext() before registration.
 */
void lua_plugin_register_libs(lua_State* L);

/**
 * Called by vmShutdown() to let the API layer clean up any per-plugin state.
 */
void lua_plugin_api_on_shutdown();

// ---------------------------------------------------------------------------
// Context setters — called by LuaPluginActivity before vmInit()
// ---------------------------------------------------------------------------

class GfxRenderer;
class MappedInputManager;
struct CrossPointSettings;

void lua_plugin_set_plugin_name(const char* name);
void lua_plugin_set_renderer(GfxRenderer* r);
void lua_plugin_set_input(MappedInputManager* m);
void lua_plugin_set_settings(const CrossPointSettings* s);

// ---------------------------------------------------------------------------
// Signal handlers — called from the LuaPluginActivity event loop
// ---------------------------------------------------------------------------

/**
 * Returns true if the plugin called finish() during the last callback.
 */
bool lua_plugin_wants_exit();
