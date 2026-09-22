-- ============================================================================
-- Breakout — classic brick breaker
-- ----------------------------------------------------------------------------
-- Demonstrates: time-based physics, sub-stepped collision (no tunnelling),
-- a held-input latch with initial-state absorption (the button used to launch
-- the plugin cannot trigger an action on entry), a delimited play area, and
-- the e-ink game pattern: fast logic loop + periodic panel flush.
-- ============================================================================

-- NAME: Breakout
-- DESC: Classic brick breaker game
-- ICON: AppsHub
-- RESTART: no

-- ---------------------------------------------------------------------------
-- Play area (480 x 800 panel): a bordered box with the score bar above it.
-- All gameplay coordinates are confined inside this box.
-- ---------------------------------------------------------------------------
local AREA_X = 10
local AREA_Y = 40
local AREA_W = 460
local AREA_H = 740

-- Game constants
local PADDLE_WIDTH = 40
local PADDLE_HEIGHT = 6
local BALL_SIZE = 4
local BRICK_ROWS = 5
local BRICK_COLS = 8
local BRICK_WIDTH = 18
local BRICK_HEIGHT = 10
local BRICK_PADDING = 2

-- Time-based speeds (px per second), so gameplay speed is independent of the
-- slow e-ink refresh.
local PADDLE_SPEED = 130
local BALL_SPEED = 70
local DISPLAY_INTERVAL_MS = 600  -- flush the e-ink panel at most this often

-- Derived positions inside the play area
local WALL_LEFT = AREA_X + BALL_SIZE            -- 14
local WALL_RIGHT = AREA_X + AREA_W - BALL_SIZE  -- 466
local WALL_TOP = AREA_Y + BALL_SIZE             -- 44
local LOSE_LINE = AREA_Y + AREA_H - BALL_SIZE   -- 776
local PADDLE_X_MIN = AREA_X                     -- 10
local PADDLE_X_MAX = AREA_X + AREA_W - PADDLE_WIDTH  -- 430
local PADDLE_Y = AREA_Y + AREA_H - PADDLE_HEIGHT - 8  -- 766

-- Game state
local paddle = {x = 0, y = PADDLE_Y}
local ball = {x = 0, y = 0, dx = 0, dy = 0}
local bricks = {}
local score = 0
local lives = 3
local gameOver = false
local gameWon = false
local gameStarted = false
local level = 1
local lastFrameMs = 0
local lastDisplayMs = 0

-- Screen dimensions
local screenWidth = lcd.getWidth()

-- ---------------------------------------------------------------------------
-- Held-input latch. input.isPressed (level) registers reliably even when the
-- refresh blocks the loop; the latch fires once per hold. Self-primes on the
-- first call so the button held to LAUNCH the plugin cannot trigger an action.
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
  resetGame()
  lastDisplayMs = sys.getUptimeMs()
  drawStartScreen()
  lcd.display()  -- immediate: show the start screen
end

function resetGame()
  paddle.x = (AREA_X + (AREA_W - PADDLE_WIDTH) / 2)
  resetBall()            -- ball resting on the paddle until launch
  createBricks()
  score = 0
  lives = 3
  level = 1
  gameOver = false
  gameWon = false
  gameStarted = false
  lastFrameMs = sys.getUptimeMs()
end

function resetBall()
  ball.x = paddle.x + PADDLE_WIDTH / 2
  ball.y = PADDLE_Y - BALL_SIZE
  ball.dx = BALL_SPEED * (sys.random(0, 1) == 0 and 1 or -1)
  ball.dy = -BALL_SPEED
end

function createBricks()
  bricks = {}

  local totalWidth = BRICK_COLS * (BRICK_WIDTH + BRICK_PADDING)
  local startX = AREA_X + (AREA_W - totalWidth) / 2  -- centered in the area
  local startY = AREA_Y + 10

  for row = 1, BRICK_ROWS do
    bricks[row] = {}
    for col = 1, BRICK_COLS do
      bricks[row][col] = {
        x = startX + (col - 1) * (BRICK_WIDTH + BRICK_PADDING),
        y = startY + (row - 1) * (BRICK_HEIGHT + BRICK_PADDING),
        active = true
      }
    end
  end
end

-- ---------------------------------------------------------------------------
-- Drawing — black shapes on a white background (buffer only, no flush)
-- ---------------------------------------------------------------------------
function drawBorder()
  lcd.drawRect(AREA_X, AREA_Y, AREA_W, AREA_H, false)
end

function drawBricks()
  for row = 1, BRICK_ROWS do
    for col = 1, BRICK_COLS do
      local brick = bricks[row][col]
      if brick.active then
        lcd.fillRect(brick.x, brick.y, BRICK_WIDTH, BRICK_HEIGHT)
      end
    end
  end
end

function drawScene()
  lcd.fillRect(paddle.x, paddle.y, PADDLE_WIDTH, PADDLE_HEIGHT)
  lcd.fillCircle(ball.x, ball.y, BALL_SIZE)
  drawBricks()
end

function drawStartScreen()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  drawBorder()
  drawScene()

  lcd.setCursor(30, 100)
  lcd.print("Hold LEFT/RIGHT to move")
  lcd.setCursor(30, 120)
  lcd.print("Hold OK to launch")
  lcd.setCursor(30, 140)
  lcd.print("Hold BACK to exit")
