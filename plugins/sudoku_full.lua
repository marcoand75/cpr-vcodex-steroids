-- NAME: Sudoku Full
-- DESC: Classic Sudoku puzzle game
-- ICON: AppsHub

-- Game constants
local GRID_SIZE = 9
local BOX_SIZE = 3
local CELL_SIZE = 15
local MARGIN_TOP = 20
local MARGIN_LEFT = 10

-- Game state
local board = {}
local solution = {}
local fixed = {}
local selectedCell = {row = 0, col = 0}
local difficulty = 2  -- 1=easy, 2=medium, 3=hard
local gameOver = false
local gameWon = false

function init()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    
    generateNewGame()
    drawBoard()
end

function generateNewGame()
    -- Generate a complete valid Sudoku
    solution = generateCompleteSudoku()
    
    -- Create the puzzle by removing numbers based on difficulty
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
    
    -- Remove numbers based on difficulty
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
    gameOver = false
    gameWon = false
end

function generateCompleteSudoku()
    local sudoku = {}
    
    -- Initialize empty board
    for i = 1, GRID_SIZE do
        sudoku[i] = {}
        for j = 1, GRID_SIZE do
            sudoku[i][j] = 0
        end
    end
    
    -- Fill the diagonal boxes first (they are independent)
    fillDiagonalBoxes(sudoku)
    
    -- Fill remaining cells
    solveSudoku(sudoku)
    
    return sudoku
end

function fillDiagonalBoxes(sudoku)
    for box = 0, 2 do
        local startRow = box * BOX_SIZE + 1
        local startCol = box * BOX_SIZE + 1
        
        local numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9}
        shuffleTable(numbers)
        
        local idx = 1
        for i = 0, BOX_SIZE - 1 do
            for j = 0, BOX_SIZE - 1 do
                sudoku[startRow + i][startCol + j] = numbers[idx]
                idx = idx + 1
            end
        end
    end
end

