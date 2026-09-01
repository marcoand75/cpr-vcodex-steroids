#include <Arduino.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <BitmapHelpers.h>
#include <DitheringConfig.h>
#include <ImageRenderConfig.h>
#include <HalDisplay.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalSpiBus.h>
#include <HalStorage.h>
#include <BoardConfig.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "HiddenBooksStore.h"
#include "FlashcardsStore.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "version.h"
#include "SdCardFontGlobals.h"
#include "util/StringUtils.h"
#include "UiFontSelection.h"
#include "util/WiFiUtils.h"
#include "SilentRestart.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/apps/LuaPluginActivity.h"
#include "activities/apps/PluginBrowserActivity.h"
#include "activities/apps/ScreenSaverActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BootRecovery.h"
#include "util/ButtonNavigator.h"
#include "util/CprVcodexLogs.h"
#include "util/ScreenshotUtil.h"
#include "util/TimeUtils.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts
#ifndef OMIT_BOOKERLY
EpdFont bookerly14RegularFont(&bookerly_14_regular);
EpdFont bookerly14BoldFont(&bookerly_14_bold);
EpdFont bookerly14ItalicFont(&bookerly_14_italic);
EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                    &bookerly14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont bookerly10RegularFont(&bookerly_10_regular);
EpdFont bookerly10BoldFont(&bookerly_10_bold);
EpdFont bookerly10ItalicFont(&bookerly_10_italic);
EpdFont bookerly10BoldItalicFont(&bookerly_10_bolditalic);
EpdFontFamily bookerly10FontFamily(&bookerly10RegularFont, &bookerly10BoldFont, &bookerly10ItalicFont,
                                    &bookerly10BoldItalicFont);
EpdFont bookerly12RegularFont(&bookerly_12_regular);
EpdFont bookerly12BoldFont(&bookerly_12_bold);
EpdFont bookerly12ItalicFont(&bookerly_12_italic);
EpdFont bookerly12BoldItalicFont(&bookerly_12_bolditalic);
EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, &bookerly12BoldFont, &bookerly12ItalicFont,
                                    &bookerly12BoldItalicFont);
EpdFont bookerly16RegularFont(&bookerly_16_regular);
EpdFont bookerly16BoldFont(&bookerly_16_bold);
EpdFont bookerly16ItalicFont(&bookerly_16_italic);
EpdFont bookerly16BoldItalicFont(&bookerly_16_bolditalic);
EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, &bookerly16BoldFont, &bookerly16ItalicFont,
                                    &bookerly16BoldItalicFont);
EpdFont bookerly18RegularFont(&bookerly_18_regular);
EpdFont bookerly18BoldFont(&bookerly_18_bold);
EpdFont bookerly18ItalicFont(&bookerly_18_italic);
EpdFont bookerly18BoldItalicFont(&bookerly_18_bolditalic);
EpdFontFamily bookerly18FontFamily(&bookerly18RegularFont, &bookerly18BoldFont, &bookerly18ItalicFont,
                                     &bookerly18BoldItalicFont);
#endif
#else
// Bookerly omitted to save flash — use NotoSans font data at the same IDs
EpdFont bookerly14RegularFont(&notosans_14_regular);
EpdFont bookerly14BoldFont(&notosans_14_bold);
EpdFont bookerly14ItalicFont(&notosans_14_italic);
EpdFont bookerly14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                    &bookerly14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont bookerly10RegularFont(&notosans_10_regular);
EpdFont bookerly10BoldFont(&notosans_10_bold);
EpdFont bookerly10ItalicFont(&notosans_10_italic);
EpdFont bookerly10BoldItalicFont(&notosans_10_bolditalic);
EpdFontFamily bookerly10FontFamily(&bookerly10RegularFont, &bookerly10BoldFont, &bookerly10ItalicFont,
                                    &bookerly10BoldItalicFont);
EpdFont bookerly12RegularFont(&notosans_12_regular);
EpdFont bookerly12BoldFont(&notosans_12_bold);
EpdFont bookerly12ItalicFont(&notosans_12_italic);
EpdFont bookerly12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, &bookerly12BoldFont, &bookerly12ItalicFont,
                                    &bookerly12BoldItalicFont);
EpdFont bookerly16RegularFont(&notosans_16_regular);
EpdFont bookerly16BoldFont(&notosans_16_bold);
EpdFont bookerly16ItalicFont(&notosans_16_italic);
EpdFont bookerly16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, &bookerly16BoldFont, &bookerly16ItalicFont,
                                    &bookerly16BoldItalicFont);
EpdFont bookerly18RegularFont(&notosans_18_regular);
EpdFont bookerly18BoldFont(&notosans_18_bold);
EpdFont bookerly18ItalicFont(&notosans_18_italic);
EpdFont bookerly18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily bookerly18FontFamily(&bookerly18RegularFont, &bookerly18BoldFont, &bookerly18ItalicFont,
                                     &bookerly18BoldItalicFont);
#endif
#endif

