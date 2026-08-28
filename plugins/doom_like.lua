-- ============================================================================
-- Doom-Like — a first-person raycaster for the e-ink display (v4)
-- ----------------------------------------------------------------------------
-- A textured, dithered Wolfenstein-style raycaster tuned for the CPR-vCodex
-- Lua plugin constraints:
--   • single .lua file, sandboxed VM (80 KB heap cap — ~53 KB available at
--     runtime is used for PRECOMPUTED wall textures and sprite data)
--   • 6 buttons: left/right turn, up/down move, OK shoot, Back pause/menu
--   • e-ink refresh ~2 Hz: the game LOGIC runs on a fast loop; the SCENE is
--     rendered in small BATCHES across frames (from a camera snapshot), so the
--     instruction budget is never exceeded and texturing can be rich.
--   • a fixed 4:3 VIEWPORT (480x360, 160x120 render cells = 3x3 px) with the
--     2D MAP + HUD on a top panel and a status bar below — nothing inside the
--     view is wasted, so the higher density reads as much finer walls.
--
-- EXTERNAL DATA (optional): if /custom/doom_like_data/map.txt exists it is
-- used as the level. Format: one text row per line;
--   '#' = brick wall   'S' = stone wall   'D' = metal door
--   '.' = floor        'P' = player start (first one wins)
--   'E' = demon spawn
-- Rows may be shorter than the longest row (missing cells become walls).
-- Best score is stored in /custom/doom_like_data/highscore.txt.
--
-- The raycasting/DDA algorithm is derived from Lode Vandevenne's raycasting
-- tutorial (https://lodev.org/cgtutor/raycasting.html), distributed under the
-- 2-clause BSD license. The license notice must be retained:
--
--   Copyright (c) 2004-2019, Lode Vandevenne. All rights reserved.
--   Redistribution and use in source and binary forms, with or without
--   modification, are permitted provided that the following conditions are
--   met:
--   1. Redistributions of source code must retain the above copyright
--      notice, this list of conditions and the following disclaimer.
--   2. Redistributions in binary form must reproduce the above copyright
--      notice, this list of conditions and the following disclaimer in the
--      documentation and/or other materials provided with the distribution.
--
-- The rest of this file (map, textures, game logic, sprites, UI) is original
-- example code for CPR-vCodex Steroids.
-- ============================================================================

-- NAME: Doom-Like
-- DESC: First-person maze raycaster (Wolfenstein-style)
-- ICON: AppsHub
-- RESTART: yes

-- ---------------------------------------------------------------------------
-- 4:3 viewport (480 x 360) rendered at 160 x 120 cells (3 x 3 px each).
-- Top panel = 2D map + HUD; bottom panel = status bar.
-- ---------------------------------------------------------------------------
local RENDER_W = 160
local RENDER_H = 120
local VIEW_X = 0
local VIEW_Y = 220
local VIEW_W = 480
local VIEW_H = 360
local SCALE_X = VIEW_W / RENDER_W   -- 3 px per column
local SCALE_Y = VIEW_H / RENDER_H   -- 3 px per row
local HALF_H = RENDER_H / 2         -- 60 (horizon in render rows)
local BATCH = 20                    -- columns rendered per fast-loop frame

-- Game feel (time-based, independent of the slow e-ink refresh)
local MOVE_SPEED = 5.0     -- cells per second
local ROT_SPEED = 2.6      -- radians per second
local SHOOT_COOLDOWN_MS = 300

-- Wall types (numbers stored in the map)
local T_FLOOR, T_BRICK, T_STONE, T_DOOR = 0, 1, 2, 3

-- ---------------------------------------------------------------------------
-- PRECOMPUTED WALL TEXTURES (16x16 tiles, 1 = black, 0 = white mortar).
-- Built once at init; indexing them at render time is fast and lets us use
-- the available heap for much richer patterns than flat columns.
-- ---------------------------------------------------------------------------
local TEX_W, TEX_H = 16, 16
local TEX = {}  -- TEX[wallType] = flat table of 256 texels

local function buildBrickTile(tx, ty)
  if ty % 4 == 3 then return 0 end                       -- horizontal mortar
  local off = (math.floor(ty / 4) % 2) * 8               -- staggered courses
  if (tx + off) % 8 == 7 then return 0 end               -- vertical mortar
  return 1
end
local function buildStoneTile(tx, ty)
  if ty % 8 == 7 then return 0 end                       -- coarse horizontal
  local off = (math.floor(ty / 8) % 2) * 8
  if (tx + off) % 16 == 15 then return 0 end             -- coarse vertical
  return 1
end
local function buildDoorTile(tx, ty)
  if tx % 4 == 3 then return 0 end                       -- vertical seams
  if ty % 16 == 15 then return 0 end                     -- bottom seam
  return 1
end

local function buildTextures()
  TEX[T_BRICK] = {}
  TEX[T_STONE] = {}
  TEX[T_DOOR] = {}
  for ty = 0, TEX_H - 1 do
    for tx = 0, TEX_W - 1 do
      local idx = ty * TEX_W + tx + 1
      TEX[T_BRICK][idx] = buildBrickTile(tx, ty)
      TEX[T_STONE][idx] = buildStoneTile(tx, ty)
      TEX[T_DOOR][idx] = buildDoorTile(tx, ty)
    end
  end
end

-- ---------------------------------------------------------------------------
-- PRECOMPUTED DEMON SPRITE (8 x 12). 'X' = black body, 'E' = white eye,
-- '.' = transparent. Scaled to the projected sprite box at render time.
-- ---------------------------------------------------------------------------
local DEMON_SPRITE = {
  ".X....X.",
  ".XXXXXX.",
  ".XEEXEEX.",
  ".XXXXXX.",
  "..XXXX..",
  ".XXXXXX.",
  "X.XXXX.X",
  "X.X..X.X",
  "X.XXXX.X",
  ".XXXXXX.",
  ".X.XX.X.",
  ".X.XX.X.",
}
local SPR_W, SPR_H = 8, 12

-- ---------------------------------------------------------------------------
-- Map: MAP[y][x] = 0 floor / 1 brick / 2 stone / 3 door
-- ---------------------------------------------------------------------------
local MAP = {}
local MAP_W, MAP_H = 24, 16
local PLAYER_START = { x = 2.5, y = 2.5 }
local ENEMY_SPAWNS = {}

function buildDefaultMap()
  MAP_W, MAP_H = 24, 16
  MAP = {}
  for y = 1, MAP_H do
    MAP[y] = {}
    for x = 1, MAP_W do
      MAP[y][x] = (x == 1 or x == MAP_W or y == 1 or y == MAP_H) and T_BRICK or T_FLOOR
    end
  end
  local function pil(x, y, w, h)
    for yy = y, y + h - 1 do
      for xx = x, x + w - 1 do
        MAP[yy][xx] = T_BRICK
      end
    end
  end
  pil(9, 4, 1, 1)
  pil(13, 7, 2, 2)
  pil(19, 4, 1, 1)
  pil(6, 11, 1, 1)
  pil(17, 12, 2, 1)
  pil(20, 9, 1, 1)
  MAP[4][12], MAP[5][12] = T_STONE, T_STONE
  MAP[12][10] = T_DOOR

  PLAYER_START = { x = 2.5, y = 2.5 }
  ENEMY_SPAWNS = { {7.5, 2.5}, {16.5, 8.5}, {5.5, 13.5}, {20.5, 5.5} }
end

function parseMap(content)
  MAP, ENEMY_SPAWNS = {}, {}
  PLAYER_START = { x = 2.5, y = 2.5 }
  local rows = {}
  for line in content:gsub("\r", ""):gmatch("([^\n]+)") do
    if #line > 0 then rows[#rows + 1] = line end
  end
  MAP_H = #rows
  MAP_W = 0
  for _, r in ipairs(rows) do
    if #r > MAP_W then MAP_W = #r end
  end
  if MAP_H == 0 then buildDefaultMap() return end

  local function setCell(y, x, ch)
    if ch == "#" then
      MAP[y][x] = T_BRICK
    elseif ch == "S" then
      MAP[y][x] = T_STONE
    elseif ch == "D" then
      MAP[y][x] = T_DOOR
    elseif ch == "P" then
      MAP[y][x] = T_FLOOR
      PLAYER_START.x, PLAYER_START.y = x + 0.5, y + 0.5
    elseif ch == "E" or ch == "B" then
      MAP[y][x] = T_FLOOR
      ENEMY_SPAWNS[#ENEMY_SPAWNS + 1] = { x + 0.5, y + 0.5 }
    else
      MAP[y][x] = T_FLOOR
    end
  end
  for y = 1, MAP_H do
    MAP[y] = {}
    local r = rows[y]
    for x = 1, MAP_W do
      setCell(y, x, x <= #r and r:sub(x, x) or "#")
    end
  end
  for y = 1, MAP_H do
    for x = 1, MAP_W do
      if (x == 1 or x == MAP_W or y == 1 or y == MAP_H) and MAP[y][x] == T_FLOOR then
        MAP[y][x] = T_BRICK
      end
    end
  end
  if #ENEMY_SPAWNS == 0 then ENEMY_SPAWNS = { {7.5, 2.5} } end
end

function loadMap()
  local content = fs.readFile("map.txt")
  if content and #content > 0 then
    parseMap(content)
    return true
  end
  buildDefaultMap()
  return false
end

-- ---------------------------------------------------------------------------
-- Game state
-- ---------------------------------------------------------------------------
local posX, posY
local dirX, dirY
local planeX, planeY
local health = 100
local score = 0
local highScore = 0
local state = "title"     -- title | play | over | win
local paused = false
local enemies = {}
local lastFrameMs = 0
local lastShotMs = 0
local dirty = false
local flashTimer = 0      -- muzzle flash persistence (seconds)
local job = nil           -- render job (camera snapshot + column progress)

-- ---------------------------------------------------------------------------
-- Held-input latch (fires once per hold) + launch-press absorption
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

-- ---------------------------------------------------------------------------
-- Map helpers
-- ---------------------------------------------------------------------------
-- Returns TRUE when the cell is a WALL (or out of bounds).
function isWall(px, py)
  local mx, my = math.floor(px), math.floor(py)
  local row = MAP[my]
  return not row or row[mx] ~= T_FLOOR
end

function enemiesAlive()
  local n = 0
  for _, e in ipairs(enemies) do
    if e.alive then n = n + 1 end
  end
  return n
end

-- ---------------------------------------------------------------------------
-- High score persistence
-- ---------------------------------------------------------------------------
function loadHighScore()
  local c = fs.readFile("highscore.txt")
  if c then highScore = tonumber(c) or 0 end
end
function saveHighScore()
  if score > highScore then
    highScore = score
    fs.writeFile("highscore.txt", tostring(highScore))
  end
end

-- ---------------------------------------------------------------------------
-- Game setup
-- ---------------------------------------------------------------------------
function startGame()
  posX, posY = PLAYER_START.x, PLAYER_START.y
  dirX, dirY = 1, 0
  planeX, planeY = 0, 0.66
  health = 100
  score = 0
  state = "play"
  paused = false
  enemies = {}
  for _, s in ipairs(ENEMY_SPAWNS) do
    enemies[#enemies + 1] = { x = s[1], y = s[2], alive = true, hitTimer = 2.5 }
  end
  lastFrameMs = sys.getUptimeMs()
  lastShotMs = 0
  flashTimer = 0
  dirty = true
end

-- ---------------------------------------------------------------------------
-- Player movement / rotation (time-based, wall collision)
-- ---------------------------------------------------------------------------
function movePlayer(dt)
  local rot = 0
  if input.isPressed("left") then rot = rot - 1 end
  if input.isPressed("right") then rot = rot + 1 end
  if rot ~= 0 then
    local a = rot * ROT_SPEED * dt
    local c, s = math.cos(a), math.sin(a)
    local odx, ody, opx, opy = dirX, dirY, planeX, planeY
    dirX = odx * c - ody * s
    dirY = odx * s + ody * c
    planeX = opx * c - opy * s
    planeY = opx * s + opy * c
    dirty = true
  end

  local fwd = 0
  if input.isPressed("up") then fwd = fwd + 1 end
  if input.isPressed("down") then fwd = fwd - 1 end
  if fwd ~= 0 then
    local step = fwd * MOVE_SPEED * dt
    local nx, ny = posX + dirX * step, posY + dirY * step
    if not isWall(nx, posY) then posX = nx end
    if not isWall(posX, ny) then posY = ny end
    dirty = true
  end
end

-- ---------------------------------------------------------------------------
-- Shooting — hitscan ray in the facing direction (range ~12 cells)
-- ---------------------------------------------------------------------------
function shoot()
  local now = sys.getUptimeMs()
  if now - lastShotMs < SHOOT_COOLDOWN_MS then return end
  lastShotMs = now
  flashTimer = 1.5   -- long enough to survive the ~600 ms render latency

  local px, py = posX, posY
  for _ = 1, 96 do
    px = px + dirX * 0.125
    py = py + dirY * 0.125
    if isWall(px, py) then break end
    for _, e in ipairs(enemies) do
      if e.alive and math.abs(px - e.x) < 0.45 and math.abs(py - e.y) < 0.45 then
        e.alive = false
        score = score + 100
        dirty = true
        return
      end
    end
  end
  dirty = true
end

-- ---------------------------------------------------------------------------
-- Enemy AI — chase the player faster, melee damage on a timer
-- ---------------------------------------------------------------------------
function updateEnemies(dt)
  for _, e in ipairs(enemies) do
    if e.alive then
      e.hitTimer = e.hitTimer - dt
      local dx, dy = posX - e.x, posY - e.y
      local dist = math.sqrt(dx * dx + dy * dy)
      if dist < 1.1 then
        if e.hitTimer <= 0 then
          e.hitTimer = 2.5
          health = health - 10
          dirty = true
        end
      elseif dist > 0.01 then
        local step = 2.5 * dt   -- faster chase so movement is clearly visible
        local nx = e.x + (dx / dist) * step
        local ny = e.y + (dy / dist) * step
        if not isWall(nx, ny) then
          e.x, e.y = nx, ny
          dirty = true
        end
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- Background inside the viewport: horizon line + receding hatched floor
-- ---------------------------------------------------------------------------
function drawFloorCeiling()
  local horizon = VIEW_Y + math.floor(HALF_H * SCALE_Y)
  lcd.drawLineH(VIEW_X, horizon, VIEW_W)
  local y = horizon + 24
  local step = 24
  while y < VIEW_Y + VIEW_H do
    lcd.drawLineH(VIEW_X, y, VIEW_W)
    y = y + step
    step = step + 16
  end
end

-- ---------------------------------------------------------------------------
-- Textured wall column using the precomputed tile, world-anchored (texX from
-- the exact hit point, so textures do not slide when the player moves).
-- Distance stipple lightens far walls. Black bands are drawn with fillRect
-- (white = mortar gaps).
-- ---------------------------------------------------------------------------
function renderWallColumn(x, drawStart, drawEnd, d, wtype, texX)
  local sx = x * SCALE_X
  local wallRows = drawEnd - drawStart
  local tex = TEX[wtype] or TEX[T_BRICK]
  local stipple = d >= 4
  local tx = texX % TEX_W

  local bandStart = drawStart
  local row = drawStart
  while row <= drawEnd do
    local rel = row - drawStart
    local ty = math.floor(rel / wallRows * TEX_H)
    if ty >= TEX_H then ty = TEX_H - 1 end
    local texel = tex[ty * TEX_W + tx + 1]
    local white = (texel == 0)
    if not white and stipple and (rel + x) % 2 == 0 then white = true end
    if white then
      if row > bandStart then
        lcd.fillRect(sx, VIEW_Y + bandStart * SCALE_Y, SCALE_X, (row - bandStart) * SCALE_Y)
      end
      bandStart = row + 1
    end
    row = row + 1
  end
  if drawEnd >= bandStart then
    lcd.fillRect(sx, VIEW_Y + bandStart * SCALE_Y, SCALE_X, (drawEnd - bandStart + 1) * SCALE_Y)
  end
end

-- ---------------------------------------------------------------------------
-- Demons: billboard sprites from the job snapshot, back-to-front, z-buffered,
-- scaled from the precomputed sprite (body + eyes + horns).
-- ---------------------------------------------------------------------------
function renderSprites(job)
  local list = {}
  for _, ep in ipairs(job.ep) do
    if ep.alive then
      local dx, dy = ep.x - job.px, ep.y - job.py
      local invDet = 1 / (job.pxv * job.dy - job.dx * job.pyv)
      local tx = invDet * (job.dy * dx - job.dx * dy)
      local ty = invDet * (-job.pyv * dx + job.pxv * dy)
      if ty > 0.1 then list[#list + 1] = { tx = tx, ty = ty, ep = ep } end
    end
  end
  table.sort(list, function(a, b) return a.ty > b.ty end)

  for _, it in ipairs(list) do
    local tx, ty, ep = it.tx, it.ty, it.ep
    local sX = math.floor((RENDER_W / 2) * (1 + tx / ty))
    local sH = math.floor(math.abs(RENDER_H / ty))
    local y0 = math.floor(-sH / 2 + HALF_H)
    local x0 = math.floor(-sH / 2 + sX)
    local x1 = math.floor(sH / 2 + sX)

    local c0, c1 = math.max(0, x0), math.min(RENDER_W - 1, x1)
    if c1 >= c0 and y0 + sH > 0 then
      local minZ = 1e9
      for cx = c0, c1 do
        if job.zbuf[cx] < minZ then minZ = job.zbuf[cx] end
      end
      if ty < minZ then
        local yy0, yy1 = math.max(0, y0), math.min(RENDER_H - 1, y0 + sH)
        if yy1 > yy0 then
          local bx = c0 * SCALE_X
          local by = VIEW_Y + yy0 * SCALE_Y
          local bw = (c1 - c0 + 1) * SCALE_X
          local bh = (yy1 - yy0) * SCALE_Y
          local sw = bw / SPR_W
          local sh = bh / SPR_H
          -- Draw the black body cells
          for r = 0, SPR_H - 1 do
            local line = DEMON_SPRITE[r + 1]
            for c = 0, SPR_W - 1 do
              local ch = line:sub(c + 1, c + 1)
              if ch == "X" then
                lcd.fillRect(bx + c * sw, by + r * sh, sw, sh)
              end
            end
          end
          -- White eyes (always visible against any background)
          for r = 0, SPR_H - 1 do
            local line = DEMON_SPRITE[r + 1]
            for c = 0, SPR_W - 1 do
              if line:sub(c + 1, c + 1) == "E" then
                local ex = bx + c * sw
                local ey = by + r * sh
                for dy = 0, 1 do
                  for dx = 0, 2 do
                    lcd.drawPixel(ex + dx, ey + dy, false)
                  end
                end
              end
            end
          end
        end
      end
    end
  end
end

function drawCrosshair()
  local cx, cy = 240, VIEW_Y + HALF_H * SCALE_Y
  lcd.fillRect(cx - 1, cy - 8, 2, 16)
  lcd.fillRect(cx - 8, cy - 1, 16, 2)
  if flashTimer > 0 then
    -- Bold muzzle flash: a large white starburst + black core, visible against
    -- both dark targets and the white background, for several renders.
    for dy = -10, 10, 2 do
      lcd.drawPixel(cx + dy, cy, false)
      lcd.drawPixel(cx, cy + dy, false)
      lcd.drawPixel(cx + dy * 0.7, cy + dy * 0.7, false)
      lcd.drawPixel(cx - dy * 0.7, cy + dy * 0.7, false)
    end
    lcd.fillRect(cx - 3, cy - 3, 6, 6)  -- black core
    lcd.drawRect(cx - 5, cy - 5, 10, 10, false)  -- white ring
  end
end

-- ---------------------------------------------------------------------------
-- Top panel: the 2D MAP (with player facing + enemies) and HUD text
-- ---------------------------------------------------------------------------
function drawMapAndHud()
  local mm = 10
  local mx, my = 10, 20
  -- map border
  lcd.drawRect(mx - 2, my - 2, MAP_W * mm + 4, MAP_H * mm + 4, false)
  for y = 1, MAP_H do
    for x = 1, MAP_W do
      if MAP[y][x] ~= T_FLOOR then
        lcd.fillRect(mx + (x - 1) * mm, my + (y - 1) * mm, mm, mm)
      end
    end
  end
  -- enemies
  for _, e in ipairs(enemies) do
    if e.alive then
      lcd.fillRect(mx + (math.floor(e.x) - 1) * mm - 2, my + (math.floor(e.y) - 1) * mm - 2, 4, 4)
    end
  end
  -- player (ring) + facing direction line
  local ppx = mx + (math.floor(posX) - 1) * mm + mm / 2
  local ppy = my + (math.floor(posY) - 1) * mm + mm / 2
  lcd.fillRect(ppx - 4, ppy - 4, 8, 8)
  lcd.drawRect(ppx - 1, ppy - 1, 2, 2, false)
  lcd.drawLine(ppx, ppy, ppx + dirX * 10, ppy + dirY * 10)

  -- HUD text on the right side of the top panel
  lcd.drawText("HP " .. health, 280, 20)
  lcd.drawText("SCORE " .. score, 280, 44)
  lcd.drawText("DEMONS " .. enemiesAlive(), 280, 68)
  lcd.drawText("BEST " .. highScore, 280, 92)
end

-- ---------------------------------------------------------------------------
-- Bottom panel: black status bar
-- ---------------------------------------------------------------------------
function drawStatusBar()
  lcd.fillRect(0, VIEW_Y + VIEW_H, 480, 220)
  lcd.setTextColor(1)
  lcd.drawCenteredText("L/R turn   U/D move   OK shoot   Back pause", VIEW_Y + VIEW_H + 20)
  lcd.drawCenteredText("Hold Back: exit", VIEW_Y + VIEW_H + 44)
  lcd.setTextColor(0)
end

-- ---------------------------------------------------------------------------
-- Render job: rendered in small batches across frames (from a snapshot)
-- ---------------------------------------------------------------------------
function startRenderJob()
  job = {
    px = posX, py = posY,
    dx = dirX, dy = dirY,
    pxv = planeX, pyv = planeY,
    col = 0,
    zbuf = {},
    ep = {},
  }
  for i, e in ipairs(enemies) do
    job.ep[i] = { x = e.x, y = e.y, alive = e.alive }
  end
  dirty = false
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  drawFloorCeiling()
end

function renderJobBatch(job)
  local last = math.min(RENDER_W, job.col + BATCH)
  for x = job.col, last - 1 do
    local cameraX = 2 * x / RENDER_W - 1
    local rayDirX = job.dx + job.pxv * cameraX
    local rayDirY = job.dy + job.pyv * cameraX

    local mapX, mapY = math.floor(job.px), math.floor(job.py)
    local deltaDistX = (rayDirX == 0) and 1e30 or math.abs(1 / rayDirX)
    local deltaDistY = (rayDirY == 0) and 1e30 or math.abs(1 / rayDirY)

    local stepX, stepY, sideDistX, sideDistY
    if rayDirX < 0 then
      stepX, sideDistX = -1, (job.px - mapX) * deltaDistX
    else
      stepX, sideDistX = 1, (mapX + 1 - job.px) * deltaDistX
    end
    if rayDirY < 0 then
      stepY, sideDistY = -1, (job.py - mapY) * deltaDistY
    else
      stepY, sideDistY = 1, (mapY + 1 - job.py) * deltaDistY
    end

    local side, hit, steps, wtype = 0, false, 0, T_BRICK
    while not hit and steps < 48 do
      steps = steps + 1
      if sideDistX < sideDistY then
        sideDistX = sideDistX + deltaDistX
        mapX = mapX + stepX
        side = 0
      else
        sideDistY = sideDistY + deltaDistY
        mapY = mapY + stepY
        side = 1
      end
      local cell = MAP[mapY] and MAP[mapY][mapX] or T_BRICK
      if cell ~= T_FLOOR then hit = true; wtype = cell end
    end

    local perpWallDist
    if side == 0 then perpWallDist = sideDistX - deltaDistX
    else perpWallDist = sideDistY - deltaDistY end
    if perpWallDist < 0.05 then perpWallDist = 0.05 end
    job.zbuf[x] = perpWallDist

    local lineHeight = math.floor(RENDER_H / perpWallDist)
    if lineHeight > RENDER_H * 4 then lineHeight = RENDER_H * 4 end
    local drawStart = math.floor(-lineHeight / 2 + HALF_H)
    local drawEnd = math.floor(lineHeight / 2 + HALF_H)
    if drawStart < 0 then drawStart = 0 end
    if drawEnd >= RENDER_H then drawEnd = RENDER_H - 1 end

    if drawEnd > drawStart then
      -- world-anchored texture column (lodev): wallX from the hit point
      local wallX
      if side == 0 then wallX = job.py + perpWallDist * rayDirY
      else wallX = job.px + perpWallDist * rayDirX end
      wallX = wallX - math.floor(wallX)
      local texX = math.floor(wallX * TEX_W)
      if (side == 0 and rayDirX > 0) or (side == 1 and rayDirY < 0) then
        texX = TEX_W - texX - 1
      end
      renderWallColumn(x, drawStart, drawEnd, perpWallDist, wtype, texX)
    end
  end
  job.col = last
end

function updateRender()
  if job == nil then
    if dirty then startRenderJob() end
  end
  if job then
    renderJobBatch(job)
    if job.col >= RENDER_W then
      renderSprites(job)
      drawCrosshair()
      drawMapAndHud()
      drawStatusBar()
      lcd.display()
      job = nil
    end
  end
end

-- ---------------------------------------------------------------------------
-- Screens (title / help-pause / result)
-- ---------------------------------------------------------------------------
function drawTitle()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawCenteredText("DOOM-LIKE", 10)
  lcd.drawCenteredText("A maze raycaster for e-ink", 30)

  lcd.setCursor(30, 70)
  lcd.print("Left/Right: turn")
  lcd.setCursor(30, 90)
  lcd.print("Up/Down: move")
  lcd.setCursor(30, 110)
  lcd.print("OK: shoot")
  lcd.setCursor(30, 130)
  lcd.print("Back: pause")
  lcd.setCursor(30, 150)
  lcd.print("Hold Back: exit")

  lcd.setCursor(30, 200)
  lcd.print("Kill all " .. #ENEMY_SPAWNS .. " demons!")
  if highScore > 0 then
    lcd.drawCenteredText("Best score: " .. highScore, 230)
  end
  lcd.drawCenteredText("Hold OK to start", 260)
  lcd.display()
end

function drawHelp()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawCenteredText("PAUSED", 20)
  lcd.setCursor(30, 60)
  lcd.print("Left/Right: turn")
  lcd.setCursor(30, 80)
  lcd.print("Up/Down: move")
  lcd.setCursor(30, 100)
  lcd.print("OK: shoot / resume")
  lcd.setCursor(30, 120)
  lcd.print("Back: resume")
  lcd.setCursor(30, 140)
  lcd.print("Hold Back: exit")
  lcd.display()
end

function drawResult()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  if state == "over" then
    lcd.drawCenteredText("YOU DIED", 60)
  else
    lcd.drawCenteredText("LEVEL CLEARED!", 60)
  end
  lcd.drawCenteredText("Score: " .. score, 100)
  lcd.drawCenteredText("Best: " .. highScore, 120)
  lcd.setCursor(30, 160)
  lcd.print("Hold OK to restart")
  lcd.setCursor(30, 180)
  lcd.print("Hold Back: exit")
  lcd.display()
end

-- ---------------------------------------------------------------------------
-- Main loop (state machine), dispatched every frame by the activity
-- ---------------------------------------------------------------------------
function onKey()
  if pressed("back") then
    if state == "play" then
      paused = not paused
      if paused then drawHelp() else dirty = true end
    end
    return
  end

  if state == "title" or state == "over" or state == "win" then
    if pressed("ok") then
      startGame()
      dirty = true
    end
    return
  end

  if paused then
    if pressed("ok") then
      paused = false
      dirty = true
    end
    return
  end

  local now = sys.getUptimeMs()
  local dt = (now - lastFrameMs) / 1000
  lastFrameMs = now
  if dt < 0 then dt = 0 end
  if dt > 0.5 then dt = 0.5 end  -- clamp huge jumps (e.g. after a flush)

  if input.isPressed("ok") then shoot() end
  movePlayer(dt)
  updateEnemies(dt)
  if flashTimer > 0 then flashTimer = flashTimer - dt end

  if health <= 0 then
    state = "over"
    saveHighScore()
    drawResult()
    return
  end
  if enemiesAlive() == 0 then
    state = "win"
    saveHighScore()
    drawResult()
    return
  end

  updateRender()
end

function init()
  buildTextures()
  loadMap()
  loadHighScore()
  startGame()
  state = "title"
  drawTitle()
end

function finish()
  saveHighScore()
end

-- No top-level loop: onKey() is called every frame by the activity loop.
