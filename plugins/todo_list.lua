-- NAME: Todo List
-- DESC: Simple todo list manager with file persistence
-- ICON: File

local todos = {}
local editing = false
local editBuffer = ""

local W = lcd.getWidth()

function init()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawText("Todo List", 20, 20)

  -- Load saved todos
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
  for i, todo in ipairs(todos) do
    local marker = "[ ] "
    lcd.drawText(marker .. todo, 20, y)
    y = y + 30
  end

  lcd.drawLine(20, y, W - 20, y)
  lcd.drawText("+" .. " Add   -" .. " Del   Back=Exit", 20, y + 20)
  lcd.display()
end

function save()
  local content = table.concat(todos, "\n")
  fs.writeFile("todos.txt", content)
end

function onKey()
  -- In a full implementation, this would parse button names
  -- and support add/delete. This is a simplified demo.
  save()
  sys.finish()
end
