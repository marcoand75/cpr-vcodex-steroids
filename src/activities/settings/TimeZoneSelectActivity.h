#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "../util/ListInputMapper.h"

class TimeZoneSelectActivity final : public Activity {
 public:
  explicit TimeZoneSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TimeZoneSelect", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedIndex = 0;
  ListInputMapper listInputMapper;

  static void onBack(void* ctx);
  static void onConfirm(void* ctx);
  static void onNav(void* ctx, int delta);
};
