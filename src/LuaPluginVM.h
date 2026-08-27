#pragma once

#include <string>

// Forward declarations to avoid including lua.h in headers
struct lua_State;

namespace lua_plugin {

// Hard memory cap for a single plugin VM. This includes the Lua state,
// string table, table slots, and any buffers the plugin allocates via the
// API. The VM allocator enforces this. 80 KB keeps the small example plugins
// comfortably small while allowing richer ones (e.g. the Doom-like raycaster,
// a 24 KB source that needs ~60 KB of VM heap just to compile/load) — the
// device has ~90-114 KB of contiguous heap available on the silent-reboot
// launch path, and the largest single VM allocation is well under that.
static constexpr size_t PLUGIN_MEM_CAP = 80 * 1024;

// Maximum Lua instructions per callback before the instruction hook
// aborts execution (matches SUMI's 100,000 limit). Prevents infinite loops.
static constexpr int MAX_INSTRUCTIONS_PER_CALLBACK = 100000;

// Maximum size of a single .lua source file loaded from SD card.
static constexpr size_t MAX_LUA_SOURCE_SIZE = 40 * 1024;

/**
 * Initialise the Lua plugin VM. Creates a new lua_State with a custom
 * allocator that enforces PLUGIN_MEM_CAP. Opens the base/string/table/
 * utf8/debug standard libraries plus the plugin API.
 *
 * Must be called once per plugin launch. Call vmInit() before vmLoad()
 * and vmRunMain().
 *
 * @return true on success, false if allocation failed.
 */
bool vmInit();

/**
 * Load a Lua source buffer into the VM and compile it with luaL_loadbuffer.
 * The buffer must remain valid for the duration of the call.
 */
bool vmLoad(const char* source, size_t size, const char* pluginName);

/**
 * Run the Lua 'main()' function (if present). Sets up the instruction
 * hook with MAX_INSTRUCTIONS_PER_CALLBACK. Called once during onEnter()
 * after vmLoad().
 */
bool vmRunMain();

/**
 * Call a Lua callback function by name with no arguments.
 * Used by the activity loop to dispatch button/input events.
 */
bool vmCallCallback(const char* funcName, int nargs = 0);

/**
 * Check whether a Lua function exists in the global table.
 */
bool vmHasFunction(const char* funcName);

/**
 * Shutdown the VM. Frees all Lua state memory.
 * Called during onExit() to reclaim heap.
 */
void vmShutdown();

/**
 * Check whether there is enough free heap to safely initialise the VM.
 * Returns true if ESP.getFreeHeap() > 100 KB and ESP.getMaxAllocHeap() > 75 KB.
 */
bool checkMemoryAvailable();

/**
 * Get the last error message from the VM (set by lua_pcall failures).
 */
const char* getLastError();

/**
 * Get current free heap (ESP.getFreeHeap) for status display.
 */
uint32_t getFreeHeap();

/**
 * Get current max allocatable block (ESP.getMaxAllocHeap) for status display.
 */
uint32_t getMaxAlloc();

/**
 * Get the number of bytes allocated by the Lua VM's custom allocator.
 */
uint32_t getAllocatedBytes();

}  // namespace lua_plugin
