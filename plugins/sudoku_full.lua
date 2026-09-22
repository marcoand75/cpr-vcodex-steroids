-- ============================================================================
-- Sudoku Full — playable Sudoku with a generated puzzle
-- ----------------------------------------------------------------------------
-- Demonstrates: procedural puzzle generation by random cell removal from a
-- pre-validated solution (no runtime backtracking solver, so init() stays
-- well within the 40 KB VM heap cap), board navigation, a non-blocking number
-- pad (state machine driven from onKey — NO busy-wait loops, which would hit
-- the instruction limit), and a held-input latch with initial-state absorption
-- for reliable controls on slow e-ink refreshes.
-- ============================================================================

-- NAME: Sudoku Full
-- DESC: Classic Sudoku puzzle game
-- ICON: AppsHub
-- RESTART: no

-- ---------------------------------------------------------------------------
-- Board layout: 9x9 grid of 42px cells = 378px, centered on the 480px panel.
-- ---------------------------------------------------------------------------
local GRID_SIZE = 9
local BOX_SIZE = 3
local CELL_SIZE = 42
local GRID_W = GRID_SIZE * CELL_SIZE            -- 378
local MARGIN_LEFT = math.floor((480 - GRID_W) / 2)  -- 51
local MARGIN_TOP = 40

-- Valid pre-generated solution. The puzzle is created by removing cells at
-- runtime — no backtracking solver, keeping init() far under the 40 KB cap.
-- NOTE: random removal does not guarantee a unique solution; for a demo the
-- win check compares against this solution.
local BASE_SOLUTION = {
  {1, 2, 3, 4, 5, 6, 7, 8, 9},
  {4, 5, 6, 7, 8, 9, 1, 2, 3},
  {7, 8, 9, 1, 2, 3, 4, 5, 6},
  {2, 3, 1, 5, 6, 4, 8, 9, 7},
  {5, 6, 4, 8, 9, 7, 2, 3, 1},
  {8, 9, 7, 2, 3, 1, 5, 6, 4},
  {3, 1, 2, 6, 4, 5, 9, 7, 8},
  {6, 4, 5, 9, 7, 8, 3, 1, 2},
  {9, 7, 8, 3, 1, 2, 6, 4, 5},
}

-- Game state
local board = {}        -- player board (0 = empty)
local solution = {}     -- reference solution
local fixed = {}        -- true = given clue (not editable)
local selectedCell = {row = 1, col = 1}
local difficulty = 2    -- 1=easy, 2=medium, 3=hard (fixed for this demo)
local gameWon = false

-- Number-pad state (non-blocking, driven from onKey)
local numPadActive = false
local numPadSel = 1

-- Number-pad layout (3x3 buttons, centered)
local PAD_SIZE = 60
local PAD_GAP = 24
local PAD_START_X = math.floor((480 - (3 * PAD_SIZE + 2 * PAD_GAP)) / 2)  -- 126
local PAD_START_Y = 60
local PAD_STEP = PAD_SIZE + PAD_GAP                                        -- 84

-- ---------------------------------------------------------------------------
-- Held-input latch. E-ink refreshes at ~2 Hz, so short taps can be missed by
-- edge detection; using input.isPressed (level) makes every held command
-- register. The latch self-primes on the first call: any button still held
-- from LAUNCHING the plugin (e.g. Confirm in the browser) is absorbed so it
-- cannot trigger a plugin action on entry.
-- ---------------------------------------------------------------------------
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

  generateNewGame()
  drawBoard()
end

-- ---------------------------------------------------------------------------
-- Puzzle generation (random removal from the pre-validated solution)
-- ---------------------------------------------------------------------------
function generateNewGame()
  solution = BASE_SOLUTION

  board = {}
  fixed = {}

  for i = 1, GRID_SIZE do
    board[i] = {}
    fixed[i] = {}
    for j = 1, GRID_SIZE do
      board[i][j] = solution[i][j]
      fixed[i][j] = true
    end
  end

  local cellsToRemove
  if difficulty == 1 then
    cellsToRemove = 35  -- Easy
  elseif difficulty == 2 then
    cellsToRemove = 45  -- Medium
  else
    cellsToRemove = 55  -- Hard
  end

  local removed = 0
  while removed < cellsToRemove do
    local row = sys.random(1, GRID_SIZE)
    local col = sys.random(1, GRID_SIZE)

    if board[row][col] ~= 0 then
      board[row][col] = 0
      fixed[row][col] = false
      removed = removed + 1
    end
  end

  selectedCell = {row = 1, col = 1}
  gameWon = false
  numPadActive = false
end