#ifndef OMIT_FONTS
#ifndef OMIT_LEXEND
// Lexend is bundled with regular and bold only. Italic falls back to regular,
// and bold italic falls back to bold to keep the family complete for EPUB styling.
EpdFont lexend10RegularFont(&lexend_10_regular);
EpdFont lexend10BoldFont(&lexend_10_bold);
EpdFontFamily lexend10FontFamily(&lexend10RegularFont, &lexend10BoldFont, &lexend10RegularFont, &lexend10BoldFont);
EpdFont lexend12RegularFont(&lexend_12_regular);
EpdFont lexend12BoldFont(&lexend_12_bold);
EpdFontFamily lexend12FontFamily(&lexend12RegularFont, &lexend12BoldFont, &lexend12RegularFont, &lexend12BoldFont);
EpdFont lexend14RegularFont(&lexend_14_regular);
EpdFont lexend14BoldFont(&lexend_14_bold);
EpdFontFamily lexend14FontFamily(&lexend14RegularFont, &lexend14BoldFont, &lexend14RegularFont, &lexend14BoldFont);
EpdFont lexend16RegularFont(&lexend_16_regular);
EpdFont lexend16BoldFont(&lexend_16_bold);
EpdFontFamily lexend16FontFamily(&lexend16RegularFont, &lexend16BoldFont, &lexend16RegularFont, &lexend16BoldFont);
EpdFont lexend18RegularFont(&lexend_18_regular);
EpdFont lexend18BoldFont(&lexend_18_bold);
EpdFontFamily lexend18FontFamily(&lexend18RegularFont, &lexend18BoldFont, &lexend18RegularFont, &lexend18BoldFont);
#endif

EpdFont notosans10RegularFont(&notosans_10_regular);
EpdFont notosans10BoldFont(&notosans_10_bold);
EpdFont notosans10ItalicFont(&notosans_10_italic);
EpdFont notosans10BoldItalicFont(&notosans_10_bolditalic);
EpdFontFamily notosans10FontFamily(&notosans10RegularFont, &notosans10BoldFont, &notosans10ItalicFont,
                                   &notosans10BoldItalicFont);
EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

#ifndef OMIT_UBUNTU
EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);
#else
// Ubuntu omitted to save flash — use NotoSans (already included for Vietnamese fallback)
EpdFont ui10RegularFont(&notosans_10_regular);
EpdFont ui10BoldFont(&notosans_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&notosans_12_regular);
EpdFont ui12BoldFont(&notosans_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);
#endif

namespace {

bool shouldUseNotoUiFonts(const Language lang) {
#ifdef OMIT_FONTS
  (void)lang;
  return false;
#else
  return lang == Language::VI;
#endif
}

void applyUiFontsForLanguage(const Language lang) {
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

#ifdef OMIT_FONTS
  (void)lang;
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
#else
  if (shouldUseNotoUiFonts(lang)) {
    // Keep Vietnamese UI at 10 pt for both slots to preserve existing layouts
    // while still providing full glyph coverage.
    renderer.insertFont(UI_10_FONT_ID, notosans10FontFamily);
    renderer.insertFont(UI_12_FONT_ID, notosans10FontFamily);
    LOG_INF("MAIN", "UI fonts: Noto Sans 8/10/10 for language %s", I18N.getLanguageName(lang));
    return;
  }

  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
#endif

  LOG_INF("MAIN", "UI fonts: default UI stack for language %s", I18N.getLanguageName(lang));
}

}  // namespace

void refreshUiFontsForCurrentLanguage() { applyUiFontsForLanguage(I18N.getLanguage()); }
void useLanguageSelectionUiFonts() { applyUiFontsForLanguage(Language::VI); }

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR char silentRebootPluginName[32];
RTC_NOINIT_ATTR uint32_t silentRebootCaller;  // 0=unknown, 1=apps, 2=home, 3=plugin_browser
RTC_NOINIT_ATTR bool silentRebootReturnToPluginBrowser;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_APPS = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_PLUGIN = 3;
constexpr uint32_t SILENT_REBOOT_TARGET_PLUGIN_BROWSER = 4;

// Latched once deep sleep is committed. WiFi activities also restart silently
// from onExit(), but deep sleep already gives us a clean heap on wake.
static bool deepSleepInProgress = false;

static void requestSilentRestart(SilentRebootTarget target, bool seamless,
                                 const char* pluginName = nullptr, bool fromApps = false,
                                 bool returnToPluginBrowser = false) {
  if (deepSleepInProgress) return;

  silentRebootTarget = static_cast<uint32_t>(target);
  silentRebootMagic = SILENT_REBOOT_MAGIC;

  if (target == SilentRebootTarget::Plugin) {
    if (pluginName) {
      StringUtils::copyToFixedBuffer(silentRebootPluginName, sizeof(silentRebootPluginName), pluginName);
    }
    silentRebootCaller = fromApps ? 1 : 2;  // 1=apps, 2=home
    silentRebootReturnToPluginBrowser = returnToPluginBrowser;
    LOG_INF("MAIN", "Silent restart (target=%u plugin:%s, caller=%u, retPB=%d)",
            silentRebootTarget, pluginName, fromApps ? 1 : 2, returnToPluginBrowser);
  } else {
    LOG_DBG("MAIN", "Silent restart (target=%u, seamless=%d)", static_cast<uint32_t>(target), seamless ? 1 : 0);
  }

  delay(seamless ? 20 : 50);
  ESP.restart();
}

