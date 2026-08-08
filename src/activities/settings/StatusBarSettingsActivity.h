#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <vector>

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"
#include "I18nKeys.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public Activity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StatusBarSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  int selectedIndex = 0;

  // Filtered menu: excludes clock items (indices 7-9) on X4 devices without RTC.
  std::vector<StrId> menuNames;
  int menuItemCount() const { return static_cast<int>(menuNames.size()); }

  void handleSelection();

 private:
  ListInputMapper listInputMapper;

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);
};
