#include "LuaPluginVM.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstring>

#include <Arduino.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "LuaPluginAPI.h"

namespace lua_plugin {

static const char* TAG = "LUA_VM";

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------

static lua_State* s_L = nullptr;
static char s_lastError[256] = {0};

// Tracks total bytes allocated by the Lua VM's custom allocator.
static uint32_t s_luaAllocated = 0;

// Instruction counter for the hook (per-callback).
static uint32_t s_instrCount = 0;

// ---------------------------------------------------------------------------
// Custom memory allocator — enforces PLUGIN_MEM_CAP (40 KB hard cap).
// Uses heap_caps_malloc with MALLOC_CAP_8BIT for 8-bit-aligned access
// (e-ink framebuffer and font rendering may read 32-bit words).
// ---------------------------------------------------------------------------

static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  (void)ud;

  if (nsize == 0) {
    // Free
    const uint32_t oldSize = static_cast<uint32_t>(osize);
    if (s_luaAllocated >= oldSize) {
      s_luaAllocated -= oldSize;
    } else {
      s_luaAllocated = 0;  // safety: clamp to zero
    }
    heap_caps_free(ptr);
    return nullptr;
  }

  // Check if the new size would exceed the cap
  if (nsize > osize) {
    const uint32_t growth = static_cast<uint32_t>(nsize - osize);
    if (s_luaAllocated + growth > PLUGIN_MEM_CAP) {
      ESP_LOGW(TAG, "Memory cap reached: allocated=%u cap=%u growth=%u",
               s_luaAllocated, static_cast<uint32_t>(PLUGIN_MEM_CAP), growth);
      return nullptr;  // Signal OOM to Lua — it will error out
    }
  }

  void* newPtr = heap_caps_realloc(ptr, nsize, MALLOC_CAP_8BIT);
  if (newPtr) {
    const int32_t delta = static_cast<int32_t>(nsize) - static_cast<int32_t>(osize);
    s_luaAllocated += static_cast<uint32_t>(delta);
  } else {
    ESP_LOGE(TAG, "heap_caps_realloc failed for %u bytes", static_cast<uint32_t>(nsize));
  }
  return newPtr;
}

// ---------------------------------------------------------------------------
// Instruction hook — aborts after MAX_INSTRUCTIONS_PER_CALLBACK instructions
// ---------------------------------------------------------------------------

static void luaInstructionHook(lua_State* L, lua_Debug* ar) {
  (void)ar;
  s_instrCount++;
  if (s_instrCount > static_cast<uint32_t>(MAX_INSTRUCTIONS_PER_CALLBACK)) {
    s_instrCount = 0;
    luaL_error(L, "instruction limit exceeded (%d)", MAX_INSTRUCTIONS_PER_CALLBACK);
  }
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

bool checkMemoryAvailable() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();

  // Need at least 100 KB free and 75 KB contiguous for Lua state + script
  if (freeHeap < 100000 || maxAlloc < 75000) {
    ESP_LOGE(TAG, "Insufficient memory: free=%u maxAlloc=%u", freeHeap, maxAlloc);
    return false;
  }

  ESP_LOGI(TAG, "Memory check OK: free=%u maxAlloc=%u", freeHeap, maxAlloc);
  return true;
}

uint32_t getFreeHeap() { return ESP.getFreeHeap(); }
uint32_t getMaxAlloc() { return ESP.getMaxAllocHeap(); }
uint32_t getAllocatedBytes() { return s_luaAllocated; }

const char* getLastError() { return s_lastError[0] ? s_lastError : "unknown error"; }

void vmShutdown() {
  if (s_L) {
    lua_plugin_api_on_shutdown();  // notify API bindings
    lua_close(s_L);
    s_L = nullptr;
  }
  s_luaAllocated = 0;
  s_instrCount = 0;
  s_lastError[0] = '\0';
}

