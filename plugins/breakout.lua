-- NAME: Breakout
-- DESC: Classic brick breaker game
-- ICON: AppsHub

-- Game constants
local PADDLE_WIDTH = 40
local PADDLE_HEIGHT = 6
local BALL_SIZE = 4
local BRICK_ROWS = 5
local BRICK_COLS = 8
local BRICK_WIDTH = 18
local BRICK_HEIGHT = 10
local BRICK_PADDING = 2

-- Game state
local paddle = {x = 0, y = 0}
local ball = {x = 0, y = 0, dx = 0, dy = 0}
local bricks = {}
local score = 0
local lives = 3
local gameOver = false
local gameWon = false
local gameStarted = false
local level = 1

-- Screen dimensions
local screenWidth = lcd.getWidth()
local screenHeight = lcd.getHeight()

function init()
    resetGame()
    drawStartScreen()
end

function resetGame()
    -- Initialize paddle at bottom center
    paddle.x = (screenWidth - PADDLE_WIDTH) / 2
    paddle.y = screenHeight - 30
    
    -- Initialize ball
    resetBall()
    
    -- Create bricks
    createBricks()
    
    score = 0
    lives = 3
    level = 1
    gameOver = false
    gameWon = false
    gameStarted = false
end

function resetBall()
    ball.x = screenWidth / 2
    ball.y = screenHeight / 2
    ball.dx = 2 * (sys.random(0, 1) == 0 and 1 or -1)
    ball.dy = -2
end

function createBricks()
    bricks = {}
    
    local totalWidth = BRICK_COLS * (BRICK_WIDTH + BRICK_PADDING)
    local startX = (screenWidth - totalWidth) / 2
    local startY = 40
    
    -- Colors for each row
    local colors = {1, 1, 1, 1, 1}  -- All white for simplicity
    
    for row = 1, BRICK_ROWS do
        bricks[row] = {}
        for col = 1, BRICK_COLS do
            bricks[row][col] = {
                x = startX + (col - 1) * (BRICK_WIDTH + BRICK_PADDING),
                y = startY + (row - 1) * (BRICK_HEIGHT + BRICK_PADDING),
                active = true,
                color = colors[row]
            }
        end
    end
end

function drawStartScreen()
    lcd.fillScreen(0)
    lcd.setTextColor(1)
    
    lcd.drawCenteredText("BREAKOUT", 50)
    
    lcd.setCursor(30, 100)
    lcd.print("Use LEFT/RIGHT to move")
    lcd.setCursor(30, 120)
    lcd.print("OK to launch ball")
    lcd.setCursor(30, 140)
    lcd.print("BACK to exit")
    
    lcd.display()
end

function drawGame()
    lcd.fillScreen(0)
    lcd.setTextColor(1)
    
    -- Draw paddle
    lcd.fillRect(paddle.x, paddle.y, PADDLE_WIDTH, PADDLE_HEIGHT)
    
    -- Draw ball
    lcd.fillCircle(ball.x, ball.y, BALL_SIZE, true)
    
    -- Draw bricks
    for row = 1, BRICK_ROWS do
        for col = 1, BRICK_COLS do
            local brick = bricks[row][col]
            if brick.active then
                lcd.fillRect(brick.x, brick.y, BRICK_WIDTH, BRICK_HEIGHT)
            end
        end
    end
    
    -- Draw UI
    lcd.setCursor(5, 5)
    lcd.print("Score: " .. score)
    lcd.setCursor(screenWidth - 60, 5)
    lcd.print("Lives: " .. lives)
    
    lcd.display()
end

function drawGameOver()
    lcd.fillScreen(0)
    lcd.setTextColor(1)
    
    if gameWon then
        lcd.drawCenteredText("YOU WIN!", 60)
    else
        lcd.drawCenteredText("GAME OVER", 60)
    end
    
    lcd.drawCenteredText("Score: " .. score, 90)
    lcd.drawCenteredText("Level: " .. level, 110)
    
    lcd.setCursor(30, 150)
    lcd.print("OK to restart")
    lcd.setCursor(30, 170)
    lcd.print("BACK to exit")
    
    lcd.display()
end

function updateBall()
    if not gameStarted or gameOver then
        return
    end
    
    -- Move ball
    ball.x = ball.x + ball.dx
    ball.y = ball.y + ball.dy
    
    -- Wall collisions
    if ball.x <= BALL_SIZE or ball.x >= screenWidth - BALL_SIZE then
        ball.dx = -ball.dx
        ball.x = math.max(BALL_SIZE, math.min(screenWidth - BALL_SIZE, ball.x))
    end
    
    if ball.y <= BALL_SIZE then
        ball.dy = -ball.dy
        ball.y = BALL_SIZE
    end
    
    -- Bottom collision (lose life)
    if ball.y >= screenHeight - BALL_SIZE then
        lives = lives - 1
        if lives <= 0 then
            gameOver = true
        else
            resetBall()
            gameStarted = false
        end
        return
    end
    
    -- Paddle collision
    if ball.y >= paddle.y - BALL_SIZE and 
       ball.y <= paddle.y + PADDLE_HEIGHT and
       ball.x >= paddle.x and 
       ball.x <= paddle.x + PADDLE_WIDTH then
        
        ball.dy = -math.abs(ball.dy)
        
        -- Add some angle based on where it hit the paddle
        local hitPos = (ball.x - paddle.x) / PADDLE_WIDTH
        ball.dx = 4 * (hitPos - 0.5)
        
        ball.y = paddle.y - BALL_SIZE
    end
    
    -- Brick collisions
    for row = 1, BRICK_ROWS do
        for col = 1, BRICK_COLS do
            local brick = bricks[row][col]
            if brick.active then
                if ball.x >= brick.x and 
                   ball.x <= brick.x + BRICK_WIDTH and
                   ball.y >= brick.y and 
                   ball.y <= brick.y + BRICK_HEIGHT then
                    
                    brick.active = false
                    score = score + 10
                    ball.dy = -ball.dy
                    
                    -- Check win condition
                    if checkWin() then
                        gameWon = true
                        gameOver = true
                    end
                    
                    return
                end
            end
        end
    end
end

function checkWin()
    for row = 1, BRICK_ROWS do
        for col = 1, BRICK_COLS do
            if bricks[row][col].active then
                return false
            end
        end
    end
    return true
end

function onKey()
    if input.wasPressed("back") then
        plugin.finish()
        return
    end

    if gameOver then
        if input.wasPressed("ok") then
            resetGame()
            drawStartScreen()
        end
        return
    end

    -- Paddle movement (continuous while held)
    if input.isPressed("left") and paddle.x > 0 then
        paddle.x = paddle.x - 3
    end
    if input.isPressed("right") and paddle.x < screenWidth - PADDLE_WIDTH then
        paddle.x = paddle.x + 3
    end

    -- Launch ball
    if not gameStarted and input.wasPressed("ok") then
        gameStarted = true
    end

    -- Game update
    if gameStarted and not gameOver then
        updateBall()
        drawGame()
    end
end

function finish()
    -- Cleanup if needed
end