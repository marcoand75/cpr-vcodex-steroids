#pragma once

#include <I18n.h>

#include <memory>

#include "activities/Activity.h"

class ClockOffsetActivity final : public Activity {
 public:
  explicit ClockOffsetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockOffset", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int SIGN_INDEX = 0;   // 0 = sign
  static constexpr int HOURS_INDEX = 1;  // 1 = hours (0-13)
  static constexpr int MINS_INDEX = 2;   // 2 = minutes (0-45, step 15)

  int selectedField = 0;

  int sign = 1;           // +1 or -1
  int hours = 0;          // 0..13
  int minutes = 0;        // 0, 15, 30, 45

  int calcOffsetQ() const;
  void loadFromSettings();
  void saveToSettings() const;
  void clampForSign();
};
