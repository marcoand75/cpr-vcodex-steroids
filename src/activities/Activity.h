#pragma once
#include <Logging.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "SilentRestart.h"
#include "util/ScreenshotInfo.h"

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

 public:
  static constexpr uint8_t UI_TRANSITION_REFRESH_WEIGHT_NONE = 0;
  static constexpr uint8_t UI_TRANSITION_REFRESH_WEIGHT_DENSE = 2;

  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  // Exclusive storage activities suspend global controls and normal activity
  // transitions so no filesystem code races a raw SD-card owner.
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  virtual bool isReaderActivity() const { return false; }
  // Returns true when the activity schedules a reader-aware forced refresh.
  virtual bool handleForcedRefresh() { return false; }
  virtual uint8_t getUiTransitionRefreshWeight() const { return UI_TRANSITION_REFRESH_WEIGHT_NONE; }
  virtual bool isHomeActivity() const { return false; }
  // Steroids fork-only: true once the current activity finished its
  // memory-heavy boot work (Home: cover generation). Heavy store loads
  // (reading stats, achievements) must wait for it so they cannot collapse the
  // largest free heap block before covers are generated.
  virtual bool deferredStoreLoadReady() const { return true; }
  // Steroids fork-only: marks the ScreenSaver activity so the power-button
  // state machine can treat its button edges specially.
  virtual bool isScreenSaverActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  /// Release temporary memory that is not needed while this activity is in the
  /// background (under a reader or another pushed activity). Default: no-op.
  /// Called by ActivityManager::pushActivity() before the new activity runs.
  virtual void freeBackgroundMemory() {}

  // ---- Fast silent restart on exit (Steroids fork) --------------------------
  // Declarative exit-restart policy ("simple code parameter"): activities that
  // fragment the heap heavily (WiFi sessions, big list browsing, the Lua VM)
  // return to their origin with a fast silent reboot — the boot skips the
  // splash and lands straight on the declared exit destination. Disabled by
  // default: an activity behaves exactly as before until it opts in by
  // overriding exitRestartPlan() and calling exitWithFastRestart() at the exit
  // points that previously performed the plain activity swap.
  virtual ExitRestartPlan exitRestartPlan() const { return {}; }

  // Fast-restart exit: reboots landing on exitRestartPlan().landing (seamless
  // per the plan). Never returns once the reboot starts; returns without any
  // side effect when the plan is disabled (the caller's plain exit path runs).
  void exitWithFastRestart();

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);
  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience method to facilitate API transition to ActivityManager
  // TODO: remove this in near future
  void onGoHome(HomeMenuItem item = HomeMenuItem::NONE);
  void onSelectBook(const std::string& path);

  // Additive accessor used by shared UI helpers (e.g. PopupUtils) that are
  // given an Activity& and need the renderer without friending every caller.
  GfxRenderer& getRenderer() { return renderer; }
  const GfxRenderer& getRenderer() const { return renderer; }
};