void silentRestart() {
  requestSilentRestart(SilentRebootTarget::Home, false);
}

void silentRestartToReader() {
  requestSilentRestart(SilentRebootTarget::Reader, false);
}

void silentRestartToHome() {
  if (deepSleepInProgress) {
    LOG_DBG("MAIN", "Silent restart skipped: deepSleepInProgress");
    return;
  }
  LOG_DBG("MAIN", "Silent restart (target=home, seamless — no popup)");
  // Skip the "Loading..." popup for a seamless transition.
  // The display.begin(true) in setup() will skip the white flash,
  // and the boot activity is skipped, so the user sees a brief
  // dark frame then Home appears — visually cleaner than the popup.
  requestSilentRestart(SilentRebootTarget::Home, true);
}

void silentRestartToApps() {
  if (deepSleepInProgress) {
    LOG_DBG("MAIN", "Silent restart to apps skipped: deepSleepInProgress");
    return;
  }
  requestSilentRestart(SilentRebootTarget::Apps, true);
}

void silentRestartToPluginBrowser() {
  if (deepSleepInProgress) {
    LOG_DBG("MAIN", "Silent restart to plugin browser skipped: deepSleepInProgress");
    return;
  }
  requestSilentRestart(SilentRebootTarget::PluginBrowser, true);
}

void silentRestartToPlugin(const char* pluginName, bool fromApps, bool returnToPluginBrowser) {
  if (deepSleepInProgress) {
    LOG_DBG("MAIN", "silentRestartToPlugin skipped: deepSleepInProgress");
    return;
  }
  requestSilentRestart(SilentRebootTarget::Plugin, true, pluginName, fromApps, returnToPluginBrowser);
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() < calibratedPressDuration);
    abort = gpio.getPowerButtonHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// ---------------------------------------------------------------------------
// Sleep screensaver cycle on brief power-button tap during deep sleep
// ---------------------------------------------------------------------------
namespace {
// How long a press must be released within to count as a tap (not a wake hold).
// 200 ms keeps genuine deliberate taps snappy while the 200-400 ms dead zone
// (200 ms – getPowerButtonDuration()) falls through to the normal wake path.
constexpr unsigned long SCREENSAVER_TAP_MAX_MS = 200;

// How long we keep the chip awake after drawing the sleep screen so that taps
// arriving during the e-ink settle window are caught before deep sleep re-arms.
constexpr uint16_t POST_SLEEP_SCREEN_SETTLE_MS = 500;
}  // namespace

// Returns true if the wake press was a brief tap (released within SCREENSAVER_TAP_MAX_MS).
// Reads GPIO directly — InputManager debounce takes ~500 ms and would miss short taps.
static bool detectScreensaverCycleTap() {
  const unsigned long start = millis();
  while (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW && (millis() - start) < SCREENSAVER_TAP_MAX_MS) {
    delay(5);
  }
  const bool released = digitalRead(InputManager::POWER_BUTTON_PIN) == HIGH;
  LOG_INF("MAIN", "Cycle tap detect: %s (took %lu ms)", released ? "TAP" : "HELD", millis() - start);
  return released;
}

// Poll for power-button taps during the e-ink settle window after the sleep screen is drawn.
// Returns true if a tap was detected, false on timeout.
static bool pollForCycleTapDuringSleepEntry() {
  const auto start = millis();
  while (millis() - start < POST_SLEEP_SCREEN_SETTLE_MS) {
    if (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW) {
      const auto pressStart = millis();
      while (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW &&
             (millis() - pressStart) < SCREENSAVER_TAP_MAX_MS) {
        delay(5);
      }
      return digitalRead(InputManager::POWER_BUTTON_PIN) == HIGH;
    }
    delay(10);
  }
  return false;
}

// ISR flag set on a falling edge while the sleep screen is rendering (chip awake, no main loop).
static volatile bool sleepEntryTapPending = false;

static void IRAM_ATTR onSleepEntryPowerEdge() { sleepEntryTapPending = true; }

static void armSleepEntryTapIsr() {
  sleepEntryTapPending = false;
  attachInterrupt(InputManager::POWER_BUTTON_PIN, onSleepEntryPowerEdge, FALLING);
}

static void disarmSleepEntryTapIsr() {
  detachInterrupt(InputManager::POWER_BUTTON_PIN);
  sleepEntryTapPending = false;
}

// Returns true if the ISR captured a complete tap (press + release) during the render.
static bool consumeCompletedSleepEntryTap() {
  if (!sleepEntryTapPending) return false;
  if (digitalRead(InputManager::POWER_BUTTON_PIN) != HIGH) return false;
  sleepEntryTapPending = false;
  return true;
}

