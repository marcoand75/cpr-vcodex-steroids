# CPR-vCodex Steroids Lua Plugins

This directory contains example Lua plugins for the CPR-vCodex Steroids
Lua plugin VM system.

## Included examples

| Plugin | What it demonstrates |
|---|---|
| `hello_world.lua` | Minimal plugin template (headers, `init`/`onKey`, exit) |
| `snake.lua` | Game loop, delimited play area, held-input latch, high score on SD |
| `breakout.lua` | Time-based physics, sub-stepped collisions, e-ink fast-loop pattern |
| `sudoku_full.lua` | Pre-generated puzzle, non-blocking state-machine number pad |
| `todo_list.lua` | Sandboxed file persistence, wrapped multi-line text |
| `doom_like.lua` | First-person maze **raycaster** (Lode's DDA, BSD-2-Clause), **4:3 viewport** (160×120 cells, 3×3 px) with **precomputed dithered wall textures** (brick/stone/door) world-anchored with distance shading, top panel with a **2D map + HUD**, black status bar, precomputed **demon sprites** (horns/eyes/mouth), persistent muzzle flash, **batch rendering** and **external data**: optional `/custom/doom_like_data/map.txt` level, persisted `highscore.txt` |

The `doom_like_data/` folder contains the example `map.txt` (copy it to
`/custom/doom_like_data/map.txt` on the SD card to override the built-in
level). Map format: `#` brick, `S` stone, `D` metal door, `.` floor, `P`
player start, `E` demon spawn.

Copy the `.lua` files to `/custom/` on the SD card and open **Apps → Plugins**.

## Requirements

- A plugin is a single `.lua` file placed in `/custom/` on the SD card.
- The script is loaded and executed in a sandboxed Lua 5.4.7 VM with a
  64 KB memory cap.
- File I/O is restricted to `/custom/<plugin_name>_data/`.

## Plugin Lifecycle

1. **`init()`** — Called once after the script is loaded. Draw the initial
   screen and call `lcd.display()` when done.
2. **`onKey()`** — Called every ~10 ms. Check `input.wasPressed()` /
   `input.isPressed()` inside this function.
3. **`finish()`** — Called on **every exit path** (after `sys.finish()`, a
   long-press Back, or a load error). Use it to persist state.

**Back button has two roles** (few buttons on the device):
- **Short press** → delivered to `onKey()` first — the plugin decides what to
  do with it (e.g. cancel a sub-screen). Call `sys.finish()` to exit on it.
- **Long press (hold ≥ 1.5 s)** → always exits the plugin.

Exit: `sys.finish()` / `plugin.finish()`, or a long-press Back. The VM then
shuts down and the device silently restarts (unless the plugin declares
`-- RESTART: no`, in which case it returns to the browser with no reboot).

> **E-ink note:** the display refresh blocks the loop ~505 ms, so a quick tap
> can fall between two input samples and be missed. Prefer `input.isPressed()`
> plus a "held latch" (see the games in this folder) for reliable controls.
>
> **Game pattern:** run the game logic on the fast loop (~10 ms) and flush the
> panel only every `DISPLAY_INTERVAL_MS` via `maybeFlush()` (reset the timer
> *after* `lcd.display()` so the fast input window is a full interval long).
> This keeps controls responsive while the panel updates as fast as e-ink
> physically allows.

## API Reference

### `lcd.*` — Drawing

| Function | Description |
|---|---|
| `lcd.fillScreen(color)` | Clear screen (0=black, 1=white) |
| `lcd.clear()` | Clear to white |
| `lcd.display()` | Flush framebuffer to display |
| `lcd.setTextColor(color)` | 0=black text, 1=white text |
| `lcd.setCursor(x, y)` | Set text cursor position |
| `lcd.print(text)` | Draw text at current cursor |
| `lcd.drawText(text, x, y)` | Draw text at (x, y) |
| `lcd.drawCenteredText(text, y)` | Centered text at y |
| `lcd.drawWrappedText(text, x, y, maxWidth, maxLines?)` | Word-wrapped multi-line text (ellipsis on excess) — prevents overflow |
| `lcd.drawRect(x, y, w, h, filled?, color?)` | Draw/fill rectangle (color: 0=black default, 1=white) |
| `lcd.fillRect(x, y, w, h, color?)` | Fill rectangle |
| `lcd.drawLine(x1, y1, x2, y2, color?)` | Draw line |
| `lcd.drawLineH(x, y, w, color?)` | Horizontal line |
| `lcd.drawLineV(x, y, h, color?)` | Vertical line |
| `lcd.drawCircle(cx, cy, r, filled?, color?)` | Draw/fill circle |
| `lcd.fillCircle(cx, cy, r, color?)` | Fill circle |
| `lcd.fillScreenColor(color)` | Alias for fillScreen |
| `lcd.getWidth()` | Screen width in pixels |
| `lcd.getHeight()` | Screen height in pixels |
| `lcd.getTextWidth(text)` | Text width in pixels |
| `lcd.getLineHeight()` | Line height in pixels |

### `fs.*` — File I/O (sandboxed)

| Function | Description |
|---|---|
| `fs.readFile(filename)` | Read entire file as string |
| `fs.writeFile(filename, content)` | Write/overwrite file |
| `fs.appendFile(filename, content)` | Append to file |
| `fs.exists(filename)` | Check if file exists |
| `fs.listDir(subdir)` | List files in subdirectory |
| `fs.remove(filename)` | Delete file |
| `fs.mkdir(dirname)` | Create subdirectory |
| `fs.rename(old, new)` | Rename file |
| `fs.getDataDir()` | Get plugin data directory path |

### `input.*` — Input

| Function | Description |
|---|---|
| `input.isPressed(name)` | Is button currently held? |
| `input.wasPressed(name)` | Was button pressed this frame? |
| `input.waitButton()` | Block until any button pressed |
| `input.getHeldTime()` | How long button has been held |

Button names: `"back"`, `"ok"`, `"left"`, `"right"`, `"up"`, `"down"`, `"power"`.

### `sys.*` — System

| Function | Description |
|---|---|
| `sys.getTime()` | Unix timestamp (seconds) |
| `sys.getUptimeMs()` | Milliseconds since boot — millisecond clock for timers |
| `sys.getBattery()` | Battery percentage (0-100) |
| `sys.getBatteryVoltage()` | Battery voltage in mV |
| `sys.getSetting(key)` | Get a reader setting |
| `sys.log(msg)` | Log message |
| `sys.finish()` | Exit the plugin |
| `sys.random(min, max)` | Random integer |
| `sys.getDisplayWidth()` | Display width |

### `plugin.*` — Plugin helpers

| Function | Description |
|---|---|
| `plugin.finish()` | Exit the plugin |
| `plugin.log(msg)` | Log message |

### `plugin_str.*` — String utilities

| Function | Description |
|---|---|
| `plugin_str.wrapText(text, width, maxLines)` | Word-wrap text |
| `plugin_str.truncate(text, width)` | Truncate to width |
| `plugin_str.len(text)` | String length |
| `plugin_str.upper(text)` | Uppercase |
| `plugin_str.lower(text)` | Lowercase |
| `plugin_str.format(fmt, ...)` | Minimal string.format |

## Plugin Headers

Place these at the top of your `.lua` file:

```lua
-- NAME: My Plugin
-- DESC: A brief description
-- ICON: AppsHub
-- RESTART: no
```

`-- RESTART: yes` (default) launches the plugin with a silent fast reboot.
`-- RESTART: no` runs it in-process (no reboot on launch or exit) — much
faster to iterate during development. `sys.log()` / `plugin.log()` output
appears on the serial console tagged `[PLUGIN:<plugin_name>]`, and Lua errors
are logged with a line-numbered stack traceback.
