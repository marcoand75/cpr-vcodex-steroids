#pragma once

// Centralized popup / toast helpers for activities.
//
// Usage:
//   PopupUtils::showTransientPopup(*this, tr(STR_LOADING), 20, 120);
//   PopupUtils::showTimerPauseFeedback(renderer, nowPaused);
//   PopupUtils::showErrorToast(renderer, tr(STR_ERROR_GENERAL_FAILURE), 500);

#include <cstdint>

#include <I18n.h>
#include "activities/Activity.h"
#include "components/UITheme.h"

namespace PopupUtils {

// Show a transient popup with optional progress bar. This helper performs the
// full requestUpdateAndWait + render + optional delay cycle used by
// SettingsActivity, ReadingStatsActivity and SyncDayActivity.
inline void showTransientPopup(Activity& activity, const char* message, int progress = -1,
                               unsigned long delayMs = 0) {
  activity.requestUpdateAndWait();
  {
    RenderLock lock(activity);
    const Rect popupRect = GUI.drawPopup(activity.getRenderer(), message);
    if (progress >= 0) {
      GUI.fillPopupProgress(activity.getRenderer(), popupRect, progress);
    }
  }
  if (delayMs > 0) {
    delay(delayMs);
  }
}

// Show a brief toast-style popup for timer toggle feedback in reader activities.
inline void showTimerPauseFeedback(GfxRenderer& renderer, bool nowPaused, unsigned long delayMs = 500) {
  GUI.drawPopup(renderer, nowPaused ? tr(STR_READING_TIMER_PAUSED) : tr(STR_READING_TIMER_ACTIVE));
  renderer.displayBuffer();
  delay(delayMs);
}

// Show a generic error toast with a default 500ms delay.
inline void showErrorToast(GfxRenderer& renderer, const char* message, unsigned long delayMs = 500) {
  GUI.drawPopup(renderer, message);
  renderer.displayBuffer();
  delay(delayMs);
}

}  // namespace PopupUtils