end

function drawGame()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  drawBorder()
  drawScene()

  lcd.setCursor(5, 5)
  lcd.print("Score: " .. score)
  lcd.setCursor(screenWidth - 60, 5)
  lcd.print("Lives: " .. lives)
end

function drawGameOver()
  lcd.fillScreen(1)
  lcd.setTextColor(0)

  if gameWon then
    lcd.drawCenteredText("YOU WIN!", 60)
  else
    lcd.drawCenteredText("GAME OVER", 60)
  end

  lcd.drawCenteredText("Score: " .. score, 90)
  lcd.drawCenteredText("Level: " .. level, 110)

  lcd.setCursor(30, 150)
  lcd.print("Hold OK to restart")
  lcd.setCursor(30, 170)
  lcd.print("Hold BACK to exit")

  lcd.display()  -- immediate: the user must see the game-over screen
end

-- ---------------------------------------------------------------------------
-- Physics — time-based, sub-stepped so the ball never tunnels through the
-- paddle or a brick, even at slow frame rates.
-- ---------------------------------------------------------------------------
function updateBall(dt)
  if not gameStarted or gameOver then
    return
  end

  local distX = math.abs(ball.dx * dt)
  local distY = math.abs(ball.dy * dt)
  local steps = math.max(1, math.ceil(math.max(distX, distY) / 5))
  local stepDt = dt / steps

  for s = 1, steps do
    local prevY = ball.y
    ball.x = ball.x + ball.dx * stepDt
    ball.y = ball.y + ball.dy * stepDt

    -- Side walls
    if ball.x <= WALL_LEFT then
      ball.x = WALL_LEFT
      ball.dx = math.abs(ball.dx)
    end
    if ball.x >= WALL_RIGHT then
      ball.x = WALL_RIGHT
      ball.dx = -math.abs(ball.dx)
    end

    -- Top wall
    if ball.y <= WALL_TOP then
      ball.y = WALL_TOP
      ball.dy = math.abs(ball.dy)
    end

    -- Paddle collision (crossing the paddle top while moving down)
    if ball.dy > 0 and prevY <= paddle.y and ball.y >= paddle.y and
       ball.x >= paddle.x - BALL_SIZE and ball.x <= paddle.x + PADDLE_WIDTH + BALL_SIZE then

      local hitPos = (ball.x - paddle.x) / PADDLE_WIDTH
      ball.dx = 2.5 * BALL_SPEED * (hitPos - 0.5)  -- angle from hit position
      ball.dy = -math.abs(ball.dy)
      ball.y = paddle.y - BALL_SIZE
    end

    -- Bottom: lost a life
    if ball.y >= LOSE_LINE then
      lives = lives - 1
      if lives <= 0 then
        gameOver = true
      else
        resetBall()
        gameStarted = false
      end
      return
    end

    -- Brick collisions (box inflated by the ball radius)
    for row = 1, BRICK_ROWS do
      for col = 1, BRICK_COLS do
        local brick = bricks[row][col]
        if brick.active then
          if ball.x + BALL_SIZE >= brick.x and ball.x - BALL_SIZE <= brick.x + BRICK_WIDTH and
             ball.y + BALL_SIZE >= brick.y and ball.y - BALL_SIZE <= brick.y + BRICK_HEIGHT then

            brick.active = false
            score = score + 10
            ball.dy = -ball.dy

            if checkWin() then
              gameWon = true
              gameOver = true
            end
            break
          end
        end
      end
      if gameOver then break end
    end
    if gameOver then return end
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

-- ---------------------------------------------------------------------------
-- Input loop (fast logic, periodic panel flush)
-- ---------------------------------------------------------------------------
function onKey()
  -- Back SHORT press is delivered here (no action in this game). A LONG
  -- Back press (hold ~1.5 s) exits — handled by the activity.
  local now = sys.getUptimeMs()
  local dt = (now - lastFrameMs) / 1000
  lastFrameMs = now
  if dt < 0 then dt = 0 end
  if dt > 0.5 then dt = 0.5 end  -- clamp huge jumps (e.g. after a flush)

  if gameOver then
    if pressed("ok") then
      resetGame()
      drawStartScreen()
      lcd.display()  -- immediate
    end
    return
  end

  -- Paddle movement (continuous while held, time-based, clamped to the area)
  local dir = 0
  if input.isPressed("left") then dir = dir - 1 end
  if input.isPressed("right") then dir = dir + 1 end
  if dir ~= 0 then
    paddle.x = paddle.x + dir * PADDLE_SPEED * dt
    if paddle.x < PADDLE_X_MIN then paddle.x = PADDLE_X_MIN end
    if paddle.x > PADDLE_X_MAX then paddle.x = PADDLE_X_MAX end
  end

  -- Start screen: show paddle movement (periodic flush), launch on OK
  if not gameStarted then
    if dir ~= 0 then
      drawStartScreen()
      maybeFlush()
    end
    if pressed("ok") then
      gameStarted = true
      drawGame()
      lcd.display()  -- immediate: show the game start
    end
    return
  end

  -- Game update: fast physics loop, panel flushed periodically
  if gameStarted and not gameOver then
    updateBall(dt)
    drawGame()
    maybeFlush()
  end
end

function finish()
  -- Cleanup if needed
end