function shuffleTable(tbl)
    for i = #tbl, 2, -1 do
        local j = sys.random(1, i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

function solveSudoku(sudoku)
    local emptyCell = findEmptyCell(sudoku)
    
    if not emptyCell then
        return true  -- Solved
    end
    
    local row, col = emptyCell.row, emptyCell.col
    
    for num = 1, 9 do
        if isValidPlacement(sudoku, row, col, num) then
            sudoku[row][col] = num
            
            if solveSudoku(sudoku) then
                return true
            end
            
            sudoku[row][col] = 0
        end
    end
    
    return false
end

function findEmptyCell(sudoku)
    for i = 1, GRID_SIZE do
        for j = 1, GRID_SIZE do
            if sudoku[i][j] == 0 then
                return {row = i, col = j}
            end
        end
    end
    return nil
end

function isValidPlacement(sudoku, row, col, num)
    -- Check row
    for j = 1, GRID_SIZE do
        if sudoku[row][j] == num then
            return false
        end
    end
    
    -- Check column
    for i = 1, GRID_SIZE do
        if sudoku[i][col] == num then
            return false
        end
    end
    
    -- Check 3x3 box
    local boxRow = math.floor((row - 1) / BOX_SIZE) * BOX_SIZE + 1
    local boxCol = math.floor((col - 1) / BOX_SIZE) * BOX_SIZE + 1
    
    for i = 0, BOX_SIZE - 1 do
        for j = 0, BOX_SIZE - 1 do
            if sudoku[boxRow + i][boxCol + j] == num then
                return false
            end
        end
    end
    
    return true
end

function drawBoard()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    
    -- Draw title
    lcd.drawCenteredText("SUDOKU", 5)
    
    -- Draw grid
    for i = 1, GRID_SIZE do
        for j = 1, GRID_SIZE do
            local x = MARGIN_LEFT + (j - 1) * CELL_SIZE
            local y = MARGIN_TOP + (i - 1) * CELL_SIZE
            
            -- Draw cell border
            local lineWidth = 1
            if i % BOX_SIZE == 1 or i == 1 then
                lineWidth = 2
            end
            if j % BOX_SIZE == 1 or j == 1 then
                lineWidth = 2
            end
            
            -- Highlight selected cell
            if i == selectedCell.row and j == selectedCell.col then
                lcd.fillRect(x - 1, y - 1, CELL_SIZE + 1, CELL_SIZE + 1)
                lcd.setTextColor(1)
            else
                lcd.drawRect(x, y, CELL_SIZE, CELL_SIZE, false)
            end
            
            -- Draw number
            if board[i][j] ~= 0 then
                local text = tostring(board[i][j])
                local textWidth = lcd.getTextWidth(text)
                local textX = x + (CELL_SIZE - textWidth) / 2
                local textY = y + (CELL_SIZE - lcd.getLineHeight()) / 2
                
                if fixed[i][j] then
                    lcd.setTextColor(0)
                else
                    lcd.setTextColor(0)
                end
                
                lcd.drawText(text, textX, textY)
            end
            
            lcd.setTextColor(0)
        end
    end
    
    -- Draw thick borders for 3x3 boxes
    for i = 0, 3 do
        local y = MARGIN_TOP + i * BOX_SIZE * CELL_SIZE
        lcd.drawLineH(MARGIN_LEFT - 1, y, GRID_SIZE * CELL_SIZE + 2)
    end
    
    for j = 0, 3 do
        local x = MARGIN_LEFT + j * BOX_SIZE * CELL_SIZE
        lcd.drawLineV(x, MARGIN_TOP - 1, GRID_SIZE * CELL_SIZE + 2)
    end
    
    -- Draw instructions
    lcd.setCursor(5, MARGIN_TOP + GRID_SIZE * CELL_SIZE + 10)
    lcd.print("Arrows: Move | OK: Input")
    lcd.setCursor(5, MARGIN_TOP + GRID_SIZE * CELL_SIZE + 25)
    lcd.print("1-9: Number | BACK: Exit")
    
    lcd.display()
end

function inputNumber(num)
    if not fixed[selectedCell.row][selectedCell.col] then
        board[selectedCell.row][selectedCell.col] = num
        
        -- Check if puzzle is solved
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

function showNumberInput()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    
    lcd.drawCenteredText("Select Number", 30)
    
    -- Draw number buttons
    for i = 1, 9 do
        local row = math.floor((i - 1) / 3)
        local col = (i - 1) % 3
        
        local x = 50 + col * 40
        local y = 60 + row * 40
        
        lcd.drawRect(x, y, 35, 35, false)
        lcd.drawCenteredText(tostring(i), y + 12)
    end
    
    -- Clear button
    lcd.drawRect(50, 180, 35, 35, false)
    lcd.drawCenteredText("CLR", 192)
    
    lcd.display()
end

function onKey()
    if input.wasPressed("back") then
        plugin.finish()
        return
    end
    
    if gameWon then
        if input.wasPressed("ok") then
            generateNewGame()
            drawBoard()
        end
        return
    end
    
    -- Handle movement
    if input.wasPressed("up") and selectedCell.row > 1 then
        selectedCell.row = selectedCell.row - 1
        drawBoard()
    elseif input.wasPressed("down") and selectedCell.row < GRID_SIZE then
        selectedCell.row = selectedCell.row + 1
        drawBoard()
    elseif input.wasPressed("left") and selectedCell.col > 1 then
        selectedCell.col = selectedCell.col - 1
        drawBoard()
    elseif input.wasPressed("right") and selectedCell.col < GRID_SIZE then
        selectedCell.col = selectedCell.col + 1
        drawBoard()
    end
    
    -- Handle number input
    if input.wasPressed("ok") then
        showNumberInput()
        
        -- Wait for number selection
        while true do
            if input.wasPressed("back") then
                drawBoard()
                break
            end
            
            for i = 1, 9 do
                if input.wasPressed(tostring(i)) then
                    inputNumber(i)
                    drawBoard()
                    break
                end
            end
            
            if input.wasPressed("ok") then
                -- Use OK to clear cell
                if not fixed[selectedCell.row][selectedCell.col] then
                    board[selectedCell.row][selectedCell.col] = 0
                    drawBoard()
                end
                break
            end
            
            sys.log("")
        end
    end
end

function finish()
    -- Cleanup if needed
end

-- No top-level loop: onKey() is called every frame by the activity loop.