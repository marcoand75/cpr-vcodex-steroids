#include "Activity.h"

#include "ActivityManager.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

void Activity::exitWithFastRestart() {
  const ExitRestartPlan plan = exitRestartPlan();
  if (!plan.enabled) {
    // Opt-out (e.g. an in-process plugin): fall through so the caller's plain
    // exit path runs instead.
    LOG_DBG("ACT", "Fast-restart exit requested but the plan is disabled for '%s'", name.c_str());
    return;
  }
  silentRestartTo(plan.landing, plan.seamless);
  // Unreachable once the reboot starts: ESP.restart() resets the CPU.
}
