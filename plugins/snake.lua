-- NAME: Snake Game
-- DESC: Classic Snake game for CPR-vCodex
-- ICON: AppsHub

-- Game constants
local GRID_SIZE = 10
local INITIAL_SPEED = 200  -- milliseconds between moves
local MIN_SPEED = 80

-- Game state
local snake = {}
local direction = "right"
local nextDirection = "right"
local food = {x = 0, y = 0}
local score = 0
local highScore = 0
local gameOver = false
lastMoveTime = 0
gameStarted = false

-- Get screen dimensions
local screenWidth = lcd.getWidth()
local screenHeight = lcd.getHeight()
local gridWidth = math.floor(screenWidth / GRID_SIZE)
local gridHeight = math.floor(screenHeight / GRID_SIZE)

function init()
    -- Load high score
    loadHighScore()
    
    -- Initialize game
    resetGame()
    
    -- Draw initial screen
    drawStartScreen()
end

function resetGame()
    -- Initialize snake in the center
    local startX = math.floor(gridWidth / 2)
    local startY = math.floor(gridHeight / 2)
    
    snake = {
        {x = startX, y = startY},
        {x = startX - 1, y = startY},
        {x = startX - 2, y = startY}
    }
    
    direction = "right"
    nextDirection = "right"
    score = 0
    gameOver = false
    gameStarted = false
    lastMoveTime = sys.getTime() * 1000
    
    spawnFood()
end

function spawnFood()
    local validPosition = false
    
    while not validPosition do
        food.x = sys.random(0, gridWidth - 1)
        food.y = sys.random(0, gridHeight - 1)
        
        -- Make sure food doesn't spawn on snake
        validPosition = true
        for _, segment in ipairs(snake) do
            if segment.x == food.x and segment.y == food.y then
                validPosition = false
                break
            end
        end
    end
end

function drawStartScreen()
    lcd.fillScreen(0)
    lcd.setTextColor(1)
    
    -- Title
    lcd.drawCenteredText("SNAKE", 50)
    
    -- Instructions
    lcd.setCursor(20, 100)
    lcd.print("Use arrow keys to move")
    lcd.setCursor(20, 120)
    lcd.print("OK to start")
    lcd.setCursor(20, 140)
    lcd.print("BACK to exit")
    
    -- High score
    if highScore > 0 then
        lcd.setCursor(20, 180)
        lcd.print("High Score: " .. highScore)
    end
    
    lcd.display()
end

function drawGame()
    lcd.fillScreen(0)
    
    -- Draw snake
    lcd.setTextColor(1)
    for i, segment in ipairs(snake) do
        local x = segment.x * GRID_SIZE
        local y = segment.y * GRID_SIZE
        
        if i == 1 then
            -- Head is brighter
            lcd.fillRect(x, y, GRID_SIZE - 1, GRID_SIZE - 1)
        else
            lcd.drawRect(x, y, GRID_SIZE - 1, GRID_SIZE - 1, true)
        end
    end
    
    -- Draw food
    lcd.fillRect(food.x * GRID_SIZE, food.y * GRID_SIZE, GRID_SIZE - 1, GRID_SIZE - 1)
    
    -- Draw score
    lcd.setTextColor(1)
    lcd.setCursor(5, 5)
    lcd.print("Score: " .. score)
    
    lcd.display()
end

function drawGameOver()
    lcd.fillScreen(0)
    lcd.setTextColor(1)
    
    lcd.drawCenteredText("GAME OVER", 60)
    lcd.drawCenteredText("Score: " .. score, 90)
    
    if score > highScore then
        lcd.drawCenteredText("NEW HIGH SCORE!", 120)
        saveHighScore(score)
    else
        lcd.drawCenteredText("High Score: " .. highScore, 120)
    end
    
    lcd.setCursor(30, 160)
    lcd.print("OK to restart")
    lcd.setCursor(30, 180)
    lcd.print("BACK to exit")
    
    lcd.display()
end

function moveSnake()
    if gameOver or not gameStarted then
        return
    end
    
    -- Update direction
    direction = nextDirection
    
    -- Calculate new head position
    local head = snake[1]
    local newX = head.x
    local newY = head.y
    
    if direction == "up" then
        newY = newY - 1
    elseif direction == "down" then
        newY = newY + 1
    elseif direction == "left" then
        newX = newX - 1
    elseif direction == "right" then
        newX = newX + 1
    end
    
    -- Check wall collision
    if newX < 0 or newX >= gridWidth or newY < 0 or newY >= gridHeight then
        gameOver = true
        return
    end
    
    -- Check self collision
    for _, segment in ipairs(snake) do
        if segment.x == newX and segment.y == newY then
            gameOver = true
            return
        end
    end
    
    -- Add new head
    table.insert(snake, 1, {x = newX, y = newY})
    
    -- Check if food eaten
    if newX == food.x and newY == food.y then
        score = score + 10
        spawnFood()
    else
        -- Remove tail
        table.remove(snake)
    end
end

function onKey()
    if input.wasPressed("back") then
        plugin.finish()
        return
    end
    
    if gameOver then
        if input.wasPressed("ok") then
            resetGame()
            drawGame()
        end
        return
    end
    
    if not gameStarted then
        if input.wasPressed("ok") then
            gameStarted = true
            drawGame()
        end
        return
    end
    
    -- Handle direction changes
    if input.wasPressed("up") and direction ~= "down" then
        nextDirection = "up"
    elseif input.wasPressed("down") and direction ~= "up" then
        nextDirection = "down"
    elseif input.wasPressed("left") and direction ~= "right" then
        nextDirection = "left"
    elseif input.wasPressed("right") and direction ~= "left" then
        nextDirection = "right"
    end
end

function finish()
    -- Cleanup if needed
end

function loadHighScore()
    if fs.exists("highscore.txt") then
        local content = fs.readFile("highscore.txt")
        if content then
            highScore = tonumber(content) or 0
        end
    end
end

function saveHighScore(newScore)
    fs.writeFile("highscore.txt", tostring(newScore))
end

function onKey()
    if input.wasPressed("back") then
        plugin.finish()
        return
    end

    if gameOver then
        if input.wasPressed("ok") then
            resetGame()
            drawGame()
        end
        return
    end

    if not gameStarted then
        if input.wasPressed("ok") then
            gameStarted = true
            drawGame()
        end
        return
    end

    -- Handle direction changes
    if input.wasPressed("up") and direction ~= "down" then
        nextDirection = "up"
    elseif input.wasPressed("down") and direction ~= "up" then
        nextDirection = "down"
    elseif input.wasPressed("left") and direction ~= "right" then
        nextDirection = "left"
    elseif input.wasPressed("right") and direction ~= "left" then
        nextDirection = "right"
    end

    -- Game update: move snake on a timer
    local currentTime = sys.getTime() * 1000
    local speed = math.max(MIN_SPEED, INITIAL_SPEED - (score / 2))

    if currentTime - lastMoveTime >= speed then
        moveSnake()
        lastMoveTime = currentTime

        if gameOver then
            drawGameOver()
        else
            drawGame()
        end
    end
end