// Minimal boot path for cycle-screensaver-on-tap.
// Runs after a brief power-button tap from deep sleep; does NOT do a full UI boot.
// Loads APP_STATE, inits display+renderer, cycles the sleep image, then re-sleeps.
static void initDisplayRenderer(bool seamless = false);
[[noreturn]] static void cycleScreensaverThenDeepSleep() {
  APP_STATE.loadFromFile();

  initDisplayRenderer(true);

  armSleepEntryTapIsr();
  while (true) {
    SleepActivity::cycleScreensaverFromDeepSleep(renderer);
    if (consumeCompletedSleepEntryTap()) continue;
    if (pollForCycleTapDuringSleepEntry()) continue;
    break;
  }
  disarmSleepEntryTapIsr();

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Screensaver cycled — re-entering deep sleep");
  powerManager.startDeepSleep(gpio);

  // startDeepSleep() does not return on hardware; spin so [[noreturn]] is satisfied.
  while (true) { delay(1000); }
}

// Returns true when the replacement-screensaver is enabled, battery is
// sufficient, and the user is inside a reader activity. Only active
// during reading so the rest of the system keeps normal sleep.
static bool canStartReplacementScreenSaver() {
  if (!SETTINGS.screenSaverReplaceSleep) return false;
  if (!activityManager.isReaderActivity()) return false;
  const int minPct = (static_cast<int>(SETTINGS.screenSaverMinBattery) + 1) * 10;
  return static_cast<int>(powerManager.getBatteryPercentage()) >= minPct;
}

// Launches the screensaver on top of the reader, preserving the reader on
// the activity stack so it's restored when the user wakes the device.
static bool startReplacementScreenSaver() {
  if (activityManager.isScreenSaverActive()) return false;
  if (!activityManager.isReaderActivity()) return false;

  activityManager.pushActivity(std::make_unique<ScreenSaverActivity>(renderer, mappedInputManager, true));
  return true;
}

// Enter deep sleep mode
void enterDeepSleep() {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  deepSleepInProgress = true;

  if (SETTINGS.cycleScreensaverOnTap) {
    // Arm an ISR before goToSleep() so taps that land during the (blocking)
    // sleep-screen render can still be caught.  The framebuffer snapshot for
    // cycleScreensaverFromDeepSleep is saved by SleepActivity::onEnter()
    // (which runs synchronously inside goToSleep), so we don't duplicate
    // the ~52 KB SD write here — that saves ~300-800 ms on slow SD cards.
    armSleepEntryTapIsr();
    activityManager.goToSleep();
    // Catch any taps that arrived during the render or the settle window.
    while (true) {
      if (consumeCompletedSleepEntryTap()) {
        SleepActivity::cycleScreensaverFromDeepSleep(renderer);
        continue;
      }
      if (pollForCycleTapDuringSleepEntry()) {
        SleepActivity::cycleScreensaverFromDeepSleep(renderer);
        continue;
      }
      break;
    }
    disarmSleepEntryTapIsr();
  } else {
    activityManager.goToSleep();
    delay(POST_SLEEP_SCREEN_SETTLE_MS);
  }

  APP_STATE.saveToFile();  // deferred: serialized after the sleep screen rendered

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFiUtils::forceDisconnect();
    WiFiUtils::powerOff();
  }

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void ensureSdFontLoaded() {
  if (Storage.ready()) {
    sdFontSystem.ensureLoaded(renderer);
  }
}

// Free font heap memory for use by other subsystems (e.g. screensaver PNG decoder).
// Font caches and decompressor are rebuilt on next font access.
void freeFontMemory() {
  const int beforeFree = static_cast<int>(ESP.getFreeHeap());
  const int beforeMaxAlloc = static_cast<int>(ESP.getMaxAllocHeap());
  fontCacheManager.clearCache();
  fontDecompressor.deinit();
  LOG_DBG("FNT", "freeFontMemory: free=%d->%d maxAlloc=%d->%d",
          beforeFree, static_cast<int>(ESP.getFreeHeap()),
          beforeMaxAlloc, static_cast<int>(ESP.getMaxAllocHeap()));
}

// Restore font memory that was freed with freeFontMemory().
// Reinitialises the decompressor (lazy — pages decompress on demand).
void restoreFontMemory() {
  const int beforeFree = static_cast<int>(ESP.getFreeHeap());
  const int beforeMaxAlloc = static_cast<int>(ESP.getMaxAllocHeap());
  fontDecompressor.init();
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  LOG_DBG("FNT", "restoreFontMemory: free=%d->%d maxAlloc=%d->%d",
          beforeFree, static_cast<int>(ESP.getFreeHeap()),
          beforeMaxAlloc, static_cast<int>(ESP.getMaxAllocHeap()));
}

static void initDisplayRenderer(bool seamless) {
  BoardConfig::holdPowerRails();
  display.begin(seamless);
  HalSpiBus::begin();
  renderer.begin();
}

