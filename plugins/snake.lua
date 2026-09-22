-- ============================================================================
-- Snake Game — classic Snake on the e-ink display
-- ----------------------------------------------------------------------------
-- KEY E-INK DESIGN: the panel refresh blocks the loop for ~505 ms, so the
-- game logic runs on a FAST loop (every ~10 ms) and the panel is flushed only
-- every DISPLAY_INTERVAL_MS. This keeps input responsive (a held direction
-- registers within ~10 ms) and the snake moving at the intended speed, while
-- the display updates as fast as the panel physically allows.
-- ============================================================================

-- NAME: Snake Game
-- DESC: Classic Snake game for CPR-vCodex
-- ICON: AppsHub
-- RESTART: no

-- Game constants
local GRID_SIZE = 10
local INITIAL_SPEED = 200  -- milliseconds between moves
local MIN_SPEED = 80
local DISPLAY_INTERVAL_MS = 600  -- flush the e-ink panel at most this often

-- Delimited play area (score bar above it, visible border around it)
local AREA_X = 10
local AREA_Y = 40
local AREA_W = 460
local AREA_H = 740
local gridWidth = math.floor(AREA_W / GRID_SIZE)   -- 46 cells wide
local gridHeight = math.floor(AREA_H / GRID_SIZE)  -- 74 cells tall

-- Game state
local snake = {}
local direction = "right"
local nextDirection = "right"
local food = {x = 0, y = 0}
local score = 0
local highScore = 0
local gameOver = false
local lastMoveTime = 0
local gameStarted = false
local lastDisplayMs = 0

-- ---------------------------------------------------------------------------
-- Held-input latch. input.isPressed (level) registers reliably even when the
-- refresh blocks the loop; the latch fires once per hold. It self-primes on
-- the first call: any button still held from LAUNCHING the plugin (Confirm
-- in the browser) is absorbed so it cannot trigger an action on entry.
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

function toPixelX(gx) return AREA_X + gx * GRID_SIZE end
function toPixelY(gy) return AREA_Y + gy * GRID_SIZE end

-- Flush the panel at most every DISPLAY_INTERVAL_MS. The refresh blocks
-- ~505 ms; lastDisplayMs is reset AFTER the block so the fast input window is
-- a full DISPLAY_INTERVAL_MS long (not just the leftover before the next
-- flush), which keeps button presses responsive between refreshes.
function maybeFlush()
  local now = sys.getUptimeMs()
  if now - lastDisplayMs >= DISPLAY_INTERVAL_MS then
    lastDisplayMs = now
    lcd.display()                    -- blocks for the e-ink refresh
    lastDisplayMs = sys.getUptimeMs()  -- restart the window after the block
  end
end

function init()
    loadHighScore()
    resetGame()
    lastDisplayMs = sys.getUptimeMs()
    drawStartScreen()   -- flushes immediately (state transition)
end

function resetGame()
    -- Initialize snake in the center of the play area
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
    lastMoveTime = sys.getUptimeMs()

    spawnFood()
end

function spawnFood()
    local validPosition = false

    while not validPosition do
        food.x = sys.random(0, gridWidth - 1)
        food.y = sys.random(0, gridHeight - 1)

        validPosition = true
        for _, segment in ipairs(snake) do
            if segment.x == food.x and segment.y == food.y then
                validPosition = false
                break
            end
        end
    end
end

function drawBorder()
    lcd.drawRect(AREA_X, AREA_Y, AREA_W, AREA_H, false)
end

function drawStartScreen()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    drawBorder()

    lcd.drawCenteredText("SNAKE", 8)

    lcd.setCursor(20, 100)
    lcd.print("Arrows to steer")
    lcd.setCursor(20, 120)
    lcd.print("Hold OK to start")
    lcd.setCursor(20, 140)
    lcd.print("Hold BACK to exit")

    if highScore > 0 then
        lcd.setCursor(20, 180)
        lcd.print("High Score: " .. highScore)
    end

    lcd.display()  -- immediate: the user must see the start screen
