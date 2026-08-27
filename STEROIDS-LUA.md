# CPR-vCodex Steroids — Lua Plugin Development Guide

> **SCOPE:** This file is the complete reference for developing Lua plugins for the
> CPR-vCodex Steroids on-device plugin system. It covers the plugin lifecycle, the
> full `lcd.*`, `fs.*`, `input.*`, `sys.*`, and `plugin_str.*` API surfaces, file
> layout, sandboxing, memory limits, debugging, and best practices.

---

## 1. Overview

CPR-vCodex Steroids embeds a **Lua 5.4.7** interpreter that runs user-authored
`.lua` scripts as lightweight apps on the Xteink X4 e-reader. Plugins are
**not compiled into firmware** — they are plain text files placed on the SD card
and discovered at runtime by the `PluginBrowserActivity`.

The system is built around three C++ components:

| Component | File(s) | Role |
|---|---|---|
| **`LuaPluginVM`** | `src/LuaPluginVM.h`, `src/LuaPluginVM.cpp` | Manages the `lua_State`, custom 40 KB memory allocator, instruction-count safety hook, and callback dispatch. |
| **`LuaPluginAPI`** | `src/LuaPluginAPI.cpp` (headers: `LuaPluginAPI.h`, `SilentRestart.h`) | Registers the custom `lcd`, `fs`, `input`, `sys`, and `plugin_str` modules into the Lua globals. |
| **Activities** | `src/activities/apps/PluginBrowserActivity.{cpp,h}`, `src/activities/apps/LuaPluginActivity.{cpp,h}` | Browser UI that scans `/custom/` and the activity that hosts the Lua runtime. |

### Plugin Discovery

1. The user opens the **Apps** hub from Home and selects the **"Plugins"** entry.
2. `PluginBrowserActivity` scans `/custom/` on the SD card for `*.lua` files.
3. Each file's first ~20 lines are parsed for header comments:
   ```
   -- NAME: Snake Game
   -- DESC: Classic Snake game for CPR-vCodex
   -- ICON: AppsHub
   ```
4. The browser renders a scrolling list. Pressing **Confirm** launches the selected plugin.

---

## 2. Plugin Lifecycle

The Lua plugin system uses the **silent restart** mechanism for a seamless
transition between the Plugin Browser and the plugin itself. No "Loading..."
popup or screen flash occurs.

### Full Lifecycle

```
Boot (cold or silent)
  │
  ├─ setup() detects silentRebootTarget == SILENT_REBOOT_TARGET_PLUGIN (3)
  │   └─ snapshotPluginName / snapshotCallerFromApps / snapshotReturnToPluginBrowser
  │
  ├─ ActivityManager.goToPlugin(name, fromApps, returnToPluginBrowser)
  │   └─ LuaPluginActivity created
  │
  ├─ LuaPluginActivity::onEnter()
  │   ├─ Storage.exists("/custom/<name>.lua") → error screen if not found
  │   ├─ Read file into buffer (≤ 40 KB)
  │   ├─ checkMemoryAvailable() → free > 100 KB, maxAlloc > 75 KB
  │   ├─ lua_plugin_set_*() context setters
  │   ├─ vmInit() → creates lua_State with custom allocator + instruction hook
  │   ├─ vmLoad() → luaL_loadbuffer + lua_pcall (defines functions)
  │   ├─ vmRunMain() → calls init()
  │   └─ renderer.displayBuffer() → flush initial frame
  │
  ├─ LuaPluginActivity::loop()  [every ~10 ms; input updated by the main loop]
  │   ├─ Back SHORT press → delivered to onKey() first (plugin decides)
  │   ├─ Back held ≥ 1.5 s (LONG press) → exitRequested_ = true
  │   ├─ Check lua_plugin_wants_exit() → finish()
  │   ├─ vmCallCallback("onKey", 0) if onKey exists
  │   │   └─ error → log traceback to serial, show error screen, finish()
  │   └─ delay(10)
  │
  ├─ User calls sys.finish() OR long-presses Back
  │   └─ exitRequested_ = true
  │
  ├─ LuaPluginActivity::onExit()
  │   ├─ vmCallCallback("finish", 0) if the plugin defines finish()  ← persist state here
  │   ├─ vmShutdown() → lua_close (reclaims all Lua heap)
  │   ├─ "-- RESTART: no" → return (ActivityManager pops back to browser, no reboot)
  │   ├─ silentRestartToPluginBrowser() OR silentRestartToHome() OR silentRestartToApps()
  │   └─ ESP.restart()
  │
  └─ Boot again → route to PluginBrowser / Home / Apps
```

