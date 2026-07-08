#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reusable input mapper for list-based activities. Uses raw function pointers
// (not std::function) to minimize per-instance memory overhead on ESP32-C3.
//
// Usage:
//   static void onBack(void* ctx) { static_cast<MyActivity*>(ctx)->finish(); }
//   static void onConfirm(void* ctx) { static_cast<MyActivity*>(ctx)->doAction(); }
//   static void onNav(void* ctx, int delta) { ... }
//
//   listInputMapper.setBackHandler(onBack, this);
//   listInputMapper.setConfirmHandler(onConfirm, this);
//   listInputMapper.setNavHandlers(onNavPress, onNavRelease, onNavContinuous, this);
//   listInputMapper.loop(mappedInput);
class ListInputMapper {
 public:
  using BackFn = void (*)(void* ctx);
  using ConfirmFn = void (*)(void* ctx);
  using NavFn = void (*)(void* ctx, int delta);

  // Configure Back behavior. If useRelease is true, triggers on wasReleased;
  // otherwise on wasPressed.
  void setBackHandler(BackFn handler, void* ctx, bool useRelease = true) {
    backHandler_ = handler;
    backCtx_ = ctx;
    backUseRelease_ = useRelease;
  }

  // Configure Confirm behavior. If useRelease is true, triggers on wasReleased;
  // otherwise on wasPressed.
  void setConfirmHandler(ConfirmFn handler, void* ctx, bool useRelease = true) {
    confirmHandler_ = handler;
    confirmCtx_ = ctx;
    confirmUseRelease_ = useRelease;
  }

  // Configure navigation. delta is +1 or -1 for press/release, +pageItems or -pageItems for continuous.
  // Pass nullptr for unused modes (e.g. pressNav=nullptr if you only use release-based nav).
  void setNavHandlers(NavFn pressFn, NavFn releaseFn, NavFn continuousFn, void* ctx) {
    pressNav_ = pressFn;
    releaseNav_ = releaseFn;
    continuousNav_ = continuousFn;
    navCtx_ = ctx;
  }

  // Convenience: set only press + continuous (no release).
  void setNavPressAndContinuous(NavFn pressFn, NavFn continuousFn, void* ctx) {
    setNavHandlers(pressFn, nullptr, continuousFn, ctx);
  }

  // Convenience: set only release + continuous (common for app/reader activities).
  void setNavReleaseAndContinuous(NavFn releaseFn, NavFn continuousFn, void* ctx) {
    setNavHandlers(nullptr, releaseFn, continuousFn, ctx);
  }

  // Convenience: set all three nav modes to the same handler.
  void setNavAll(NavFn fn, void* ctx) { setNavHandlers(fn, fn, fn, ctx); }

  // Process input for one frame. Call this from the activity's loop().
  void loop(MappedInputManager& input) {
    if (backHandler_) {
      const bool triggered = backUseRelease_ ? input.wasReleased(MappedInputManager::Button::Back)
                                            : input.wasPressed(MappedInputManager::Button::Back);
      if (triggered) {
        backHandler_(backCtx_);
        return;
      }
    }

    if (confirmHandler_) {
      const bool triggered = confirmUseRelease_ ? input.wasReleased(MappedInputManager::Button::Confirm)
                                               : input.wasPressed(MappedInputManager::Button::Confirm);
      if (triggered) {
        confirmHandler_(confirmCtx_);
        return;
      }
    }

    if (pressNav_) {
      buttonNavigator_.onNextPress([this]() {
        if (pressNav_) pressNav_(navCtx_, 1);
      });
      buttonNavigator_.onPreviousPress([this]() {
        if (pressNav_) pressNav_(navCtx_, -1);
      });
    }

    if (releaseNav_) {
      buttonNavigator_.onNextRelease([this]() {
        if (releaseNav_) releaseNav_(navCtx_, 1);
      });
      buttonNavigator_.onPreviousRelease([this]() {
        if (releaseNav_) releaseNav_(navCtx_, -1);
      });
    }

    if (continuousNav_) {
      buttonNavigator_.onNextContinuous([this]() {
        if (continuousNav_) continuousNav_(navCtx_, 1);
      });
      buttonNavigator_.onPreviousContinuous([this]() {
        if (continuousNav_) continuousNav_(navCtx_, -1);
      });
    }
  }

 private:
  ButtonNavigator buttonNavigator_;
  BackFn backHandler_ = nullptr;
  ConfirmFn confirmHandler_ = nullptr;
  NavFn pressNav_ = nullptr;
  NavFn releaseNav_ = nullptr;
  NavFn continuousNav_ = nullptr;
  void *backCtx_ = nullptr;
  void *confirmCtx_ = nullptr;
  void *navCtx_ = nullptr;
  bool backUseRelease_ = true;
  bool confirmUseRelease_ = true;
};