void setupDisplayAndFonts(bool seamless = false) {
  initDisplayRenderer(seamless);
  renderer.setDarkMode(SETTINGS.darkMode);
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");
  LOG_DBG("HCR-FRAG", "fonts pre-begin: free=%d maxA=%d frag=%d", static_cast<int>(ESP.getFreeHeap()),
          static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));

  // Font decompressor is initialised lazily on first use (decompressGroup).
  // This avoids allocating the 48 KB pool (page buffers + inflate ring buffer)
  // at boot when most rendering uses SD-card fonts (already decompressed).
  // fontDecompressor.init() is called internally when needed.
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(BOOKERLY_10_FONT_ID, bookerly10FontFamily);
  renderer.insertFont(BOOKERLY_12_FONT_ID, bookerly12FontFamily);
  renderer.insertFont(BOOKERLY_16_FONT_ID, bookerly16FontFamily);
  renderer.insertFont(BOOKERLY_18_FONT_ID, bookerly18FontFamily);

#ifndef OMIT_LEXEND
  renderer.insertFont(LEXEND_10_FONT_ID, lexend10FontFamily);
  renderer.insertFont(LEXEND_12_FONT_ID, lexend12FontFamily);
  renderer.insertFont(LEXEND_14_FONT_ID, lexend14FontFamily);
  renderer.insertFont(LEXEND_16_FONT_ID, lexend16FontFamily);
  renderer.insertFont(LEXEND_18_FONT_ID, lexend18FontFamily);
#endif

  renderer.insertFont(NOTOSANS_10_FONT_ID, notosans10FontFamily);
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  refreshUiFontsForCurrentLanguage();
  LOG_DBG("HCR-FRAG", "fonts builtin done: free=%d maxA=%d frag=%d", static_cast<int>(ESP.getFreeHeap()),
          static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
  if (Storage.ready()) {
    sdFontSystem.begin(renderer);
  }
  LOG_DBG("HCR-FRAG", "fonts SD begin done: free=%d maxA=%d frag=%d", static_cast<int>(ESP.getFreeHeap()),
          static_cast<int>(ESP.getMaxAllocHeap()),
          static_cast<int>(ESP.getFreeHeap()) - static_cast<int>(ESP.getMaxAllocHeap()));
  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  t1 = millis();

  // ===========================================================================
  // PHASE 1 — Hardware init
  //   - Bring up HalSystem and detect the board.
  //   - Hold the power rails BEFORE any probe (BoardConfig::selectDevice is
  //     a fallback; HalGPIO::begin() may re-select X3 if the probe confirms it).
  //   - Start the RTC, tilt sensor, and power manager.
  // ===========================================================================
  HalSystem::begin();

  BoardConfig::selectDevice(BoardConfig::Board::XteinkX4);
  BoardConfig::holdPowerRails();

  // Snapshot the silent-reboot routing BEFORE zeroing RTC_NOINIT.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
       (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_PLUGIN_BROWSER) ? silentRebootTarget : 0;
  LOG_INF("MAIN", "RTC: magic=0x%08x target=%u caller=%u retPB=%d snapshotTarget=%u",
          silentRebootMagic, silentRebootTarget, silentRebootCaller, silentRebootReturnToPluginBrowser, snapshotTarget);

  char snapshotPluginName[32] = {0};
  bool snapshotCallerFromApps = false;
  bool snapshotReturnToPluginBrowser = false;
  if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_PLUGIN) {
    StringUtils::copyToFixedBuffer(snapshotPluginName, sizeof(snapshotPluginName), silentRebootPluginName);
    snapshotCallerFromApps = (silentRebootCaller == 1);
    snapshotReturnToPluginBrowser = silentRebootReturnToPluginBrowser;
  }

  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootPluginName[0] = '\0';
  silentRebootCaller = 0;
  silentRebootReturnToPluginBrowser = false;

  gpio.begin();
  powerManager.begin();
  halClock.begin();
  halTiltSensor.begin();

  // Seed the PRNG from ESP32 hardware entropy (RF ADC noise).
  // Without this, random() produces a deterministic sequence on each cold boot.
  randomSeed(esp_random());

  // Build the shared grayscale gamma LUT once so every image decoder (BMP
  // reader, JPEG/PNG cover converters, screensaver/sleep) uses the same
  // correction without a per-pixel pow().
  initGammaLUT();

  // Disable Arduino core's NVS auto-persist of Wi-Fi credentials. WifiSelectionActivity
  // always scans first and uses WifiCredentialStore (SD card JSON) as the source of
  // truth; the SDK's hidden nvs.net80211 copy must not auto-reconnect behind the user.
  WiFiUtils::disableNvsAutoPersist();
  WiFiUtils::powerOff();