**In-process mode:** Plugins that declare `-- RESTART: no` skip the silent
restart entirely. The browser pushes the plugin activity onto the stack, and
exit just pops back — no `ESP.restart()` at all. All other lifecycle steps
(file check, VM init, `init()`/`onKey()` dispatch) are identical.

### Key Constraints

| Limit | Value | Notes |
|---|---|---|
| Lua source file size | 40 KB | `MAX_LUA_SOURCE_SIZE` |
| VM heap cap | 40 KB | `PLUGIN_MEM_CAP`, enforced by custom allocator |
| Instructions per callback | 100,000 | `MAX_INSTRUCTIONS_PER_CALLBACK`, aborts with error |
| Free heap required to start | > 100 KB free, > 75 KB maxAlloc | `checkMemoryAvailable()` |
| Plugin directory | `/custom/` | All `.lua` files are scanned |
| Plugin data directory | `/custom/<name>_data/` | Sandboxed file I/O |

---

## 3. File Layout

```
SD card:
  /custom/
    hello_world.lua       ← plugin script (max 40 KB)
    snake.lua             ← plugin script
    breakout.lua          ← plugin script
    sudoku_full.lua       ← plugin script
    todo_list.lua         ← plugin script
    hello_world_data/     ← sandboxed file I/O for hello_world plugin
      config.txt
      save.dat
    snake_data/           ← sandboxed file I/O for snake plugin
      highscore.txt
    ...
```

### Requirements for a Valid Plugin

1. **Filename:** `<name>.lua` (no spaces; use underscores). The filename (without
   `.lua`) is the plugin ID used for routing and sandboxing.
2. **Location:** Must be in `/custom/` on the SD card root.
3. **Headers:** The first lines should contain `-- NAME:`, `-- DESC:`, and
   `-- ICON:` comments. `NAME` is the display name; if absent, the filename
   (without extension) is used. `ICON` is reserved for future icon theming.
4. **Size:** ≤ 40 KB. If exceeded, `LuaPluginActivity::onEnter()` shows
   "Plugin too large" and exits.
5. **Entry points:** Must define `init()` (required). `onKey()`, `finish()`
   are optional.

---

## 4. Plugin Header Comments

Headers are parsed from the first 20 lines of the `.lua` file. Lines starting
with `-- NAME:`, `-- DESC:`, or `-- ICON:` are extracted (leading whitespace
tolerant).

| Header | Required | Purpose |
|---|---|---|
| `-- NAME: <string>` | No | Display name in the browser. Falls back to filename if omitted. |
| `-- DESC: <string>` | No | Short description shown below the name in the browser. |
| `-- ICON: <string>` | No | Icon name (currently unused; reserved for future icon mapping). |
| `-- RESTART: <yes\|no>` | No | `yes` (default): launch + exit use the silent fast reboot. `no`: run in-process without any reboot. |

Example:
```lua
-- NAME: Snake Game
-- DESC: Classic Snake game for CPR-vCodex
-- ICON: AppsHub
-- RESTART: no
```

---

## 5. Entry Points

### `init()` — **required**

Called once at plugin launch (after the VM is initialized and the script is
loaded). Use this to:
- Read saved state from `fs.*`
- Initialize game variables, board state, etc.
- Draw the initial screen
- Call `lcd.display()` to flush the framebuffer

```lua
function init()
  lcd.fillScreen(1)       -- 1 = white
  lcd.setTextColor(0)     -- 0 = black text
  lcd.drawText("Hello!", 20, 20)
  lcd.display()
end
```

### `onKey()` — optional

Called **every loop iteration** (~10 ms). This is the main game/animation loop.
Use `input.wasPressed()` for edge-triggered actions and `input.isPressed()` for
continuous input. Note: on e-ink the refresh blocks the loop for ~500 ms, so a
**short tap can fall between two input samples and be missed** — prefer
`input.isPressed()` plus a "held latch" (see the games in `plugins/`).

**Back button has two roles** (the device has very few buttons):

- **Short press** → delivered to `onKey()` first. The plugin decides what to
  do (e.g. cancel a sub-screen, go back one level). The plugin must call
  `sys.finish()` itself if it wants to exit on a short Back.
