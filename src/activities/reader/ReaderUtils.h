#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <memory>
#include <new>

#include <I18n.h>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/AchievementPopupUtils.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long CONFIRM_DOUBLE_CLICK_MS = 300;
constexpr unsigned long SKIP_HOLD_MS = 700;

struct TiledGrayscaleTimings {
  uint32_t grayLsb = 0;
  uint32_t grayMsb = 0;
  uint32_t grayDisplay = 0;
  uint32_t cleanup = 0;
};

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
  bool fromFrontButton;  // true when triggered by Left/Right front buttons (not side buttons)
  // Per-button detection for per-directional long-press configuration.
  // These are set when the corresponding side/front button was released
  // (for long-press purposes — short press uses the wasReleased path in
  // detectPageTurn when longPressButtonBehavior != LONG_PRESS_OFF).
  // At most one of {upBtn, downBtn, leftBtn, rightBtn} will be true.
  bool upBtn;      // side Up button (PageBack when not swapped, PageForward when swapped)
  bool downBtn;    // side Down button (PageForward when not swapped, PageBack when swapped)
  bool leftBtn;    // front Left button (or Right when orientation-swapped)
  bool rightBtn;   // front Right button (or Left when orientation-swapped)
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  // usePress is true when all long-press behaviors are OFF (normal short-press page turn).
  // With per-directional config, we check if any Up/Down button has a non-OFF action.
  const bool sideLongPressActive =
      SETTINGS.longPressUpBehavior != CrossPointSettings::BTN_ACTION_OFF ||
      SETTINGS.longPressDownBehavior != CrossPointSettings::BTN_ACTION_OFF ||
      (SETTINGS.longPressButtonBehavior != CrossPointSettings::LONG_PRESS_OFF &&
       SETTINGS.longPressUpBehavior == CrossPointSettings::BTN_ACTION_OFF &&
       SETTINGS.longPressDownBehavior == CrossPointSettings::BTN_ACTION_OFF);
  const bool usePress = !sideLongPressActive;
  const bool tiltNext = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF && halTiltSensor.wasTiltedBack();
  const bool swapFront =
      SETTINGS.frontButtonFollowOrientation && (SETTINGS.orientation == CrossPointSettings::INVERTED ||
                                                 SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CCW);
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const bool prev = usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) || input.wasPressed(prevButton))
                               : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                  input.wasReleased(prevButton));
  const bool frontPrev = !usePress && input.wasReleased(prevButton);
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                           input.wasReleased(MappedInputManager::Button::Power);
  const bool next = usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                  input.wasPressed(nextButton))
                               : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                  input.wasReleased(nextButton));
  const bool frontNext = !usePress && input.wasReleased(nextButton);

  // Per-button detection for per-directional long-press configuration.
  // We detect the logical button (Up/Down/Left/Right) that was released.
  // Side buttons: Up=PageBack, Down=PageForward (or swapped via sideButtonLayout).
  // Front buttons: Left/Right are user-remappable front buttons.
  // The swapSide/swapFront logic mirrors detectPageTurn's orientation handling.
  const bool swapSide = (SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV);
  const bool upReleased = input.wasReleased(MappedInputManager::Button::PageBack);
  const bool downReleased = input.wasReleased(MappedInputManager::Button::PageForward);
  // If side layout is swapped, Up/Down meaning is reversed.
  const bool upBtn = swapSide ? downReleased : upReleased;
  const bool downBtn = swapSide ? upReleased : downReleased;
  // Front buttons: detect raw Left/Right button release (logical buttons).
  const bool leftBtn = input.wasReleased(MappedInputManager::Button::Left);
  const bool rightBtn = input.wasReleased(MappedInputManager::Button::Right);

  return {tiltPrev || prev, tiltNext || next, tiltPrev || tiltNext, frontPrev || frontNext,
          upBtn, downBtn, leftBtn, rightBtn};
}