#ifdef ENABLE_SERIAL_LOG
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }
#endif

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // ===========================================================================
  // PHASE 2 — Storage + recovery
  //   - SD card is required for everything else; bail out to a popup on fail.
  //   - SdFat file timestamps use the synced RTC time.
  //   - Panic check + BootRecovery init set the recovery mask (skips that
  //     are then honoured by every runBootStage() call below).
  // ===========================================================================
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  TimeUtils::registerSdFatDateTimeCallback();

  HalSystem::checkPanic();
  BootRecovery::initialize();
  // Wire BootRecovery's runBootStage() skip log to the same destination as
  // the other boot diagnostics so the cpr-vcodex-logs recovery file picks
  // up the skip events for post-mortem analysis.
  BootRecovery::setSkipLogFn([](const char* message) { CPR_VCODEX_LOG_EVENT("BOOT", message); });

  // ===========================================================================
  // PHASE 3 — Core settings + UI theme
  //   Loads (in order): settings.json, language, KOReader credentials, OPDS
  //   servers, then refreshes the UI theme + ButtonNavigator binding.
  //   Each is gated by the recovery mask; the lambda runs only on a clean
  //   boot. imageRenderConfigApplySettings() must run after SETTINGS so the
  //   image decoders pick up the right gamma/dither config.
  // ===========================================================================
  if (BootRecovery::runBootStage(BootRecovery::BootStage::Settings, BootRecovery::shouldSkipSettings(),
                                 "settings",
                                 [] { SETTINGS.loadFromFile(); })) {
    imageRenderConfigApplySettings();  // Apply image-rendering params from loaded settings
    LOG_DBG("BOOT", "After settings: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  BootRecovery::runBootStage(BootRecovery::BootStage::Language, BootRecovery::shouldSkipLanguage(), "language",
                             [] { I18N.loadSettings(); });

  BootRecovery::runBootStage(BootRecovery::BootStage::KOReader, BootRecovery::shouldSkipKOReader(), "koreader",
                             [] { KOREADER_STORE.loadFromFile(); });

  BootRecovery::runBootStage(BootRecovery::BootStage::OPDS, BootRecovery::shouldSkipOPDS(), "opds",
                             [] { OPDS_STORE.loadFromFile(); });

  BootRecovery::enterStage(BootRecovery::BootStage::UiTheme);
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // ===========================================================================
  // PHASE 4 — Wakeup handling
  //   Reads the RTC wakeup reason and:
  //     - PowerButton: optionally route a short tap into cycleScreensaver
  //       (does not return); otherwise require a hold-to-wake via
  //       gpio.verifyPowerButtonWakeup() (does not return on abort).
  //     - AfterUSBPower: re-sleep immediately.
  //     - AfterFlash / Other: fall through to the normal boot path.
  // ===========================================================================
  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      // If cycle-screensaver-on-tap is enabled, check whether this is a brief tap
      // before running the normal hold-to-wake verification.
      if (SETTINGS.cycleScreensaverOnTap) {
        if (detectScreensaverCycleTap()) {
          // Brief tap — cycle the screensaver and go back to sleep immediately.
          // This does not return.
          cycleScreensaverThenDeepSleep();
        }
        // Button held past the tap window (>200 ms): this is a wake intent.
        // detectScreensaverCycleTap() already waited up to SCREENSAVER_TAP_MAX_MS
        // polling raw GPIO, so we know the button is currently LOW (still held).
        // We still run verifyPowerButtonWakeup() so the user must hold for the
        // configured duration — prevents accidental wakes from presses in the
        // 200-400 ms dead zone that were not long enough to be intentional.
        gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                     SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      } else {
        LOG_DBG("MAIN", "Verifying power button press duration");
        gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                     SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      }
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version %s", CROSSPOINT_VERSION);

  // Manual safe boot: hold Back during boot to skip all data stores and
  // force the Home activity, mirroring the recovery-mode bit.
  gpio.update();
  const bool manualSafeBoot = gpio.isPressed(HalGPIO::BTN_BACK);
  if (manualSafeBoot) {
    CPR_VCODEX_LOG_EVENT("BOOT", "Manual safe boot requested by holding Back during boot");
  }

  // ===========================================================================
  // PHASE 5 — Display + fonts + boot screen
  // ===========================================================================
  BootRecovery::enterStage(BootRecovery::BootStage::DisplayAndFonts);
  setupDisplayAndFonts(isSilentReboot);
  LOG_DBG("BOOT", "After display/fonts: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (!isSilentReboot) {
    activityManager.goToBoot();
  }

  // ===========================================================================
  // PHASE 6 — Data stores that don't block boot
  //   The first 3 (State, RecentBooks, Flashcards) are loaded eagerly because
  //   they are read on the very first frame of Home / Apps. The rest are
  //   loaded on demand to save boot heap (see the per-store comment for the
  //   activity that needs them).
  // ===========================================================================
  const bool skipStateLoad = manualSafeBoot || BootRecovery::shouldSkipState();
  const bool skipReadingStatsLoad = manualSafeBoot || BootRecovery::shouldSkipReadingStats();
  const bool skipRecentBooksLoad = manualSafeBoot || BootRecovery::shouldSkipRecentBooks();
  const bool skipFavoritesLoad = manualSafeBoot || BootRecovery::shouldSkipFavorites();
  const bool skipFlashcardsLoad = manualSafeBoot || BootRecovery::shouldSkipFlashcards();
  const bool skipAchievementsLoad = manualSafeBoot || BootRecovery::shouldSkipAchievements();
  const bool forceHomeBoot = manualSafeBoot || BootRecovery::shouldForceHome();

  if (BootRecovery::runBootStage(BootRecovery::BootStage::State, skipStateLoad, "app state",
                                 [] { APP_STATE.loadFromFile(); })) {
    LOG_DBG("BOOT", "After app state: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  if (BootRecovery::runBootStage(BootRecovery::BootStage::ReadingStats, skipReadingStatsLoad, "reading stats",
                                 [] { READING_STATS.markLoadSkippedForRecovery(); })) {
    // Reading stats are loaded on demand by the first activity that needs them.
    LOG_DBG("BOOT", "Reading stats deferred (loaded on demand)");
  }

  if (BootRecovery::runBootStage(BootRecovery::BootStage::RecentBooks, skipRecentBooksLoad, "recent books",
                                 [] { RECENT_BOOKS.loadFromFile(); })) {
    LOG_DBG("BOOT", "After recent books: free=%u maxA=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  if (BootRecovery::runBootStage(BootRecovery::BootStage::Favorites, skipFavoritesLoad, "favorites", nullptr)) {
    // Favorites are loaded on demand by HomeActivity/LibraryActivity to save boot heap.
    LOG_DBG("BOOT", "Favorites deferred (loaded on demand)");
  }

  // Hidden books are loaded on demand by LibraryActivity to save boot heap.
  LOG_DBG("BOOT", "Hidden books deferred (loaded on demand)");

  // Flashcards are skipped on silent reboot because the on-disk state
  // hasn't changed; on a clean boot the deck metadata is loaded on demand
  // by FlashcardsAppActivity/QuickCardsActivity.
  const bool skipFlashcardsEffective = skipFlashcardsLoad || isSilentReboot;
  if (BootRecovery::runBootStage(BootRecovery::BootStage::Flashcards, skipFlashcardsEffective, "flashcards", nullptr)) {
    LOG_DBG("BOOT", "Flashcards deferred (loaded on demand)");
  }

  if (BootRecovery::runBootStage(BootRecovery::BootStage::Achievements, skipAchievementsLoad, "achievements", nullptr)) {
    // Achievements are loaded on demand by AchievementsActivity/SleepActivity.
    LOG_DBG("BOOT", "Achievements deferred (loaded on demand)");
  }

  // ===========================================================================
  // PHASE 7 — Route decision + boot completion
  //   Decides which activity to launch: crash report (panic), reader
  //   resume, apps, plugin, or Home. The reader-resume path bumps
  //   readerActivityLoadCount to break boot loops if the EPUB fails to load.
  // ===========================================================================
  const bool countUsefulStart = !isSilentReboot && !forceHomeBoot &&
                                wakeupReason != HalGPIO::WakeupReason::AfterUSBPower &&
                                wakeupReason != HalGPIO::WakeupReason::AfterFlash;
  const uint8_t syncDayReminderThreshold = SETTINGS.getSyncDayReminderStartThreshold();
  BootRecovery::enterStage(BootRecovery::BootStage::RouteDecision);

  if (HalSystem::isRebootFromPanic() && !forceHomeBoot) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_READER && !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_APPS) {
    activityManager.goToApps();
  } else if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_PLUGIN_BROWSER) {
    activityManager.goToPluginBrowser();
  } else if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_PLUGIN) {
    activityManager.goToPlugin(snapshotPluginName, snapshotCallerFromApps, snapshotReturnToPluginBrowser);
  } else if (isSilentReboot) {
    activityManager.goHome();
  } else {
    const bool bootToHome = forceHomeBoot || APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
                            mappedInputManager.isPressed(MappedInputManager::Button::Back) ||
                            APP_STATE.readerActivityLoadCount > 0;

    if (bootToHome) {
      if (countUsefulStart) {
        APP_STATE.recordUsefulStart(syncDayReminderThreshold);
        APP_STATE.saveToFile();
      }
      activityManager.goHome();
    } else {
      // Clear app state to avoid getting into a boot loop if the epub doesn't load
      const auto path = APP_STATE.openEpubPath;
      APP_STATE.openEpubPath = "";
      APP_STATE.readerActivityLoadCount++;
      if (countUsefulStart) {
        APP_STATE.recordUsefulStart(syncDayReminderThreshold);
      }
      APP_STATE.saveToFile();
      activityManager.goToReader(path);
    }
  }

  BootRecovery::markBootCompleted();

  if (isSilentReboot) {
    activityManager.requestUpdateAndWait();
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();

  LOG_INF("BOOT-TIME", "setup done in %lu ms (silent=%d route=%u free=%u maxA=%u)",
          static_cast<unsigned long>(millis() - t1), isSilentReboot ? 1 : 0, snapshotTarget,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// Power button state machine: short press, long press, release.
//
// Split into 3 sub-functions (onPressEdge / onLongPressHold / onReleaseEdge)
// for readability. The static state lives at file scope so it survives across
// loop iterations.
//
//   press edge  → record start time; if a screen saver is active, remember it
//                 so the release edge suppresses the shortPwrBtn action.
//   hold >= configured duration → if reader + screenSaverReplaceSleep + battery
//                                  ok, push the replacement screensaver.
//                                otherwise enterDeepSleep() (does not return).
//   release edge → follow shortPwrBtn (FORCE_REFRESH here, TOGGLE_STATUS_BAR
//                 and PAGE_TURN inside the reader activity's loop()).
//                 Suppress the release action when a screen saver is the
//                 current activity (the wake-key event is consumed by the
//                 screen-saver activity itself, not by the main loop).

namespace {

// Persistent state for the power-button state machine. Lives at namespace
// scope so the values survive across loop() iterations; using a struct keeps
// the per-field resets in one place.
struct PowerButtonState {
  unsigned long downMs = 0;
  // True when the press started while a screen saver was active. Suppresses
  // the release-edge shortPwrBtn action so the wake-key event is consumed
  // entirely by the screen-saver logic.
  bool suppressNextRelease = false;
};

PowerButtonState& powerButtonState() {
  static PowerButtonState s;
  return s;
}

// Returns true if the caller MUST return from loop() (because we entered
// deep sleep or pushed the replacement screensaver). The caller is expected
// to return immediately on true.
bool handlePowerButtonPressEdge() {
  if (!gpio.wasPressed(HalGPIO::BTN_POWER)) return false;
  auto& s = powerButtonState();
  s.downMs = millis();
  if (activityManager.isScreenSaverActive()) {
    s.suppressNextRelease = true;
  }
  return false;
}

// Long-press detection: while the button is held past the configured
// duration, fire either the replacement screensaver (reader + battery ok)
// or deep sleep. Returns true when the caller must return.
bool handlePowerButtonLongPressHold() {
  auto& s = powerButtonState();
  if (s.downMs == 0) return false;
  // While a screen saver is already active, the screen-saver activity
  // handles wake-button events itself (it calls finish() or onGoHome() and
  // returns to the previous activity), so we skip the long-press detection
  // entirely.
  if (activityManager.isScreenSaverActive()) return false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) return false;
  if (millis() - s.downMs < SETTINGS.getPowerButtonDuration()) return false;

  if (canStartReplacementScreenSaver()) {
    startReplacementScreenSaver();
    s.downMs = 0;
    return true;
  }
  enterDeepSleep();
  s.downMs = 0;
  return true;  // enterDeepSleep() does not return, but if it ever does we
                // still want the loop to bail out.
}

void handlePowerButtonReleaseEdge() {
  auto& s = powerButtonState();
  // Suppress the release-edge shortPwrBtn action when a screen saver is
  // involved (the wake-key event is consumed by the screen-saver logic).
  if (s.suppressNextRelease || activityManager.isScreenSaverActive()) {
    s.suppressNextRelease = false;
    s.downMs = 0;
    return;
  }
  // FORCE_REFRESH: redraw the screen on every short press.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    RenderLock lock;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

}  // namespace

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
#ifdef ENABLE_SERIAL_LOG
  static unsigned long lastMemPrint = 0;
#endif

  gpio.update();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  // --- Battery safety under high-power-draw activities (WiFi, OTA) ---
  // When WiFi is active (file transfer, OTA, web server), the ESP32 draws much
  // more current. A battery reading that looks fine at idle can sag below the
  // brown-out threshold mid-operation, causing an abrupt power-off with the e-ink
  // display frozen. We now proactively check for critically low battery and render
  // a shutdown screen BEFORE the device dies.
  static unsigned long lastBatteryCheckMs = 0;
  const unsigned long now = millis();
  if (now - lastBatteryCheckMs >= 5000) {  // Check every 5 seconds to limit I2C overhead
    lastBatteryCheckMs = now;
    const uint16_t batteryPct = powerManager.getBatteryPercentage();
    if (batteryPct < 5 && !deepSleepInProgress) {
      LOG_INF("PWR", "Battery critically low (%u%%) under load — rendering shutdown screen", batteryPct);
      {
        RenderLock lock;
        renderer.clearScreen();
        const auto height = renderer.getLineHeight(UI_10_FONT_ID);
        const auto top = (renderer.getScreenHeight() - height) / 2;
        renderer.drawCenteredText(UI_10_FONT_ID, top - height, tr(STR_BATTERY_EMPTY_TITLE), true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_BATTERY_EMPTY_BODY));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      delay(2000);  // Let the user see the shutdown screen
      enterDeepSleep();
      // Should never reach here (esp_deep_sleep_start blocks)
    }
  }

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setDarkMode(SETTINGS.darkMode);
  renderer.setTextDarkness(SETTINGS.textDarkness);

#ifdef ENABLE_SERIAL_LOG
  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }
#endif

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  static bool screenshotButtonsReleased = true;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  } else {
    screenshotButtonsReleased = true;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Power button state machine. Each phase returns a "must return" flag so
  // the loop structure stays flat and easy to follow.
  if (handlePowerButtonPressEdge()) return;
  if (handlePowerButtonLongPressHold()) return;
  handlePowerButtonReleaseEdge();

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