- **Long press (hold ≥ 1.5 s)** → always exits the plugin, regardless of what
  the script does. This is the guaranteed way out.

```lua
function onKey()
  if input.wasPressed("back") then
    -- short Back: cancel an in-plugin screen, or sys.finish() to exit
    cancelCurrentScreen()
    return
  end

  if input.isPressed("left") then
    playerX = playerX - 2
  end

  -- Redraw every frame
  draw()
end
```

### `finish()` — optional

Called on **every exit path** (just before the VM is shut down) — after
`sys.finish()`, a long-press Back, or a load error. Use it to persist state.
```lua
function finish()
  fs.writeFile("save.txt", tostring(score))
end
```

---

## 6. API Reference

### 6.1 `lcd.*` — Drawing (23 functions)

All drawing is done through the `GfxRenderer` singleton, which writes to the
e-ink framebuffer in 4-level grayscale. Colors are 1-bit: `1` = white, `0` = black.

| Function | Parameters | Description |
|---|---|---|
| `lcd.fillScreen(color?)` | `color: 1=white (default), 0=black` | Fill entire screen with a color |
| `lcd.clear()` | — | Same as `fillScreen(1)` |
| `lcd.display()` | — | Flush framebuffer to e-ink panel (triggers partial refresh) |
| `lcd.setTextSize(size)` | `size: 1 (default), 2, 3...` | Conceptual text scaling multiplier (stored in `_lcd_text_size` global) |
| `lcd.setTextColor(color)` | `color: 1=white text (default), 0=black text` | Set text foreground; `1` → black text on white, `0` → white text on black |
| `lcd.setCursor(x, y)` | `x, y: int` | Set cursor position for subsequent `print()` calls |
| `lcd.print(text)` | `text: string` | Draw text at current cursor position using `UI_10_FONT_ID` |
| `lcd.drawText(text, x, y)` | `text: string, x, y: int` | Draw text at (x, y) using the current `setTextColor()` |
| `lcd.drawCenteredText(text, y, color?)` | `text, y: int; color?: 0=black, 1=white` | Horizontally centered text. Defaults to the current `setTextColor()` |
| `lcd.drawWrappedText(text, x, y, maxWidth, maxLines?)` | `text, x, y, maxWidth: int; maxLines?: int (default 10)` | Word-wraps text into ≤ maxLines lines (ellipsis on excess) and draws each line below the previous one, using the current `setTextColor()`. Prevents text overflowing the screen edge |
| `lcd.drawRect(x, y, w, h, filled?, color?)` | `filled: bool; color?: 0=black (default), 1=white` | Draw rectangle outline or filled rect |
| `lcd.fillRect(x, y, w, h, color?)` | `color?: 0=black (default), 1=white` | Fill a rectangle |
| `lcd.drawLine(x1, y1, x2, y2, color?)` | `color?: 0=black (default), 1=white` | Draw a line |
| `lcd.drawLineH(x, y, w, color?)` | `color?: 0=black (default), 1=white` | Horizontal line |
| `lcd.drawLineV(x, y, h, color?)` | `color?: 0=black (default), 1=white` | Vertical line |
| `lcd.drawCircle(cx, cy, r, filled?, color?)` | `filled: bool; color?: 0=black (default), 1=white` | Draw circle outline or filled |
| `lcd.fillCircle(cx, cy, r, color?)` | `color?: 0=black (default), 1=white` | Fill a circle |
| `lcd.drawPixel(x, y, on?)` | `x, y: int, on: true (default)` | Draw a single pixel (black by default) |
| `lcd.fillScreenColor(color)` | `color: 1=white, 0=black` | Alias of `fillScreen` |
| `lcd.getWidth()` | — | Returns display width (480 for X4) |
| `lcd.getHeight()` | — | Returns display height (800 for X4) |
| `lcd.getTextWidth(text)` | `text: string` | Width of text in pixels |
| `lcd.getLineHeight()` | — | Line height in pixels |

**Coordinate system:** Top-left is (0, 0). X increases right, Y increases down.
Display dimensions for X4: 480 × 800 (portrait).

---

### 6.2 `fs.*` — File I/O (9 functions, sandboxed)

