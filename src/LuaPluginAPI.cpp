/**
 * LuaPluginAPI.cpp
 *
 * Lua C bindings for the CPR-vCodex Steroids plugin API.
 * Mirrors SUMI's API surface: lcd.* (drawing), fs.* (file I/O),
 * input.* (input), sys.* (system), plugin_str.* (string utilities).
 *
 * Memory safety:
 * - The Lua VM's custom allocator enforces a 40 KB hard cap (see LuaPluginVM.cpp).
 * - File I/O is sandboxed to /custom/<plugin_name>_data/.
 * - No access to WiFi, HTTP, or native C function calls.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <time.h>
#include <cmath>

#include <Arduino.h>

#include "GfxRenderer.h"
#include "HalStorage.h"
#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "fontIds.h"

#include "HalGPIO.h"
#include "HalPowerManager.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "LuaPluginAPI.h"
#include "LuaPluginVM.h"
#include "Logging.h"

static const char* TAG = "LUA_API";

// ---------------------------------------------------------------------------
// Context (set by LuaPluginActivity before vmInit)
// ---------------------------------------------------------------------------

static GfxRenderer* g_renderer = nullptr;
static MappedInputManager* g_input = nullptr;
static const CrossPointSettings* g_settings = nullptr;
static std::string g_pluginName;
static bool g_wantsExit = false;

void lua_plugin_set_renderer(GfxRenderer* r) { g_renderer = r; }
void lua_plugin_set_input(MappedInputManager* m) { g_input = m; }
void lua_plugin_set_settings(const CrossPointSettings* s) { g_settings = s; }
void lua_plugin_api_on_shutdown() {
  g_renderer = nullptr;
  g_input = nullptr;
  g_settings = nullptr;
  g_wantsExit = false;
  g_pluginName.clear();
}

void lua_plugin_set_plugin_name(const char* name) {
  if (name) {
    g_pluginName = name;
    // Strip .lua suffix if present
    if (g_pluginName.size() > 4 && g_pluginName.size() >= 4 &&
        g_pluginName.compare(g_pluginName.size() - 4, 4, ".lua") == 0) {
      g_pluginName = g_pluginName.substr(0, g_pluginName.size() - 4);
    }
  }
}

const char* lua_plugin_get_plugin_name() { return g_pluginName.c_str(); }
bool lua_plugin_wants_exit() { return g_wantsExit; }

// ---------------------------------------------------------------------------
// Sandbox helpers — all file I/O is restricted to /custom/<plugin_name>_data/
// ---------------------------------------------------------------------------

// Tolerant + SAFE integer argument readers: accept any number (Lua's "/"
// operator produces floats like 4.5) and round to the nearest int. NaN and
// ±Inf are clamped to 0 so a plugin computation gone wrong can never produce
// garbage pixel coordinates (which would flood the serial with GFX
// "Outside range" errors).
static int lua_check_int(lua_State* L, int index) {
  double n = luaL_checknumber(L, index);
  if (!std::isfinite(n)) return 0;  // NaN / ±Inf → 0
  if (n >= 2147483000.0) return 2147483000;
  if (n <= -2147483000.0) return -2147483000;
  return static_cast<int>(std::lround(n));
}
static int lua_opt_int(lua_State* L, int index, int def) {
  double n = luaL_optnumber(L, index, static_cast<lua_Number>(def));
  if (!std::isfinite(n)) return def;
  if (n >= 2147483000.0) return 2147483000;
  if (n <= -2147483000.0) return -2147483000;
  return static_cast<int>(std::lround(n));
}

static std::string getSandboxDir() {
  return "/custom/" + g_pluginName + "_data/";
}

static std::string resolveSandboxPath(const char* filename) {
  std::string path = getSandboxDir() + filename;
  // Basic path traversal protection
  if (path.find("..") != std::string::npos) {
    return "";  // reject
  }
  return path;
}

static bool ensureSandboxDir() {
  const std::string dir = getSandboxDir();
  if (!Storage.exists(dir.c_str())) {
    return Storage.mkdir(dir.c_str());
  }
  return true;
}

// ---------------------------------------------------------------------------
// Drawing API (lcd.*) — 16 functions
// ---------------------------------------------------------------------------

// Shape color convention (matches lcd.setTextColor): 0 = black, 1 = white.
// Returns true when the optional arg at `index` is white. Default: black.
static bool lua_color_is_white(lua_State* L, int index) {
  if (lua_gettop(L) < index) return false;
  return lua_opt_int(L, index, 0) != 0;
}

static int l_lcd_fillScreen(lua_State* L) {
  if (!g_renderer) return 0;
  const int color = lua_opt_int(L, 1, 1);  // 1=white, 0=black
  g_renderer->clearScreen(color ? 0xFF : 0x00);
  return 0;
}

static int l_lcd_display(lua_State* L) {
  if (!g_renderer) return 0;
  g_renderer->displayBuffer();
  return 0;
}

static int l_lcd_setTextSize(lua_State* L) {
  // We use fixed-size fonts in the renderer; textSize is a conceptual scaling.
  // Store the multiplier as a Lua global for use by drawText.
  const int size = lua_opt_int(L, 1, 1);
  lua_pushinteger(L, size);
  lua_setglobal(L, "_lcd_text_size");
  lua_settop(L, 0);
  return 0;
}

static int l_lcd_setTextColor(lua_State* L) {
  // 1=white text on black bg, 0=black text on white bg
  const int color = lua_opt_int(L, 1, 1);
  lua_pushboolean(L, color ? false : true);
  lua_setglobal(L, "_lcd_black_text");
  lua_settop(L, 0);
  return 0;
}

static int l_lcd_setCursor(lua_State* L) {
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  lua_pushinteger(L, x);
  lua_setglobal(L, "_lcd_cursor_x");
  lua_pushinteger(L, y);
  lua_setglobal(L, "_lcd_cursor_y");
  return 0;
}

static int l_lcd_print(lua_State* L) {
  if (!g_renderer) return 0;
  const char* text = luaL_checkstring(L, 1);
  lua_getglobal(L, "_lcd_cursor_x");
  lua_getglobal(L, "_lcd_cursor_y");
  const int x = (int)lua_tointeger(L, -2);
  const int y = (int)lua_tointeger(L, -1);
  lua_pop(L, 2);
  lua_getglobal(L, "_lcd_black_text");
  const bool black = lua_toboolean(L, -1);
  lua_pop(L, 1);
  g_renderer->drawText(UI_10_FONT_ID, x, y, text, black);
  return 0;
}

static int l_lcd_drawText(lua_State* L) {
  if (!g_renderer) return 0;
  const char* text = luaL_checkstring(L, 1);
  const int x = lua_check_int(L, 2);
  const int y = lua_check_int(L, 3);
  lua_getglobal(L, "_lcd_black_text");
  const bool black = lua_toboolean(L, -1);
  lua_pop(L, 1);
  g_renderer->drawText(UI_10_FONT_ID, x, y, text, black);
  return 0;
}

static int l_lcd_drawRect(lua_State* L) {
  if (!g_renderer) return 0;
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  const int w = lua_check_int(L, 3);
  const int h = lua_check_int(L, 4);
  const bool filled = (lua_gettop(L) >= 5) ? (lua_toboolean(L, 5) != 0) : false;
  const bool white = lua_color_is_white(L, 6);  // 0=black (default), 1=white
  if (filled) {
    g_renderer->fillRect(x, y, w, h, !white);
  } else {
    g_renderer->drawRect(x, y, w, h, !white);
  }
  return 0;
}

static int l_lcd_fillRect(lua_State* L) {
  if (!g_renderer) return 0;
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  const int w = lua_check_int(L, 3);
  const int h = lua_check_int(L, 4);
  const bool white = lua_color_is_white(L, 5);  // 0=black (default), 1=white
  g_renderer->fillRect(x, y, w, h, !white);
  return 0;
}

static int l_lcd_drawLine(lua_State* L) {
  if (!g_renderer) return 0;
  const int x1 = lua_check_int(L, 1);
  const int y1 = lua_check_int(L, 2);
  const int x2 = lua_check_int(L, 3);
  const int y2 = lua_check_int(L, 4);
  const bool white = lua_color_is_white(L, 5);  // 0=black (default), 1=white
  g_renderer->drawLine(x1, y1, x2, y2, !white);
  return 0;
}

static int l_lcd_drawLineH(lua_State* L) {
  if (!g_renderer) return 0;
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  const int w = lua_check_int(L, 3);
  const bool white = lua_color_is_white(L, 4);  // 0=black (default), 1=white
  g_renderer->drawLine(x, y, x + w - 1, y, !white);
  return 0;
}

static int l_lcd_drawLineV(lua_State* L) {
  if (!g_renderer) return 0;
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  const int h = lua_check_int(L, 3);
  const bool white = lua_color_is_white(L, 4);  // 0=black (default), 1=white
  g_renderer->drawLine(x, y, x, y + h - 1, !white);
  return 0;
}

static int l_lcd_drawCircle(lua_State* L) {
  if (!g_renderer) return 0;
  const int cx = lua_check_int(L, 1);
  const int cy = lua_check_int(L, 2);
  const int r = lua_check_int(L, 3);
  const bool filled = (lua_gettop(L) >= 4) ? (lua_toboolean(L, 4) != 0) : false;
  const bool white = lua_color_is_white(L, 5);  // 0=black (default), 1=white
  if (filled) {
    for (int y = -r; y <= r; y++) {
      int dx = (int)(sqrtf((float)(r * r - y * y)));
      g_renderer->drawLine(cx - dx, cy + y, cx + dx, cy + y, !white);
    }
  } else {
    int dx = r;
    int dy = 0;
    int err = 0;
    while (dx >= dy) {
      g_renderer->drawPixel(cx + dx, cy + dy, !white);
      g_renderer->drawPixel(cx + dy, cy + dx, !white);
      g_renderer->drawPixel(cx - dy, cy + dx, !white);
      g_renderer->drawPixel(cx - dx, cy + dy, !white);
      g_renderer->drawPixel(cx - dx, cy - dy, !white);
      g_renderer->drawPixel(cx - dy, cy - dx, !white);
      g_renderer->drawPixel(cx + dy, cy - dx, !white);
      g_renderer->drawPixel(cx + dx, cy - dy, !white);
      dy++;
      if (err <= 0) {
        err += 2 * dy + 1;
      } else {
        dx--;
        err += 2 * (dy - dx) + 1;
      }
    }
  }
  return 0;
}

static int l_lcd_fillCircle(lua_State* L) {
  if (!g_renderer) return 0;
  const int cx = lua_check_int(L, 1);
  const int cy = lua_check_int(L, 2);
  const int r = lua_check_int(L, 3);
  const bool white = lua_color_is_white(L, 4);  // 0=black (default), 1=white
  for (int y = -r; y <= r; y++) {
    int dx = (int)(sqrtf((float)(r * r - y * y)));
    g_renderer->drawLine(cx - dx, cy + y, cx + dx, cy + y, !white);
  }
  return 0;
}

static int l_lcd_drawPixel(lua_State* L) {
  if (!g_renderer) return 0;
  const int x = lua_check_int(L, 1);
  const int y = lua_check_int(L, 2);
  // 3rd arg: true/1 = black pixel (default), false/0 = white pixel.
  bool state = true;
  if (lua_gettop(L) >= 3) {
    if (lua_isboolean(L, 3)) {
      state = lua_toboolean(L, 3);
    } else {
      state = lua_opt_int(L, 3, 1) != 0;
    }
  }
  g_renderer->drawPixel(x, y, state);
  return 0;
}

static int l_lcd_drawCenteredText(lua_State* L) {
  if (!g_renderer) return 0;
  const char* text = luaL_checkstring(L, 1);
  const int y = lua_check_int(L, 2);
  bool black = true;
  if (lua_gettop(L) >= 3) {
    // Optional explicit color: 1=white, 0=black (same convention as shapes).
    black = (lua_opt_int(L, 3, 0) == 0);
  } else {
    // Default: follow the current setTextColor() state (like drawText/print).
    lua_getglobal(L, "_lcd_black_text");
    black = lua_toboolean(L, -1);
    lua_pop(L, 1);
  }
  g_renderer->drawCenteredText(UI_10_FONT_ID, y, text, black);
  return 0;
}

static int l_lcd_getWidth(lua_State* L) {
  lua_pushinteger(L, g_renderer ? g_renderer->getDisplayWidth() : 480);
  return 1;
}

static int l_lcd_getHeight(lua_State* L) {
  lua_pushinteger(L, g_renderer ? g_renderer->getDisplayHeight() : 800);
  return 1;
}

static int l_lcd_getTextWidth(lua_State* L) {
  if (!g_renderer) { lua_pushinteger(L, 0); return 1; }
  const char* text = luaL_checkstring(L, 1);
  const int w = g_renderer->getTextWidth(UI_10_FONT_ID, text);
  lua_pushinteger(L, w);
  return 1;
}

static int l_lcd_getLineHeight(lua_State* L) {
  if (!g_renderer) { lua_pushinteger(L, 16); return 1; }
  const int h = g_renderer->getLineHeight(UI_10_FONT_ID);
  lua_pushinteger(L, h);
  return 1;
}

// drawWrappedText(text, x, y, maxWidth, maxLines?) — word-wraps text into at
// most maxLines lines (excess truncated with an ellipsis) and draws each line
// below the previous one. Uses the current setTextColor() setting. Prevents
// text from ever overflowing the screen edge.
static int l_lcd_drawWrappedText(lua_State* L) {
  if (!g_renderer) return 0;
  const char* text = luaL_checkstring(L, 1);
  const int x = lua_check_int(L, 2);
  const int y = lua_check_int(L, 3);
  const int maxWidth = lua_check_int(L, 4);
  const int maxLines = lua_opt_int(L, 5, 10);

  lua_getglobal(L, "_lcd_black_text");
  const bool black = lua_toboolean(L, -1);
  lua_pop(L, 1);

  const std::vector<std::string> lines =
      g_renderer->wrappedText(UI_10_FONT_ID, text, maxWidth, maxLines);
  const int lh = g_renderer->getLineHeight(UI_10_FONT_ID);
  int dy = y;
  for (const auto& line : lines) {
    g_renderer->drawText(UI_10_FONT_ID, x, dy, line.c_str(), black);
    dy += lh;
  }
  return 0;
}

static int l_lcd_clear(lua_State* L) {
  if (!g_renderer) return 0;
  g_renderer->clearScreen(0xFF);
  return 0;
}

static int l_lcd_fillScreenColor(lua_State* L) {
  if (!g_renderer) return 0;
  const int color = lua_opt_int(L, 1, 1);
  g_renderer->clearScreen(color ? 0xFF : 0x00);
  return 0;
}

// ---------------------------------------------------------------------------
// File I/O API (fs.*) — 10 functions, sandboxed
// ---------------------------------------------------------------------------

static int l_fs_readFile(lua_State* L) {
  const char* filename = luaL_checkstring(L, 1);
  const std::string path = resolveSandboxPath(filename);
  if (path.empty()) {
    luaL_error(L, "invalid filename: %s", filename);
    lua_pushnil(L);
    return 1;
  }
  const String content = Storage.readFile(path.c_str());
  if (content.length() == 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushlstring(L, content.c_str(), content.length());
  return 1;
}

static int l_fs_writeFile(lua_State* L) {
  const char* filename = luaL_checkstring(L, 1);
  const char* content = luaL_checkstring(L, 2);
  const std::string path = resolveSandboxPath(filename);
  if (path.empty()) {
    luaL_error(L, "invalid filename: %s", filename);
    return 0;
  }
  ensureSandboxDir();
  const bool ok = Storage.writeFile(path.c_str(), String(content));
  lua_pushboolean(L, ok);
  return 1;
}

static int l_fs_appendFile(lua_State* L) {
  const char* filename = luaL_checkstring(L, 1);
  const char* content = luaL_checkstring(L, 2);
  const std::string path = resolveSandboxPath(filename);
  if (path.empty()) {
    luaL_error(L, "invalid filename: %s", filename);
    return 0;
  }
  ensureSandboxDir();
  HalFile file = Storage.open(path.c_str(), O_APPEND | O_WRONLY | O_CREAT);
  if (!file) {
    lua_pushboolean(L, false);
    return 1;
  }
  file.write(reinterpret_cast<const uint8_t*>(content), strlen(content));
  file.close();
  lua_pushboolean(L, true);
  return 1;
}

static int l_fs_exists(lua_State* L) {
  const char* filename = luaL_checkstring(L, 1);
  const std::string path = resolveSandboxPath(filename);
  if (path.empty()) {
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushboolean(L, Storage.exists(path.c_str()));
  return 1;
}

static int l_fs_listDir(lua_State* L) {
  const char* subdir = luaL_checkstring(L, 1);
  const std::string dir = getSandboxDir() + subdir;
  if (dir.find("..") != std::string::npos) {
    lua_newtable(L);
    return 1;
  }
  lua_newtable(L);
  if (!Storage.exists(dir.c_str())) {
    return 1;
  }
  std::vector<String> files = Storage.listFiles(dir.c_str(), 100);
  for (size_t i = 0; i < files.size(); i++) {
    lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
    lua_pushstring(L, files[i].c_str());
    lua_settable(L, -3);
  }
  return 1;
}

static int l_fs_remove(lua_State* L) {
  const char* filename = luaL_checkstring(L, 1);
  const std::string path = resolveSandboxPath(filename);
  if (path.empty()) {
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushboolean(L, Storage.remove(path.c_str()));
  return 1;
}

static int l_fs_mkdir(lua_State* L) {
  const char* dirname = luaL_checkstring(L, 1);
  const std::string path = getSandboxDir() + dirname;
  if (path.find("..") != std::string::npos) {
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushboolean(L, Storage.mkdir(path.c_str()));
  return 1;
}

static int l_fs_rename(lua_State* L) {
  const char* oldName = luaL_checkstring(L, 1);
  const char* newName = luaL_checkstring(L, 2);
  const std::string oldPath = resolveSandboxPath(oldName);
  const std::string newPath = resolveSandboxPath(newName);
  if (oldPath.empty() || newPath.empty()) {
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushboolean(L, Storage.rename(oldPath.c_str(), newPath.c_str()));
  return 1;
}

static int l_fs_getDataDir(lua_State* L) {
  lua_pushstring(L, getSandboxDir().c_str());
  return 1;
}

// ---------------------------------------------------------------------------
// Input API (input.*) — 5 functions
// ---------------------------------------------------------------------------

static int l_input_isPressed(lua_State* L) {
  if (!g_input) { lua_pushboolean(L, false); return 1; }
  const char* btnStr = luaL_checkstring(L, 1);
  static const struct { const char* name; MappedInputManager::Button btn; } btnMap[] = {
    {"back", MappedInputManager::Button::Back},
    {"ok", MappedInputManager::Button::Confirm},
    {"confirm", MappedInputManager::Button::Confirm},
    {"left", MappedInputManager::Button::Left},
    {"right", MappedInputManager::Button::Right},
    {"up", MappedInputManager::Button::Up},
    {"down", MappedInputManager::Button::Down},
    {"power", MappedInputManager::Button::Power},
    {nullptr, MappedInputManager::Button::Back}
  };
  for (int i = 0; btnMap[i].name; i++) {
    if (strcmp(btnStr, btnMap[i].name) == 0) {
      lua_pushboolean(L, g_input->isPressed(btnMap[i].btn));
      return 1;
    }
  }
  lua_pushboolean(L, false);
  return 1;
}

static int l_input_wasPressed(lua_State* L) {
  if (!g_input) { lua_pushboolean(L, false); return 1; }
  const char* btnStr = luaL_checkstring(L, 1);
  static const struct { const char* name; MappedInputManager::Button btn; } btnMap[] = {
    {"back", MappedInputManager::Button::Back},
    {"ok", MappedInputManager::Button::Confirm},
    {"confirm", MappedInputManager::Button::Confirm},
    {"left", MappedInputManager::Button::Left},
    {"right", MappedInputManager::Button::Right},
    {"up", MappedInputManager::Button::Up},
    {"down", MappedInputManager::Button::Down},
    {"power", MappedInputManager::Button::Power},
    {nullptr, MappedInputManager::Button::Back}
  };
  for (int i = 0; btnMap[i].name; i++) {
    if (strcmp(btnStr, btnMap[i].name) == 0) {
      lua_pushboolean(L, g_input->wasPressed(btnMap[i].btn));
      return 1;
    }
  }
  lua_pushboolean(L, false);
  return 1;
}

static int l_input_waitButton(lua_State* L) {
  // Poll input until any button is pressed. Returns the button name as a string.
  if (!g_input) {
    lua_pushnil(L);
    return 1;
  }
  static const char* btnNames[] = {"back", "ok", "left", "right", "up", "down", nullptr};
  static const MappedInputManager::Button btnIds[] = {
    MappedInputManager::Button::Back,
    MappedInputManager::Button::Confirm,
    MappedInputManager::Button::Left,
    MappedInputManager::Button::Right,
    MappedInputManager::Button::Up,
    MappedInputManager::Button::Down,
    MappedInputManager::Button::Back
  };
  while (true) {
    g_input->update();
    for (int i = 0; btnNames[i]; i++) {
      if (g_input->wasPressed(btnIds[i])) {
        lua_pushstring(L, btnNames[i]);
        return 1;
      }
    }
    yield();
    delay(10);
  }
}

static int l_input_getHeldTime(lua_State* L) {
  if (!g_input) { lua_pushinteger(L, 0); return 1; }
  lua_pushinteger(L, static_cast<lua_Integer>(g_input->getHeldTime()));
  return 1;
}

// ---------------------------------------------------------------------------
// System API (sys.*) — 8 functions
// ---------------------------------------------------------------------------

static int l_sys_getTime(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(time(nullptr)));
  return 1;
}

static int l_sys_getUptimeMs(lua_State* L) {
  // Milliseconds since boot — the millisecond clock for game timers/speeds
  // (sys.getTime() only returns whole seconds from the RTC).
  lua_pushinteger(L, static_cast<lua_Integer>(millis()));
  return 1;
}

static int l_sys_getBattery(lua_State* L) {
  const uint16_t pct = powerManager.getBatteryPercentage();
  lua_pushinteger(L, static_cast<lua_Integer>(pct));
  return 1;
}

static int l_sys_getBatteryVoltage(lua_State* L) {
  const int adc = analogRead(1);
  const float voltage = adc * 3.3f / 4095.0f * 11.0f;
  lua_pushinteger(L, static_cast<lua_Integer>(voltage * 1000));
  return 1;
}

static int l_sys_getSetting(lua_State* L) {
  if (!g_settings) { lua_pushnil(L); return 1; }
  const char* key = luaL_checkstring(L, 1);
  if (strcmp(key, "darkMode") == 0) {
    lua_pushboolean(L, g_settings->darkMode);
  } else if (strcmp(key, "orientation") == 0) {
    lua_pushinteger(L, g_settings->orientation);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int l_sys_log(lua_State* L) {
  const char* msg = luaL_checkstring(L, 1);
  const char* name = lua_plugin_get_plugin_name();
  if (name != nullptr && name[0] != '\0') {
    LOG_INF(TAG, "[PLUGIN:%s] %s", name, msg);
  } else {
    LOG_INF(TAG, "[PLUGIN] %s", msg);
  }
  return 0;
}

static int l_sys_finish(lua_State* L) {
  g_wantsExit = true;
  return 0;
}

static int l_sys_random(lua_State* L) {
  const int min_val = lua_opt_int(L, 1, 0);
  const int max_val = lua_opt_int(L, 2, 32767);
  lua_pushinteger(L, static_cast<lua_Integer>(min_val + random(max_val - min_val + 1)));
  return 1;
}

static int l_sys_getDisplayWidth(lua_State* L) {
  lua_pushinteger(L, g_renderer ? g_renderer->getDisplayWidth() : 480);
  return 1;
}

// ---------------------------------------------------------------------------
// String API (plugin_str.*) — 6 functions
// ---------------------------------------------------------------------------

static int l_str_wrapText(lua_State* L) {
  if (!g_renderer) {
    lua_pushstring(L, "");
    return 1;
  }
  const char* text = luaL_checkstring(L, 1);
  const int maxWidth = lua_check_int(L, 2);
  const int maxLines = lua_opt_int(L, 3, 10);
  std::vector<std::string> lines = g_renderer->wrappedText(UI_10_FONT_ID, text, maxWidth, maxLines);
  lua_newtable(L);
  for (size_t i = 0; i < lines.size(); i++) {
    lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
    lua_pushstring(L, lines[i].c_str());
    lua_settable(L, -3);
  }
  return 1;
}

static int l_str_truncate(lua_State* L) {
  if (!g_renderer) {
    lua_pushstring(L, "");
    return 1;
  }
  const char* text = luaL_checkstring(L, 1);
  const int maxWidth = lua_check_int(L, 2);
  std::string truncated = g_renderer->truncatedText(UI_10_FONT_ID, text, maxWidth);
  lua_pushstring(L, truncated.c_str());
  return 1;
}

static int l_str_len(lua_State* L) {
  const char* str = luaL_checkstring(L, 1);
  lua_pushinteger(L, static_cast<lua_Integer>(strlen(str)));
  return 1;
}

static int l_str_upper(lua_State* L) {
  const char* str = luaL_checkstring(L, 1);
  std::string s(str);
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') c -= 32;
  }
  lua_pushstring(L, s.c_str());
  return 1;
}

static int l_str_lower(lua_State* L) {
  const char* str = luaL_checkstring(L, 1);
  std::string s(str);
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }
  lua_pushstring(L, s.c_str());
  return 1;
}

static int l_str_format(lua_State* L) {
  const char* fmt = luaL_checkstring(L, 1);
  std::string result;
  int argIdx = 2;
  for (const char* p = fmt; *p; p++) {
    if (*p == '%' && *(p + 1) == 's' && argIdx <= lua_gettop(L)) {
      result += luaL_checkstring(L, argIdx++);
      p++;
    } else if (*p == '%' && *(p + 1) == 'd' && argIdx <= lua_gettop(L)) {
      result += std::to_string((long)lua_check_int(L, argIdx++));
      p++;
    } else {
      result += *p;
    }
  }
  lua_pushstring(L, result.c_str());
  return 1;
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

static void registerModule(lua_State* L, const char* name, luaL_Reg* funcs) {
  lua_newtable(L);
  for (luaL_Reg* reg = funcs; reg->name; reg++) {
    lua_pushcfunction(L, reg->func);
    lua_setfield(L, -2, reg->name);
  }
  lua_setglobal(L, name);
}

static luaL_Reg lcd_funcs[] = {
  {"fillScreen", l_lcd_fillScreen},
  {"clear", l_lcd_clear},
  {"display", l_lcd_display},
  {"setTextSize", l_lcd_setTextSize},
  {"setTextColor", l_lcd_setTextColor},
  {"setCursor", l_lcd_setCursor},
  {"print", l_lcd_print},
  {"drawText", l_lcd_drawText},
  {"drawCenteredText", l_lcd_drawCenteredText},
  {"drawWrappedText", l_lcd_drawWrappedText},
  {"drawRect", l_lcd_drawRect},
  {"fillRect", l_lcd_fillRect},
  {"drawLine", l_lcd_drawLine},
  {"drawLineH", l_lcd_drawLineH},
  {"drawLineV", l_lcd_drawLineV},
  {"drawCircle", l_lcd_drawCircle},
  {"fillCircle", l_lcd_fillCircle},
  {"drawPixel", l_lcd_drawPixel},
  {"fillScreenColor", l_lcd_fillScreenColor},
  {"getWidth", l_lcd_getWidth},
  {"getHeight", l_lcd_getHeight},
  {"getTextWidth", l_lcd_getTextWidth},
  {"getLineHeight", l_lcd_getLineHeight},
  {nullptr, nullptr}
};

static luaL_Reg fs_funcs[] = {
  {"readFile", l_fs_readFile},
  {"writeFile", l_fs_writeFile},
  {"appendFile", l_fs_appendFile},
  {"exists", l_fs_exists},
  {"listDir", l_fs_listDir},
  {"remove", l_fs_remove},
  {"mkdir", l_fs_mkdir},
  {"rename", l_fs_rename},
  {"getDataDir", l_fs_getDataDir},
  {nullptr, nullptr}
};

static luaL_Reg input_funcs[] = {
  {"isPressed", l_input_isPressed},
  {"wasPressed", l_input_wasPressed},
  {"waitButton", l_input_waitButton},
  {"getHeldTime", l_input_getHeldTime},
  {nullptr, nullptr}
};

static luaL_Reg sys_funcs[] = {
  {"getTime", l_sys_getTime},
  {"getUptimeMs", l_sys_getUptimeMs},
  {"getBattery", l_sys_getBattery},
  {"getBatteryVoltage", l_sys_getBatteryVoltage},
  {"getSetting", l_sys_getSetting},
  {"log", l_sys_log},
  {"finish", l_sys_finish},
  {"random", l_sys_random},
  {"getDisplayWidth", l_sys_getDisplayWidth},
  {nullptr, nullptr}
};

static luaL_Reg str_funcs[] = {
  {"wrapText", l_str_wrapText},
  {"truncate", l_str_truncate},
  {"len", l_str_len},
  {"upper", l_str_upper},
  {"lower", l_str_lower},
  {"format", l_str_format},
  {nullptr, nullptr}
};

// Custom linit replacement — calls standard libs then registers plugin API
void lua_plugin_register_libs(lua_State* L) {
  // Open standard libraries (base, string, table, utf8, debug)
  luaL_openlibs(L);

  // Initialize default drawing state
  lua_pushinteger(L, 0);
  lua_setglobal(L, "_lcd_cursor_x");
  lua_pushinteger(L, 0);
  lua_setglobal(L, "_lcd_cursor_y");
  lua_pushboolean(L, true);  // default: black text
  lua_setglobal(L, "_lcd_black_text");
  lua_pushinteger(L, 1);
  lua_setglobal(L, "_lcd_text_size");

  // Register API modules
  registerModule(L, "lcd", lcd_funcs);     // 19 funcs
  registerModule(L, "fs", fs_funcs);       // 9 funcs
  registerModule(L, "input", input_funcs); // 4 funcs
  registerModule(L, "sys", sys_funcs);     // 8 funcs
  registerModule(L, "plugin_str", str_funcs); // 6 funcs

  // Create a "plugin" convenience table that aliases sys.finish / sys.log
  lua_newtable(L);
  lua_getglobal(L, "sys");
  lua_getfield(L, -1, "finish");
  lua_setfield(L, -3, "finish");
  lua_getfield(L, -1, "log");
  lua_setfield(L, -3, "log");
  lua_pop(L, 1);  // remove sys table
  lua_setglobal(L, "plugin");

  LOG_INF(TAG, "API registered: lcd(23) fs(9) input(4) sys(9) plugin_str(6) = 51 total");
}
