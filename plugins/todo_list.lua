-- ============================================================================
-- Todo List — simple todo manager with file persistence
-- ----------------------------------------------------------------------------
-- Demonstrates: sandboxed file I/O (fs.*), persistent storage across runs,
-- wrapped multi-line text drawing, and the held-input latch for reliable
-- controls on slow e-ink refreshes (~2 Hz).
--
-- Data is stored in the plugin's sandbox dir: /custom/todo_list_data/todos.txt
-- ============================================================================

-- NAME: Todo List
-- DESC: Simple todo list manager with file persistence
-- ICON: File
-- RESTART: no

local todos = {}
local W = lcd.getWidth()
local MAX_VISIBLE = 30  -- only the last N items are drawn

-- Held-input latch. A button held for at least one input sample (~10 ms with
-- the fast loop) fires exactly once per hold. It self-primes on the first
-- call: any button still held from LAUNCHING the plugin (Confirm in the
-- browser) is absorbed, so it can never trigger an action on entry.
local held
function pressed(name)
  if held == nil then
    held = {
      back = input.isPressed("back"),
      ok = input.isPressed("ok"),
      up = input.isPressed("up"),
      down = input.isPressed("down"),
      left = input.isPressed("left"),
      right = input.isPressed("right"),
    }
  end
  local now = input.isPressed(name)
  local was = held[name]
  held[name] = now
  return now and not was
end

function init()
  lcd.fillScreen(1)
  lcd.setTextColor(0)

  -- Load saved todos from the plugin's sandbox directory
  local saved = fs.readFile("todos.txt")
  if saved then
    for line in saved:gsub("\r", ""):gmatch("([^\n]+)") do
      table.insert(todos, line)
    end
  end

  renderList()
end

function renderList()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawText("Todo List", 20, 20)

  local y = 60
  local start = math.max(1, #todos - MAX_VISIBLE + 1)
  if #todos == 0 then
    lcd.drawText("No todos yet — hold OK to add one", 20, y)
    y = y + lcd.getLineHeight() + 6
  else
    for i = start, #todos do
      -- Wrapped text so long items never overflow the screen edge
      lcd.drawWrappedText("[ ] " .. todos[i], 20, y, W - 40, 1)
      y = y + lcd.getLineHeight() + 6
    end
  end

  lcd.drawLine(20, y, W - 20, y)
  lcd.drawText("OK=Add  Left=Del  Hold Back=Exit", 20, y + 20)
  lcd.display()
end

function save()
  -- Persist immediately on every change (the long-press Back exit is handled
  -- by the activity, so we never rely on a save-on-exit hook).
  local content = table.concat(todos, "\n")
  fs.writeFile("todos.txt", content)
end

function onKey()
  -- Back SHORT press is reserved for the plugin (nothing to do here); a LONG
  -- Back press (hold ~1.5 s) exits — handled by the activity.
  if pressed("ok") then
    table.insert(todos, "New todo #" .. #todos)
    save()
    renderList()
    return
  end

  if pressed("left") and #todos > 0 then
    table.remove(todos)
    save()
    renderList()
  end
end