All file operations are restricted to `/custom/<plugin_name>_data/`. Path traversal
(`..`) is rejected. The directory is auto-created on first write.

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `fs.readFile(filename)` | `filename: string` | `string\|nil` | Read file contents into a Lua string |
| `fs.writeFile(filename, content)` | `filename, content: string` | `bool` | Write/overwrite file |
| `fs.appendFile(filename, content)` | `filename, content: string` | `bool` | Append to file |
| `fs.exists(filename)` | `filename: string` | `bool` | Check file existence |
| `fs.listDir(subdir?)` | `subdir: string (default "")` | `table` | List files in sandbox subdir (1-indexed array) |
| `fs.remove(filename)` | `filename: string` | `bool` | Delete a file |
| `fs.mkdir(dirname)` | `dirname: string` | `bool` | Create a subdirectory |
| `fs.rename(oldName, newName)` | `oldName, newName: string` | `bool` | Rename a file |
| `fs.getDataDir()` | — | `string` | Returns the full sandbox path |

Example:
```lua
-- Save high score
fs.writeFile("highscore.txt", tostring(score))

-- Load high score
local saved = fs.readFile("highscore.txt")
if saved then
  highScore = tonumber(saved) or 0
end
```

---

### 6.3 `input.*` — Input (4 functions)

Buttons are identified by string names. The X4 has side buttons (Up, Down,
Left, Right, OK/Confirm) and a power button.

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `input.isPressed(button)` | `button: string` | `bool` | True if button is currently held |
| `input.wasPressed(button)` | `button: string` | `bool` | True on the rising edge (press moment) |
| `input.waitButton()` | — | `string` | Blocks until any button is pressed; returns button name |
| `input.getHeldTime()` | — | `int` | How long the last-held button has been held (ms) |

**Button names:**

| Name | Physical button |
|---|---|
| `"back"` | Back button |
| `"ok"` / `"confirm"` | Center/OK button |
| `"left"` | Left button |
| `"right"` | Right button |
| `"up"` | Up button |
| `"down"` | Down button |
| `"power"` | Power button |

Example:
```lua
function onKey()
  if input.wasPressed("back") then
    sys.finish()
  end
  if input.isPressed("left") then
    playerX = playerX - 2
  end
end
```

---

### 6.4 `sys.*` — System (9 functions)

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `sys.getTime()` | — | `int` | Unix epoch time (seconds) |
| `sys.getUptimeMs()` | — | `int` | Milliseconds since boot — the millisecond clock for game timers/speeds |
| `sys.getBattery()` | — | `int` | Battery percentage (0–100) |
| `sys.getBatteryVoltage()` | — | `int` | Battery voltage in millivolts |
| `sys.getSetting(key)` | `key: string` | `bool\|int\|nil` | Read a device setting (`"darkMode"`, `"orientation"`) |
| `sys.log(msg)` | `msg: string` | — | Log to ESP32 serial, tagged `[PLUGIN:<name>]` (e.g. `[PLUGIN:hello_world]`) |
| `sys.finish()` | — | — | Request plugin exit (sets `wantsExit` flag) |
| `sys.random(min, max)` | `min: int (default 0), max: int (default 32767)` | `int` | Random integer in `[min, max]` |
| `sys.getDisplayWidth()` | — | `int` | Display width in pixels |

**Important:** `sys.finish()` does NOT terminate immediately — it sets a flag that
`LuaPluginActivity::loop()` checks between callback dispatches. The plugin's
`onKey()` function should `return` immediately after calling `sys.finish()`.

---

### 6.5 `plugin_str.*` — String Utilities (6 functions)

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `plugin_str.wrapText(text, maxWidth)` | `text: string, maxWidth: int` | `table` | Wrap text into lines fitting maxWidth (1-indexed array) |
| `plugin_str.truncate(text, maxWidth)` | `text: string, maxWidth: int` | `string` | Truncate text to fit maxWidth with "..." |
| `plugin_str.len(str)` | `str: string` | `int` | String length (bytes) |
| `plugin_str.upper(str)` | `str: string` | `string` | Uppercase |
| `plugin_str.lower(str)` | `str: string` | `string` | Lowercase |
| `plugin_str.format(fmt, ...)` | `fmt: string, args...` | `string` | Mini `printf`-style formatting (`%s`, `%d`) |

---

### 6.6 `plugin.*` — Convenience Aliases

A `plugin` table is created as a convenience alias that re-exports
`sys.finish()` and `sys.log()`:

