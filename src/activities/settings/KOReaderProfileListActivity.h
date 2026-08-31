#pragma once

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"

/**
 * Activity showing the list of configured KOReader sync profiles.
 * Allows adding new profiles and editing/deleting/activating existing ones.
 * Selecting a profile opens KOReaderProfileEditActivity, which is where a
 * profile is actually made the active one (KOReaderSettingsActivity reads
 * whichever profile is active).
 */
class KOReaderProfileListActivity final : public Activity {
 public:
  explicit KOReaderProfileListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KOReaderProfileList", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ListInputMapper listInputMapper;
  int selectedIndex = 0;

  int getItemCount() const;
  void handleSelection();
};