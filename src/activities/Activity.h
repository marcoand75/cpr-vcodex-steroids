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
  GfxRenderer& getRenderer() const { return renderer; }
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
  virtual bool isReaderActivity() const { return false; }
  virtual bool isScreenSaverActivity() const { return false; }
  virtual bool isWifiActivity() const { return false; }
  virtual uint8_t getUiTransitionRefreshWeight() const { return UI_TRANSITION_REFRESH_WEIGHT_NONE; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  /// Free temporary memory that is not needed while this activity is in the
  /// background (under a reader or screensaver).  Default: no-op.
  /// Called by ActivityManager::pushActivity() before the new activity runs.
  virtual void freeBackgroundMemory() {}

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience forwarders to ActivityManager. Widely used across the
  // codebase (OpdsBookBrowser, RecentBooks, HomeActivity, FileBrowser,
  // ScreenSaverActivity, LibraryActivity, ReadingStatsDetail, etc.) — the
  // methods are intentionally kept on Activity to keep call sites short
  // and to make the activity lifecycle a single point of contact.
  void onGoHome();
  void onSelectBook(const std::string& path);
};