| Function | Equivalent |
|---|---|
| `plugin.finish()` | `sys.finish()` |
| `plugin.log(msg)` | `sys.log(msg)` |

---

### 6.7 Standard Lua Libraries

The following standard Lua 5.4.7 libraries are available:
- **`base`** — `print`, `tonumber`, `tostring`, `type`, `math` (via `math`), `select`, `pairs`, `ipairs`, `next`, `getmetatable`, `setmetatable`, `rawget`, `rawset`, `rawlen`, `rawequal`, `error`, `assert`, `pcall`, `xpcall`, etc.
- **`string`** — `string.len`, `string.sub`, `string.find`, `string.gsub`, `string.match`, etc.
- **`table`** — `table.insert`, `table.remove`, `table.concat`, `table.sort`, `table.maxn`, etc.
- **`math`** — `math.abs`, `math.floor`, `math.ceil`, `math.random`, `math.sqrt`, `math.max`, `math.min`, etc.
- **`utf8`** — UTF-8 aware string functions (`utf8.len`, `utf8.sub`, etc.)
- **`debug`** — Debug hooks (limited usefulness in this environment)

`io` and `os` libraries are registered but **do not have functional implementations**
(their file/network operations are non-functional on the ESP32-C3 embedded
environment); use the `fs.*` and `sys.*` APIs instead.

---

## 7. Writing Your First Plugin

### Step 1: Create the plugin file

Write a file `/custom/my_plugin.lua` on the SD card.

### Step 2: Add headers

```lua
-- NAME: My Plugin
-- DESC: A brief description of what it does
-- ICON: AppsHub
```

### Step 3: Implement `init()` and `onKey()`

```lua
-- NAME: Bounce
-- DESC: Bouncing ball demo
-- ICON: AppsHub

local W = lcd.getWidth()
local H = lcd.getHeight()

local ball = { x = W / 2, y = H / 2, dx = 2, dy = 2, r = 10 }

function init()
  lcd.fillScreen(1)
  draw()
end

function draw()
  lcd.fillScreen(1)
  lcd.fillCircle(ball.x, ball.y, ball.r)
  lcd.display()
end

function onKey()
  if input.wasPressed("back") then
    sys.finish()
    return
  end

  ball.x = ball.x + ball.dx
  ball.y = ball.y + ball.dy

  if ball.x <= ball.r or ball.x >= W - ball.r then ball.dx = -ball.dx end
  if ball.y <= ball.r or ball.y >= H - ball.r then ball.dy = -ball.dy end

  draw()
end
```

### Step 4: Test

1. Power-cycle the device (or navigate to the Plugin Browser from Apps).
2. Select your plugin from the list.
3. The plugin launches. Press **Back** to exit.

---

## 8. Best Practices & Gotchas

### 8.1 No top-level loops

Do **not** write `while true do` or infinite loops at the top level of your
plugin. The `onKey()` function is called automatically every ~10 ms by the
activity loop. Use `onKey()` as your frame function instead:

**Bad:**
```lua
function onKey()
  while true do
    update()
    draw()
  end
end
```

**Good:**
```lua
function onKey()
  update()
  draw()
end
```

### 8.2 Instruction limit

Each `onKey()` call has a budget of 100,000 Lua instructions. If exceeded,
the VM aborts with an error. For long-running computations, split the work
across multiple `onKey()` calls or use `input.waitButton()` to yield.

### 8.3 File I/O is sandboxed

Files are stored in `/custom/<plugin_name>_data/`. You cannot read or write
outside this directory. Path traversal via `..` is rejected.

### 8.4 Save state on exit

There is no automatic save — if the device loses power or the plugin crashes,
any in-RAM state is lost. Always persist important data with `fs.writeFile()`.

### 8.5 E-ink refresh considerations

Each `lcd.display()` call triggers an e-ink partial refresh, which takes
~500 ms and wears on the panel. For animations, call `lcd.display()` only
when the screen has actually changed.

### 8.6 Memory awareness

The Lua VM has a 40 KB allocation cap. Complex games or large data structures
may hit this limit. Monitor with `sys.log()` — the VM logs allocation stats
at startup.

### 8.7 Use `input.wasPressed()` for edges, `input.isPressed()` for hold

For button-press actions (menu navigation, single-step games), use
`input.wasPressed()` which fires once on the press edge. For continuous
movement (holding left to move a character), use `input.isPressed()` which
returns true every frame the button is held.