-- ---------------------------------------------------------------------------
-- Drawing
-- ---------------------------------------------------------------------------
function drawBoard()
  lcd.fillScreen(1)
  lcd.setTextColor(0)

  lcd.drawCenteredText("SUDOKU", 5)

  -- Cells
  for i = 1, GRID_SIZE do
    for j = 1, GRID_SIZE do
      local x = MARGIN_LEFT + (j - 1) * CELL_SIZE
      local y = MARGIN_TOP + (i - 1) * CELL_SIZE
      local selected = (i == selectedCell.row and j == selectedCell.col)

      if selected then
        -- Highlight selected cell (filled black)
        lcd.fillRect(x - 1, y - 1, CELL_SIZE + 1, CELL_SIZE + 1)
      else
        lcd.drawRect(x, y, CELL_SIZE, CELL_SIZE, false)
      end

      -- Number (white inside the black selected cell, black otherwise)
      if board[i][j] ~= 0 then
        local text = tostring(board[i][j])
        local tw = lcd.getTextWidth(text)
        local textX = x + (CELL_SIZE - tw) / 2
        local textY = y + (CELL_SIZE - lcd.getLineHeight()) / 2

        lcd.setTextColor(selected and 1 or 0)
        lcd.drawText(text, textX, textY)
      end

      lcd.setTextColor(0)
    end
  end

  -- Thick 3x3 box borders
  for i = 0, 3 do
    local y = MARGIN_TOP + i * BOX_SIZE * CELL_SIZE
    lcd.drawLineH(MARGIN_LEFT - 1, y, GRID_W + 2)
  end
  for j = 0, 3 do
    local x = MARGIN_LEFT + j * BOX_SIZE * CELL_SIZE
    lcd.drawLineV(x, MARGIN_TOP - 1, GRID_W + 2)
  end

  -- Instructions (below the grid: grid bottom = MARGIN_TOP + GRID_W = 418)
  lcd.setCursor(5, MARGIN_TOP + GRID_W + 12)
  lcd.print("Hold Arrows: Move cell")
  lcd.setCursor(5, MARGIN_TOP + GRID_W + 27)
  lcd.print("Hold OK: Pick number")
  lcd.setCursor(5, MARGIN_TOP + GRID_W + 42)
  lcd.print("Hold Back: Exit")

  lcd.display()
end

-- Non-blocking number pad (redrawn from onKey; no busy-wait loops)
function drawNumberPad()
  lcd.fillScreen(1)
  lcd.setTextColor(0)

  lcd.drawCenteredText("Select Number", 12)
  lcd.drawCenteredText("Arrows move | OK picks | BACK cancel", 30)

  for i = 1, 9 do
    local row = math.floor((i - 1) / 3)
    local col = (i - 1) % 3
    local x = PAD_START_X + col * PAD_STEP
    local y = PAD_START_Y + row * PAD_STEP
    local selected = (i == numPadSel)

    if selected then
      lcd.fillRect(x, y, PAD_SIZE, PAD_SIZE)  -- black background
      lcd.setTextColor(1)                     -- white number
    else
      lcd.drawRect(x, y, PAD_SIZE, PAD_SIZE, false)
      lcd.setTextColor(0)                     -- black number
    end

    local text = tostring(i)
    local tw = lcd.getTextWidth(text)
    lcd.drawText(text, x + (PAD_SIZE - tw) / 2, y + (PAD_SIZE - lcd.getLineHeight()) / 2)
  end

  lcd.setTextColor(0)
  lcd.display()
end

-- ---------------------------------------------------------------------------
-- Game logic
-- ---------------------------------------------------------------------------
function inputNumber(num)
  local r = selectedCell.row
  local c = selectedCell.col
  if not fixed[r][c] then
    board[r][c] = num

    if checkWin() then
      gameWon = true
    end

    drawBoard()
  end
end

function checkWin()
  for i = 1, GRID_SIZE do
    for j = 1, GRID_SIZE do
      if board[i][j] ~= solution[i][j] then
        return false
      end
    end
  end
  return true
end

-- ---------------------------------------------------------------------------
-- Input loop (state machine: board ⇄ number pad)
-- ---------------------------------------------------------------------------
function onKey()
  -- Back SHORT press: cancel the number pad back to the board (a short Back
  -- on the board does nothing — the plugin can reuse it for its own flow).
  -- A LONG Back press (hold ~1.5 s) exits — handled by the activity.
  if pressed("back") then
    if numPadActive then
      numPadActive = false
      drawBoard()
    end
    return
  end

  if gameWon then
    if pressed("ok") then
      generateNewGame()
      drawBoard()
    end
    return
  end

  -- Number pad: navigate with arrows, OK picks, BACK cancels.
  -- NOTE: the pad is redrawn ONLY when something changes (selection move,
  -- open, pick). A per-frame redraw would block the loop ~505 ms on the e-ink
  -- refresh, so button taps would fall inside the block and get missed.
  if numPadActive then
    local row = math.floor((numPadSel - 1) / 3)
    local col = (numPadSel - 1) % 3
    local moved = false

    if pressed("up") and row > 0 then numPadSel = numPadSel - 3; moved = true end
    if pressed("down") and row < 2 then numPadSel = numPadSel + 3; moved = true end
    if pressed("left") and col > 0 then numPadSel = numPadSel - 1; moved = true end
    if pressed("right") and col < 2 then numPadSel = numPadSel + 1; moved = true end

    if pressed("ok") then
      numPadActive = false
      inputNumber(numPadSel)
    elseif moved then
      drawNumberPad()
    end
    return
  end

  -- Board navigation
  if pressed("up") and selectedCell.row > 1 then
    selectedCell.row = selectedCell.row - 1
    drawBoard()
  elseif pressed("down") and selectedCell.row < GRID_SIZE then
    selectedCell.row = selectedCell.row + 1
    drawBoard()
  elseif pressed("left") and selectedCell.col > 1 then
    selectedCell.col = selectedCell.col - 1
    drawBoard()
  elseif pressed("right") and selectedCell.col < GRID_SIZE then
    selectedCell.col = selectedCell.col + 1
    drawBoard()
  end

  -- Open the number pad for the selected (editable) cell
  if pressed("ok") then
    if not fixed[selectedCell.row][selectedCell.col] then
      numPadActive = true
      numPadSel = 1
      drawNumberPad()
    end
  end
end

function finish()
  -- Cleanup if needed
end

-- No top-level loop: onKey() is called every frame by the activity loop.
