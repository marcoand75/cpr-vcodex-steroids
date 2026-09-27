#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ShortcutRegistry.h"

// Apps hub: the entry point for every fork app. A plain tappable list of the
// configured app shortcuts (name, description, icon); Back goes Home.
class AppsActivity final : public UiListActivity {
  std::vector<const ShortcutDefinition*> appShortcuts;
  // Row text storage: the ListItem rows point into these, so they live as
  // long as the rows do and are rebuilt together (rebuildRows()).
  std::vector<std::string> shortcutNames;
  std::vector<std::string> shortcutSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;

  void loadShortcuts();
  void rebuildRows();
  void openApp(int index);

  int listCount() const override { return static_cast<int>(appShortcuts.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Standardized fast-restart exit: leaving the apps hub reboots and lands on
  // Home with a clean heap (falls through to the plain pop when the plan is
  // disabled at runtime).
  void onBackButton() override {
    exitWithFastRestart();
    onGoHome();
  }
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Apps", renderer, mappedInput) {
    chromeNeedsListLayout = true;  // header shows "<page>/<pages> | <count>"
  }

  void onEnter() override;
  void onExit() override;

  // Standardized fast-restart exit policy (see onBackButton()).
  ExitRestartPlan exitRestartPlan() const override {
    return {true, RestartLanding::Home, /*seamless=*/true};
  }
};