end

-- Draws the play field to the framebuffer only (no flush here — see onKey).
function drawGame()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    drawBorder()

    for i, segment in ipairs(snake) do
        local x = toPixelX(segment.x)
        local y = toPixelY(segment.y)

        if i == 1 then
            lcd.fillRect(x, y, GRID_SIZE - 1, GRID_SIZE - 1)  -- head: solid
        else
            lcd.drawRect(x, y, GRID_SIZE - 1, GRID_SIZE - 1, false)  -- body: outline
        end
    end

    lcd.fillRect(toPixelX(food.x), toPixelY(food.y), GRID_SIZE - 1, GRID_SIZE - 1)

    lcd.setCursor(5, 5)
    lcd.print("Score: " .. score)
end

function drawGameOver()
    lcd.fillScreen(1)
    lcd.setTextColor(0)
    drawBorder()

    lcd.drawCenteredText("GAME OVER", 60)
    lcd.drawCenteredText("Score: " .. score, 90)

    if score > highScore then
        lcd.drawCenteredText("NEW HIGH SCORE!", 120)
        saveHighScore(score)
        highScore = score
    else
        lcd.drawCenteredText("High Score: " .. highScore, 120)
    end

    lcd.setCursor(30, 160)
    lcd.print("Hold OK to restart")
    lcd.setCursor(30, 180)
    lcd.print("Hold BACK to exit")

    lcd.display()  -- immediate: the user must see the game-over screen
end

function moveSnake()
    if gameOver or not gameStarted then
        return
    end

    direction = nextDirection

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

    -- Wall collision (confined to the delimited play area)
    if newX < 0 or newX >= gridWidth or newY < 0 or newY >= gridHeight then
        gameOver = true
        return
    end

    -- Self collision. The tail cell is EXCLUDED when not eating: it is about
    -- to be vacated, so moving into it is legal.
    local willEat = (newX == food.x and newY == food.y)
    local bodyLen = #snake
    local checkLen = willEat and bodyLen or (bodyLen - 1)
    for i = 1, checkLen do
        local seg = snake[i]
        if seg.x == newX and seg.y == newY then
            gameOver = true
            return
        end
    end

    table.insert(snake, 1, {x = newX, y = newY})

    if willEat then
        score = score + 10
        spawnFood()
    else
        table.remove(snake)
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
    -- Back SHORT press is delivered here (no action in this game). A LONG
    -- Back press (hold ~1.5 s) exits — handled by the activity.
    if gameOver then
        if pressed("ok") then
            resetGame()
            drawGame()
            lcd.display()  -- immediate: show the restarted field
        end
        return
    end

    if not gameStarted then
        if pressed("ok") then
            gameStarted = true
            drawGame()
            lcd.display()  -- immediate: show the game start
        end
        return
    end

    -- Direction changes (guard against reversing nextDirection, which would
    -- allow a 180 degree turn inside one move interval). Checked every frame
    -- on the fast loop, so controls respond immediately.
    if pressed("up") and nextDirection ~= "down" then
        nextDirection = "up"
    elseif pressed("down") and nextDirection ~= "up" then
        nextDirection = "down"
    elseif pressed("left") and nextDirection ~= "right" then
        nextDirection = "left"
    elseif pressed("right") and nextDirection ~= "left" then
        nextDirection = "right"
    end

    -- Movement on a millisecond timer (fast logic, independent of the display)
    local now = sys.getUptimeMs()
    local speed = math.max(MIN_SPEED, INITIAL_SPEED - (score / 2))

    if now - lastMoveTime >= speed then
        lastMoveTime = now
        moveSnake()

        if gameOver then
            drawGameOver()
        else
            drawGame()
        end
    end

    -- Periodic panel flush (the e-ink refresh blocks, so we pace it)
    maybeFlush()
end