inline bool hasNonConfirmNavigationInput(const MappedInputManager& input) {
  return input.wasPressed(MappedInputManager::Button::Back) || input.wasReleased(MappedInputManager::Button::Back) ||
         input.wasPressed(MappedInputManager::Button::PageBack) ||
         input.wasReleased(MappedInputManager::Button::PageBack) ||
         input.wasPressed(MappedInputManager::Button::PageForward) ||
         input.wasReleased(MappedInputManager::Button::PageForward) ||
         input.wasPressed(MappedInputManager::Button::Left) || input.wasReleased(MappedInputManager::Button::Left) ||
         input.wasPressed(MappedInputManager::Button::Right) || input.wasReleased(MappedInputManager::Button::Right) ||
         input.wasPressed(MappedInputManager::Button::Up) || input.wasReleased(MappedInputManager::Button::Up) ||
         input.wasPressed(MappedInputManager::Button::Down) || input.wasReleased(MappedInputManager::Button::Down) ||
         input.wasPressed(MappedInputManager::Button::Power) || input.wasReleased(MappedInputManager::Button::Power);
}

inline bool shouldToggleStatusBar(const MappedInputManager& input) {
  return SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::TOGGLE_STATUS_BAR &&
         input.wasReleased(MappedInputManager::Button::Power);
}

// Returns true if the short power button should trigger a reader action
// (beyond the basic IGNORE/SLEEP/PAGE_TURN/TOGGLE_STATUS_BAR/FORCE_REFRESH).
inline bool isPowerButtonReaderAction(const CrossPointSettings::SHORT_PWRBTN spwbtn) {
  return spwbtn >= CrossPointSettings::SPWBTN_OFF &&
         spwbtn != CrossPointSettings::SPWBTN_IGNORE;
}

// Returns the BUTTON_ACTION corresponding to the shortPwrBtn setting,
// or BTN_ACTION_OFF if the power button should not trigger a reader action.
inline CrossPointSettings::BUTTON_ACTION shortPwrBtnToReaderAction(const CrossPointSettings::SHORT_PWRBTN spwbtn) {
  switch (spwbtn) {
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_ADD_CLIPPING:      return CrossPointSettings::BTN_ACTION_ADD_CLIPPING;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_VIEW_CLIPPINGS:    return CrossPointSettings::BTN_ACTION_VIEW_CLIPPINGS;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_TOGGLE_BOOKMARK:   return CrossPointSettings::BTN_ACTION_TOGGLE_BOOKMARK;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_VIEW_BOOKMARKS:    return CrossPointSettings::BTN_ACTION_VIEW_BOOKMARKS;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_LOOKUP_WORD:       return CrossPointSettings::BTN_ACTION_LOOKUP_WORD;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_DICTIONARY:        return CrossPointSettings::BTN_ACTION_DICTIONARY;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_CHAPTER_SKIP:      return CrossPointSettings::BTN_ACTION_CHAPTER_SKIP;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_ORIENTATION:       return CrossPointSettings::BTN_ACTION_ORIENTATION;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_DARK_MODE:         return CrossPointSettings::BTN_ACTION_DARK_MODE;
    case CrossPointSettings::SHORT_PWRBTN::SPWBTN_READER_SETTINGS:   return CrossPointSettings::BTN_ACTION_READER_SETTINGS;
    default: return CrossPointSettings::BTN_ACTION_OFF;
  }
}

// Migration helpers: map legacy long-press enums to unified BUTTON_ACTION.
// Centralized here so all three reader activities use the same fallback mapping.
inline CrossPointSettings::BUTTON_ACTION legacyLongPressToButtonAction(uint8_t legacy) {
  switch (legacy) {
    case CrossPointSettings::LONG_PRESS_OFF:               return CrossPointSettings::BTN_ACTION_OFF;
    case CrossPointSettings::LONG_PRESS_BOOKMARK:          return CrossPointSettings::BTN_ACTION_TOGGLE_BOOKMARK;
    case CrossPointSettings::LONG_PRESS_CLIPPING:          return CrossPointSettings::BTN_ACTION_ADD_CLIPPING;
    case CrossPointSettings::LONG_PRESS_CHAPTER_SKIP:      return CrossPointSettings::BTN_ACTION_CHAPTER_SKIP;
    case CrossPointSettings::LONG_PRESS_ORIENTATION_CHANGE:return CrossPointSettings::BTN_ACTION_ORIENTATION;
    case CrossPointSettings::LONG_PRESS_FONTSIZE:          return CrossPointSettings::BTN_ACTION_FONTSIZE;
    case CrossPointSettings::LONG_PRESS_DICTIONARY:        return CrossPointSettings::BTN_ACTION_DICTIONARY;
    case CrossPointSettings::LONG_PRESS_DARK_MODE:         return CrossPointSettings::BTN_ACTION_DARK_MODE;
    case CrossPointSettings::LONG_PRESS_FULL_REFRESH:      return CrossPointSettings::BTN_ACTION_FULL_REFRESH;
    case CrossPointSettings::LONG_PRESS_READER_SETTINGS:   return CrossPointSettings::BTN_ACTION_READER_SETTINGS;
    default: return CrossPointSettings::BTN_ACTION_OFF;
  }
}