bool vmInit() {
  if (s_L != nullptr) {
    ESP_LOGW(TAG, "VM already initialised");
    return false;
  }

  // Reset allocator tracking
  s_luaAllocated = 0;
  s_instrCount = 0;
  s_lastError[0] = '\0';

  // Create Lua state with our custom allocator
  s_L = lua_newstate(luaAlloc, nullptr);
  if (s_L == nullptr) {
    ESP_LOGE(TAG, "Failed to create Lua state");
    return false;
  }

  // Open minimal standard libraries + plugin API
  lua_plugin_register_libs(s_L);

  // Set instruction hook (count hook, every instruction)
  lua_sethook(s_L, luaInstructionHook, LUA_MASKCOUNT, 1);

  ESP_LOGI(TAG, "VM initialised: free=%u maxAlloc=%u", getFreeHeap(), getMaxAlloc());
  return true;
}

bool vmLoad(const char* source, size_t size, const char* pluginName) {
  if (s_L == nullptr) {
    ESP_LOGE(TAG, "VM not initialised");
    return false;
  }

  // Compile the source buffer
  if (luaL_loadbuffer(s_L, source, size, pluginName) != LUA_OK) {
    const char* err = lua_tostring(s_L, -1);
    snprintf(s_lastError, sizeof(s_lastError), "load error: %s", err ? err : "(nil)");
    ESP_LOGE(TAG, "luaL_loadbuffer failed: %s", s_lastError);
    lua_pop(s_L, 1);
    return false;
  }

  // Execute the chunk to define functions (main, onButton, etc.)
  s_instrCount = 0;
  if (lua_pcall(s_L, 0, LUA_MULTRET, 0) != LUA_OK) {
    const char* err = lua_tostring(s_L, -1);
    snprintf(s_lastError, sizeof(s_lastError), "init error: %s", err ? err : "(nil)");
    ESP_LOGE(TAG, "lua_pcall (init) failed: %s", s_lastError);
    lua_pop(s_L, 1);
    return false;
  }

  ESP_LOGI(TAG, "Script loaded: %s (allocated=%u bytes)", pluginName, s_luaAllocated);
  return true;
}

bool vmHasFunction(const char* funcName) {
  if (s_L == nullptr) return false;
  lua_getglobal(s_L, funcName);
  const bool exists = lua_isfunction(s_L, -1);
  lua_pop(s_L, 1);
  return exists;
}

bool vmCallCallback(const char* funcName, int nargs) {
  if (s_L == nullptr) {
    ESP_LOGE(TAG, "VM not initialised");
    return false;
  }

  // Look up the function
  lua_getglobal(s_L, funcName);
  if (!lua_isfunction(s_L, -(nargs + 1))) {
    lua_pop(s_L, 1 + nargs);
    // Not an error if the function doesn't exist — plugin simply doesn't handle it
    return true;
  }

  // Reset instruction counter for this callback
  s_instrCount = 0;

  // Push any arguments already on the stack... actually, the caller pushes them
  // directly before calling this. But our current signature doesn't support
  // pushing args from C++. For simplicity, nargs is always 0 for our use case.

  // Call the function with pcall
  const int errfunc = 0;  // no error handler for now
  if (lua_pcall(s_L, nargs, 0, errfunc) != LUA_OK) {
    const char* err = lua_tostring(s_L, -1);
    snprintf(s_lastError, sizeof(s_lastError), "callback %s: %s",
             funcName, err ? err : "(nil)");
    ESP_LOGE(TAG, "lua_pcall failed for '%s': %s", funcName, s_lastError);
    lua_pop(s_L, 1);
    return false;
  }

  return true;
}

bool vmRunMain() {
  if (s_L == nullptr) {
    ESP_LOGE(TAG, "VM not initialised");
    return false;
  }

  if (!vmHasFunction("init")) {
    ESP_LOGW(TAG, "No init() function in plugin — skipping");
    return true;
  }

  return vmCallCallback("init", 0);
}

}  // namespace lua_plugin
