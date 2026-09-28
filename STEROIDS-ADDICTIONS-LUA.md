# CPR-vCodex Steroids — Lua Plugin System

> **SCOPE:** Lua 5.4 VM, plugin browser, Plugins shortcut, in-process disabled. High merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Plugin Browser** | Lists `.lua` files from `/.crosspoint/plugins/` |
| **Plugin Execution** | Runs Lua scripts with sandboxed API |
| **Plugins Shortcut** | Home/Apps shortcut → PluginBrowserActivity |
| **Built-in Plugins** | Examples: calculator, notes, clock, etc. |
| **API Access** | `cpr` table: display, input, storage, network, books, stats |

---

## 2. Technical Architecture

### 2.1 Lua VM Integration
```cpp
// lua.hpp (Lua 5.4.7)
lua_State* L = luaL_newstate();
luaL_openlibs(L);  // base, table, string, math, utf8

// Sandbox: remove dangerous libs
lua_pushnil(L); lua_setglobal(L, "os");
lua_pushnil(L); lua_setglobal(L, "io");
lua_pushnil(L); lua_setglobal(L, "debug");
```

### 2.2 CPR API Binding (`cpr` table)
```lua
-- Available in every plugin:
cpr.display       -- drawText, drawRect, clearScreen, displayBuffer
cpr.input         -- onButton, onTouch, getButtonState
cpr.storage       -- readFile, writeFile, listDir, exists
cpr.network       -- httpGet, httpPost (if WiFi)
cpr.books         -- getCurrentBook, getLibrary, openBook
cpr.stats         -- getGlobalSummary, getBookStats
cpr.ui            -- showToast, showDialog, showMenu
cpr.system        -- reboot, sleep, getVersion, getFreeHeap
```

### 2.3 Plugin Lifecycle
```
1. PluginBrowserActivity scans `/.crosspoint/plugins/*.lua`
2. User selects plugin → LuaPluginActivity created
3. luaL_loadfile() → lua_pcall() → runs plugin main()
4. Plugin registers callbacks via cpr.ui.onEvent()
5. Activity loop pumps Lua (coroutine.resume)
6. Plugin calls cpr.system.exit() → activity finishes
```

### 2.4 In-Process Execution (Disabled)
```cpp
// LuaPluginActivity::onEnter()
// if (context.inProcess) {
//     // Run in current task (DISABLED — stack/heap risk)
// } else {
//     // Normal: separate activity with own stack
// }
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/lua/*.h/*.cpp` | Lua 5.4.7 source (vendored) |
| `src/activities/apps/LuaPluginActivity.h/cpp` | Plugin execution activity |
| `src/activities/apps/PluginBrowserActivity.h/cpp` | Plugin listing/selection |
| `src/components/LuaPlugin.h/cpp` | API bindings (`cpr` table) |
| `src/activities/apps/AppsActivity.cpp` | "Plugins" shortcut registration |
| `src/activities/home/HomeActivity.cpp` | "Plugins" shortcut in Home grid |
| `src/ShortcutRegistry.h` | Shortcut definitions |

---

## 4. RAM/Flash Impact

| Metric | Value |
|--------|-------|
| Flash | +~180 KB (Lua VM + bindings) |
| RAM (idle) | +~2 KB (Lua state minimal) |
| RAM (running plugin) | +~15-30 KB (depends on plugin) |
| Stack | Separate task stack (4 KB default) |

---

## 5. Upstream Merge Notes

### HIGH RISK — PROTECTED
- Entire `src/lua/` vendored Lua 5.4.7
- `LuaPluginActivity` — New activity type
- `PluginBrowserActivity` — New activity type
- `LuaPlugin` API bindings (large surface)
- `ShortcutRegistry` additions

### Conflicts Guaranteed
- `ActivityManager` activity registration
- `AppsActivity` / `HomeActivity` shortcut additions
- Memory budget (Lua VM + plugin heap)

### Safe Cherry-Picks
- None — this is a standalone subsystem

---

## 6. Validation Checklist

- [ ] Plugin Browser lists `.lua` files from SD
- [ ] Selecting plugin launches LuaPluginActivity
- [ ] Plugin can draw to screen via `cpr.display`
- [ ] Plugin can handle input via `cpr.input`
- [ ] Plugin can read/write SD via `cpr.storage`
- [ ] Plugin can access book library via `cpr.books`
- [ ] Plugin can read stats via `cpr.stats`
- [ ] Multiple plugins can run sequentially
- [ ] Plugin exit returns to browser
- [ ] In-process mode disabled (no crash on complex plugins)
- [ ] Heap doesn't fragment after multiple plugin runs

---

## 7. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — LuaPlugin fast restart
- `STEROIDS-ADDICTIONS-SHORTCUTS.md` — Shortcut registration

---

*Last updated: 2026-09-28 | Commit: 7f120763 (Lua port) + subsequent fixes*