inline CrossPointSettings::BUTTON_ACTION legacyFrontLongPressToButtonAction(uint8_t legacy) {
  switch (legacy) {
    case CrossPointSettings::FRONT_LONG_PRESS_OFF:         return CrossPointSettings::BTN_ACTION_OFF;
    case CrossPointSettings::FRONT_LONG_PRESS_BOOKMARK:    return CrossPointSettings::BTN_ACTION_TOGGLE_BOOKMARK;
    case CrossPointSettings::FRONT_LONG_PRESS_CLIPPING:    return CrossPointSettings::BTN_ACTION_ADD_CLIPPING;
    case CrossPointSettings::FRONT_LONG_PRESS_CHAPTER_SKIP:return CrossPointSettings::BTN_ACTION_CHAPTER_SKIP;
    case CrossPointSettings::FRONT_LONG_PRESS_ORIENTATION: return CrossPointSettings::BTN_ACTION_ORIENTATION;
    case CrossPointSettings::FRONT_LONG_PRESS_FONTSIZE:    return CrossPointSettings::BTN_ACTION_FONTSIZE;
    case CrossPointSettings::FRONT_LONG_PRESS_DICTIONARY:  return CrossPointSettings::BTN_ACTION_DICTIONARY;
    case CrossPointSettings::FRONT_LONG_PRESS_DARK_MODE:   return CrossPointSettings::BTN_ACTION_DARK_MODE;
    case CrossPointSettings::FRONT_LONG_PRESS_FULL_REFRESH:return CrossPointSettings::BTN_ACTION_FULL_REFRESH;
    default: return CrossPointSettings::BTN_ACTION_OFF;
  }
}

// Detects if the power button was short-pressed with an action that
// should be dispatched to the reader (not handled by main.cpp).
inline bool wasPowerButtonReaderActionPressed(const MappedInputManager& input) {
  const auto spwbtn = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn);
  return isPowerButtonReaderAction(spwbtn) &&
         input.wasReleased(MappedInputManager::Button::Power);
}

inline bool registerConfirmDoubleClick(bool& waitingForSecondClick, unsigned long& firstClickMs, const unsigned long nowMs) {
  if (waitingForSecondClick && nowMs - firstClickMs <= CONFIRM_DOUBLE_CLICK_MS) {
    waitingForSecondClick = false;
    firstClickMs = 0UL;
    return true;
  }

  waitingForSecondClick = true;
  firstClickMs = nowMs;
  return false;
}

inline bool hasPendingConfirmSingleClickExpired(const bool waitingForSecondClick, const unsigned long firstClickMs,
                                                const unsigned long nowMs) {
  return waitingForSecondClick && nowMs - firstClickMs > CONFIRM_DOUBLE_CLICK_MS;
}

