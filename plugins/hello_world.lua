-- ============================================================================
-- Hello World — minimal Lua plugin
-- ----------------------------------------------------------------------------
--
-- Lifecycle:
--   init()   runs once after the script is loaded — draw the first frame
--   onKey()  runs every loop frame (~500 ms on e-ink): poll input here
--   finish() optional cleanup before the VM is shut down
--
-- Exit rules (the device has few buttons, so Back has two roles):
--   • Select/OK exits (edge-triggered input below).
--   • Back SHORT press is reserved for the plugin (here: nothing to do).
--   • Back LONG press (hold ~1.5 s) exits — handled by the activity.
-- ============================================================================

-- NAME: Hello World
-- DESC: Minimal Lua plugin example — draws text and a rectangle
-- ICON: AppsHub
-- RESTART: no

local W, H = lcd.getWidth(), lcd.getHeight()

function init()
  -- White background, black text (the standard e-ink palette)
  lcd.fillScreen(1)
  lcd.setTextColor(0)

  lcd.drawText("Hello, CPR-vCodex Steroids!", 20, 180)
  lcd.fillRect(20, 220, W - 40, 2)  -- separator line
  lcd.drawText("Select or hold Back to exit", 20, 240)

  -- Flush the framebuffer to the e-ink panel
  lcd.display()
end

function onKey()
  -- Edge-triggered input: fires only on the frame the button was pressed.
  -- (Back short-press is delivered here too, but this plugin has no use for
  -- it — the long-press exit is handled by the activity.)
  if input.wasPressed("ok") then
    sys.finish()  -- same as plugin.finish()
  end
end
