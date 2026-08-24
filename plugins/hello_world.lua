-- NAME: Hello World
-- DESC: Minimal Lua plugin example — draws text and a rectangle
-- ICON: AppsHub

local W, H = lcd.getWidth(), lcd.getHeight()

function init()
  lcd.fillScreen(1)
  lcd.setTextColor(0)
  lcd.drawText("Hello, CPR-vCodex Steroids!", 20, 200)
  lcd.fillRect(20, 240, W - 40, 2)
  lcd.display()
end

function onKey()
  -- Any button press exits the plugin
  sys.finish()
end