inline bool getConfiguredReaderRefreshMode(HalDisplay::RefreshMode& mode) {
  return SETTINGS.getForcedReaderRefreshMode(mode);
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh,
                                    const bool forceFullRefresh = false) {
  if (forceFullRefresh) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }

  HalDisplay::RefreshMode configuredMode;
  if (getConfiguredReaderRefreshMode(configuredMode)) {
    renderer.displayBuffer(configuredMode);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }

  if (pagesUntilFullRefresh <= 1) {
    if (renderer.isDarkMode()) {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

inline void requestReaderUiTransitionRefresh(GfxRenderer& renderer) {
  if (SETTINGS.darkMode || renderer.isDarkMode()) {
    return;
  }

  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
bool renderTiledGrayscale(GfxRenderer& renderer, const char* tag, RenderFn&& renderFn,
                          TiledGrayscaleTimings* timings = nullptr) {
  if (!renderer.supportsStripGrayscale()) {
    return false;
  }

  constexpr int STRIP_ROWS = 80;
  const int displayHeight = renderer.getDisplayHeight();
  const int displayWidthBytes = renderer.getDisplayWidthBytes();
  const auto heapBefore = MemoryBudget::snapshot();
  auto scratch =
      std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[static_cast<size_t>(displayWidthBytes) * STRIP_ROWS]);
  const auto heapAfterAlloc = MemoryBudget::snapshot();
  if (!scratch) {
    LOG_ERR(tag, "OOM: grayscale strip scratch (%d bytes); falling back to BW snapshot",
            displayWidthBytes * STRIP_ROWS);
    return false;
  }

  auto renderPlane = [&](const GfxRenderer::RenderMode mode, const bool lsbPlane) {
    renderer.setRenderMode(mode);
    for (int y = 0; y < displayHeight; y += STRIP_ROWS) {
      const int rows = std::min(STRIP_ROWS, displayHeight - y);
      {
        GfxStripTargetScope strip(renderer, scratch.get(), y, rows);
        renderer.clearScreen(0x00);
        renderFn();
      }
      renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
    }
  };

  renderPlane(GfxRenderer::GRAYSCALE_LSB, true);
  const uint32_t tGrayLsb = millis();

  renderPlane(GfxRenderer::GRAYSCALE_MSB, false);
  const uint32_t tGrayMsb = millis();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();
  const uint32_t tGrayDisplay = millis();
  renderer.cleanupGrayscaleWithFrameBuffer();
  const uint32_t tCleanup = millis();

  if (timings) {
    timings->grayLsb = tGrayLsb;
    timings->grayMsb = tGrayMsb;
    timings->grayDisplay = tGrayDisplay;
    timings->cleanup = tCleanup;
  }

  const auto heapAfter = MemoryBudget::snapshot();
  LOG_DBG(tag, "Tiled grayscale RAM: scratch=%d free=%u->%u->%u maxAlloc=%u->%u->%u",
          displayWidthBytes * STRIP_ROWS, heapBefore.freeHeap, heapAfterAlloc.freeHeap, heapAfter.freeHeap,
          heapBefore.maxAllocHeap, heapAfterAlloc.maxAllocHeap, heapAfter.maxAllocHeap);
  return true;
}

template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (renderer.isDarkMode()) {
    return;
  }

  if (renderTiledGrayscale(renderer, "READER", renderFn)) {
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

// Indicates which button triggered a long-press action.
// Used for directional actions like font size (Up=increase, Down=decrease).
// For non-directional buttons, use BTN_DIR_NEUTRAL.
enum class ButtonDirection {
  BTN_DIR_UP,      // side Up button — typically increase
  BTN_DIR_DOWN,    // side Down button — typically decrease
  BTN_DIR_LEFT,    // front Left button — typically increase
  BTN_DIR_RIGHT,   // front Right button — typically decrease
  BTN_DIR_NEUTRAL, // power button or select — no direction preference
};

// Returns true if the action is directional (font size, orientation).
inline bool isDirectionalAction(const CrossPointSettings::BUTTON_ACTION action) {
  return action == CrossPointSettings::BTN_ACTION_FONTSIZE ||
         action == CrossPointSettings::BTN_ACTION_ORIENTATION;
}

// For directional actions, returns true if the action should "increase".
// Up/Left = increase, Down/Right = decrease.
inline bool isIncreaseDirection(const ButtonDirection dir) {
  switch (dir) {
    case ButtonDirection::BTN_DIR_UP:
    case ButtonDirection::BTN_DIR_LEFT:
      return true;
    default:
      return false;
  }
}

// Show bookmark toggle feedback, checking achievements first.
// Returns true if an achievement popup was shown (caller should skip its own popup).
inline bool showBookmarkToggleFeedback(GfxRenderer& renderer, bool addedBookmark) {
  const bool showedAchievement = showPendingAchievementPopups(renderer);
  if (!showedAchievement) {
    GUI.drawPopup(renderer, addedBookmark ? tr(STR_BOOKMARK_ADDED) : tr(STR_BOOKMARK_REMOVED));
    renderer.displayBuffer();
    delay(500);
  }
  return showedAchievement;
}

}  // namespace ReaderUtils
