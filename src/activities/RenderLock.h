#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
//
// Acquire the lock around any code path that touches the renderer, calls
// GUI.* drawing primitives, or reads currentActivity on a non-loop thread.
// On the main loop, the activity is responsible for taking the lock
// (ActivityManager::loop / render() always lock before currentActivity is
// dereferenced).
//
// The Activity& constructor is the canonical form for activity-internal
// helper functions (PopupUtils::showTransientPopup, DictionaryActivity
// progress callback, etc.) — it expresses "lock for the duration of this
// activity's draw call". The default constructor is for top-level paths
// (main loop shutdown screen, screenshot, etc.) that do not have an
// Activity reference handy.
class RenderLock {
  bool isLocked = false;

 public:
  explicit RenderLock();
  explicit RenderLock(Activity&);
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  static bool peek();
};
