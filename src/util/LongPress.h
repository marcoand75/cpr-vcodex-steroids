#pragma once

#include <cstdint>

// Long-press timing constants and per-button state machine.
//
// Multiple activities duplicate the same "isPressed + getHeldTime + reset on
// release" pattern with different thresholds. This util gives them a single
// home for the constants and a tiny state machine for one button.
//
// Thresholds are conservative defaults: long press around 800-1000ms is
// the standard "user meant to act" window across the codebase. Activities
// that need a different threshold (e.g. LibraryContextMenu = 1500ms) can
// override the constant locally.

namespace long_press {

// Default long-press threshold in milliseconds. Used by the per-button
// state machine below.
inline constexpr uint32_t kDefaultMs = 800;

// Single-button long-press detector. Holds the "user has been holding this
// button long enough to fire" state so callers do not need a per-button
// held/longTriggered pair.
//
// Usage:
//   long_press::Button up;
//   if (mappedInput.isPressed(MappedInputManager::Button::Up)) {
//     if (up.armed() && up.fired(mappedInput.getHeldTime())) {
//       // ... handle long press
//     }
//   } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
//     if (up.wasShortPress()) { /* handle tap */ }
//     up.reset();
//   }
class Button {
 public:
  // Arm the detector (or refresh arm on continued press). Call this when
  // the button is currently held and you want to give the user a fresh
  // long-press window. Does not reset the fired state, so fired() still
  // returns false after the threshold is reached.
  void arm() {
    armed_ = true;
  }

  // Mark the detector as "no longer pressed". Call on wasReleased.
  void reset() {
    armed_ = false;
    fired_ = false;
  }

  // Has the detector been armed (i.e. is the button currently considered
  // held) and not yet fired?
  bool armed() const { return armed_ && !fired_; }

  // Has the long-press already fired for the current hold?
  bool hasFired() const { return fired_; }

  // Was the detector armed or fired during the current press cycle?
  // Useful for detecting "any directional button was held" without
  // exposing internal state.
  bool wasPressed() const { return armed_ || fired_; }

  // Atomically check the held-time threshold and fire. Returns true exactly
  // once per hold (subsequent calls in the same hold return false). The
  // caller is responsible for calling arm() when the button is pressed.
  bool fired(unsigned long heldMs, uint32_t thresholdMs = kDefaultMs) {
    if (!armed_ || fired_) return false;
    if (heldMs < thresholdMs) return false;
    fired_ = true;
    return true;
  }

  // Convenience: returns true if the user released the button WITHOUT the
  // long-press firing (i.e. a "short press"). Pair with fired() / reset().
  bool wasShortPress() const { return armed_ && !fired_; }

 private:
  bool armed_ = false;
  bool fired_ = false;
};

}  // namespace long_press