---

## 9. Plugin Manifest & Headers

The Plugin Browser reads a plugin's metadata from comment headers in the first
20 lines of the script. The parser is simple string matching — no Lua execution
happens during header parsing, so it is safe even for scripts with syntax
errors (the error will surface when the script is actually loaded).

| Header | Format | Required | Default |
|---|---|---|---|
| `-- NAME: <text>` | Text after the colon | No | Filename without `.lua` |
| `-- DESC: <text>` | Text after the colon | No | (empty string) |
| `-- ICON: <text>` | Icon name | No | (none — no icon drawn yet) |
| `-- RESTART: <yes\|no>` | `no`/`false`/`0`/`off` disable the fast reboot | No | `yes` (fast reboot) |

The parser trims leading/trailing whitespace from all header values.

### Fast reboot vs in-process (`-- RESTART: no`)

By default launching a plugin uses a **silent fast reboot**
(`silentRestartToPlugin()` → `ESP.restart()`), and exiting does the same to get
back to the browser. This guarantees a clean, unfragmented heap.

A plugin can opt out with `-- RESTART: no`. It is then launched **in-process**:
the Plugin Browser is pushed onto the activity stack, the plugin runs without
any reboot, and when it exits (`sys.finish()`, Back, or a runtime error) the
device simply pops straight back to the browser. This makes plugin development
much faster (no reboots per test cycle). Use the reboot mode for plugins that
need a pristine heap or after WiFi has fragmented memory.

---

## 10. Debugging

### Serial log tags

The plugin system logs through the project's `LOG_*` macros (`lib/Logging`),
so everything — including Lua errors — appears on the USB serial console in
the standard `[timestamp] [level] [tag]` format, and `ERR` lines are also kept
in the RTC ring buffer for crash reports.

| Tag | Source |
|---|---|
| `[LuaPlugin]` | `LuaPluginActivity.cpp` — lifecycle, file loading, errors, input trace |
| `[LUA_VM]` | `LuaPluginVM.cpp` — VM init, memory cap, instruction limit, Lua error tracebacks |
| `[LUA_API]` | `LuaPluginAPI.cpp` — API registration, `sys.log()` |
| `[PluginBrowser]` | `PluginBrowserActivity.cpp` — scanning, launching |
| `[MAIN]` | `main.cpp` — silent restart target/caller logging |
| `[PLUGIN:<name>]` | Lua `sys.log()` / `plugin.log()` output (INFO level) |

### Error messages include stack tracebacks

