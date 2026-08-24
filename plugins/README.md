# CPR-vCodex Steroids Lua Plugins

This directory contains example Lua plugins for the CPR-vCodex Steroids
Lua plugin VM system.

## Requirements

- A plugin is a single `.lua` file placed in `/custom/` on the SD card.
- The script is loaded and executed in a sandboxed Lua 5.4.7 VM with a
  40 KB memory cap.
- File I/O is restricted to `/custom/<plugin_name>_data/`.

## Plugin Lifecycle

1. **`init()`** — Called once after the script is loaded. Draw the initial
   screen and call `lcd.display()` when done.
2. **`onKey()`** — Called on every button press. Check `input.wasPressed()`
   inside this function.
3. **`finish()`** — When the plugin calls `sys.finish()` or the user holds
   BACK, the VM shuts down and the device silently restarts.

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
| `lcd.drawRect(x, y, w, h, filled)` | Draw/fill rectangle |
| `lcd.fillRect(x, y, w, h)` | Fill rectangle |
| `lcd.drawLine(x1, y1, x2, y2)` | Draw line |
| `lcd.drawLineH(x, y, w)` | Horizontal line |
| `lcd.drawLineV(x, y, h)` | Vertical line |
| `lcd.drawCircle(cx, cy, r, filled)` | Draw/fill circle |
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
| `sys.getTime()` | Unix timestamp |
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
```
