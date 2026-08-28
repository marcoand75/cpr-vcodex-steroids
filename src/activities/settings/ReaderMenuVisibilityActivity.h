#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/ReaderMenuRegistry.h"

class ReaderMenuVisibilityActivity final : public Activity {
 public:
   explicit ReaderMenuVisibilityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
       : Activity("ReaderMenuVisibility", renderer, mappedInput) {}

   void onEnter() override;
   void loop() override;
   void render(RenderLock&&) override;

 private:
   ButtonNavigator buttonNavigator;
   std::vector<const ReaderMenuItemDefinition*> entries;
   int selectedIndex = 0;
   bool waitForConfirmRelease = false;

   void reloadEntries();
   void toggleSelectedEntry();
};