Every load/callback error is written to serial **one line at a time** (so the
project's 256-byte log line never truncates the trace) with the exact line in
the plugin source, e.g.:

```
[12345] [ERR] [LUA_VM] Lua error:
[12345] [ERR] [LUA_VM]   callback onKey: /custom/hello_world.lua:15: attempt to index a nil value (global 'foo')
[12345] [ERR] [LUA_VM]   stack traceback:
[12345] [ERR] [LUA_VM]   	/custom/hello_world.lua:15: in function 'onKey'
```

The same message is shown on the device screen (up to 6 lines) and the plugin
then exits gracefully instead of spamming the loop.

### Common error messages

| Message | Cause | Fix |
|---|---|---|
| "Plugin file not found" | `.lua` file missing from `/custom/` | Verify the file is on the SD card |
| "Plugin too large" | File > 40 KB | Split the plugin or reduce data |
| "Not enough memory" | Free heap < 100 KB or maxAlloc < 75 KB | Reboot and try again; close other apps |
| "VM init failed" | Lua state allocation failed | Reboot; free heap by exiting other apps |
| "init() error:" | Lua runtime error in `init()` | Check the error message for the line number |
| "load error:" | Syntax error in the Lua script | Check Lua syntax; ensure valid UTF-8 |
| "instruction limit exceeded" | Infinite loop in a callback | Refactor to use `onKey()` frame pattern, not `while true` |

### Viewing logs

```powershell
# Flash and monitor
python -X utf8 -m platformio device monitor -p COM7 -b 115200
```

---

## 11. Included Example Plugins

| File | Name | Description |
|---|---|---|
| `plugins/hello_world.lua` | Hello World | Draws text and a rectangle; exits on any button press |
| `plugins/snake.lua` | Snake Game | Classic Snake game with high score persistence |
| `plugins/breakout.lua` | Breakout | Brick breaker with paddle, ball physics, win/lose |
| `plugins/sudoku_full.lua` | Sudoku Full | Full Sudoku puzzle generator + solver + UI |
| `plugins/sudoku.lua` | Sudoku | Minimal static Sudoku grid display |
| `plugins/todo_list.lua` | Todo List | Simple todo list with file persistence |

---

## 12. Architecture Diagram

```
                    ┌─────────────────────────────────┐
                    │          Boot / setup()         │
                    │  silentRebootTarget == 3 (PLUGIN)│
                    └────────────┬────────────────────┘
                                 │ snapshot
                                 ▼
                    ┌─────────────────────────────────┐
                    │ ActivityManager.goToPlugin()  │
                    │  creates LuaPluginActivity      │
                    └────────────┬────────────────────┘
                                 │ onEnter()
                                 ▼
              ┌──────────────────────────────────────┐
              │ LuaPluginActivity::onEnter()         │
              │  1. Check file exists (/custom/*.lua) │
              │  2. Read file (≤ 40KB buffer)          │
              │  3. checkMemoryAvailable()            │
              │  4. Set API context (renderer, input)  │
              │  5. vmInit() → lua_newstate()          │
              │  6. vmLoad() → luaL_loadbuffer+dispatch│
              │  7. vmRunMain() → call init()          │
              └────────────┬───────────────────────────┘
                           │
                           │ loop() every ~10ms
                           ▼
              ┌──────────────────────────────────────┐
              │ LuaPluginActivity::loop()            │
              │  1. input.update()                   │
              │  2. Check exit flag                  │
              │  3. vmCallCallback("onKey")          │
              │  4. delay(10)                        │
              └────────────┬───────────────────────────┘
                           │ onExit()
                           ▼
              ┌──────────────────────────────────────┐
              │ LuaPluginActivity::onExit()          │
              │  1. vmShutdown() → lua_close()        │
              │  2. silentRestartToPluginBrowser()    │
              │     or silentRestartToHome()           │
              │     or silentRestartToApps()           │
              │  3. ESP.restart()                      │
              └────────────┬───────────────────────────┘
                           │
                           ▼ (reboot)
              ┌──────────────────────────────────────┐
              │          Boot / setup()              │
              │  (return to PluginBrowser or Home)   │
              └──────────────────────────────────────┘
```

---

## 13. C++ Source Index

| File | Purpose |
|---|---|
| `src/LuaPluginVM.h` | VM configuration constants (`PLUGIN_MEM_CAP`, `MAX_INSTRUCTIONS_PER_CALLBACK`, `MAX_LUA_SOURCE_SIZE`), function declarations |
| `src/LuaPluginVM.cpp` | Custom `heap_caps_realloc` allocator, instruction-count hook, `vmInit/vmLoad/vmRunMain/vmCallCallback/vmHasFunction/vmShutdown`, memory checks |
| `src/LuaPluginAPI.cpp` | All Lua C bindings: `lcd.*` (19), `fs.*` (9), `input.*` (4), `sys.*` (8), `plugin_str.*` (6); `lua_plugin_register_libs()` |
| `src/LuaPluginActivity.h` | Activity class with `pluginName_`, `pluginPath_`, `returnToPluginBrowser_` members |
| `src/LuaPluginActivity.cpp` | Lifecycle: file loading, VM init, `init()`/`onKey()` dispatch, error screens, silent restart on exit |
| `src/activities/apps/PluginBrowserActivity.h` | Browser activity with `PluginEntry` struct |
| `src/activities/apps/PluginBrowserActivity.cpp` | SD card scanning, header parsing, list UI, launch via `silentRestartToPlugin()` |
| `src/SilentRestart.h` | `silentRestartToPluginBrowser()`, `silentRestartToPlugin()` declarations |
| `src/main.cpp` | RTC_NOINIT routing: `SILENT_REBOOT_TARGET_PLUGIN` (3), `SILENT_REBOOT_TARGET_PLUGIN_BROWSER` (4), snapshot + routing in `setup()` |
| `src/activities/ActivityManager.h/cpp` | `goToPlugin()`, `goToPluginBrowser()` methods |
| `lib/lua/src/` | Lua 5.4.7 source (patched `linit.c` for 8 standard libs, `lobject.h` assertion fixes, `llimits.h` stub asserts) |
| `plugins/*.lua` | Example plugin scripts |
