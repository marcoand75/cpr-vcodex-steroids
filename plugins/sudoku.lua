-- NAME: Sudoku
-- DESC: Minimal Sudoku puzzle display
-- ICON: AppsHub

local W = lcd.getWidth()
local grid = {
  {5, 3, 0, 0, 7, 0, 0, 0, 0},
  {6, 0, 0, 1, 9, 5, 0, 0, 0},
  {0, 9, 8, 0, 0, 0, 0, 6, 0},
  {8, 0, 0, 0, 6, 0, 0, 0, 3},
  {4, 0, 0, 8, 0, 3, 0, 0, 1},
  {7, 0, 0, 0, 2, 0, 0, 0, 6},
  {0, 6, 0, 0, 0, 0, 2, 8, 0},
  {0, 0, 0, 4, 1, 9, 0, 0, 5},
  {0, 0, 0, 0, 8, 0, 0, 7, 9},
}

function init()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawText("Sudoku", (W - lcd.getTextWidth("Sudoku")) / 2, 20)

  local cellSize = 30
  local gridX = 20
  local gridY = 80

   -- Draw grid background (filled black)
  lcd.fillRect(gridX, gridY, 9 * cellSize, 9 * cellSize)

  -- Draw grid lines (thicker for 3x3 boxes)
  for i = 0, 9 do
    local x = gridX + i * cellSize
    local y = gridY + i * cellSize
    if i % 3 == 0 then
      lcd.drawLineV(x, gridY, 9 * cellSize)
      lcd.drawLineH(gridX, y, 9 * cellSize)
    else
      -- Draw thin lines with offset for visibility
      lcd.drawPixel(x, gridY, true)
      lcd.drawPixel(x, gridY + 9 * cellSize, true)
      lcd.drawPixel(gridX, y, true)
      lcd.drawPixel(gridX + 9 * cellSize, y, true)
    end
  end

  -- Draw numbers
  for row = 1, 9 do
    for col = 1, 9 do
      local val = grid[row][col]
      if val > 0 then
        local x = gridX + (col - 1) * cellSize + 10
        local y = gridY + (row - 1) * cellSize + 10
        lcd.drawText(tostring(val), x, y)
      end
    end
  end

  lcd.display()
end

function onKey()
  sys.finish()
end
