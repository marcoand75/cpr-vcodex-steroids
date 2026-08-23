#pragma once

#include <string>
#include <vector>

#include "I18nKeys.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "CrossPointSettings.h"

/**
 * Reusable popup-style list selector for button action enum values.
 *
 * Mode BUTTON_ACTION: Shows all 14 BUTTON_ACTION options in a vertically
 * scrollable panel, each on its own row, with the current value highlighted.
 * The user navigates with Up/Down (side buttons) and confirms with the Select
 * button. The selected index (BUTTON_ACTION value) is returned as the result
 * via ActivityResult::PageResult::page.
 *
 * Mode SHORT_PWRBTN: Shows all 16 SHORT_PWRBTN options (5 legacy power-button
 * actions at indices 0-4, then 11 BUTTON_ACTION-equivalent actions at 5-15).
 * Returns the SHORT_PWRBTN index directly.
 *
 * Usage:
 *   startActivityForResult(
 *       std::make_unique<ButtonActionSelectorActivity>(renderer, mappedInput,
 *           currentValue, ButtonActionSelectorActivity::Mode::BUTTON_ACTION),
 *       [this](const ActivityResult& result) {
 *         if (!result.isCancelled) {
 *           SETTINGS.someField = static_cast<uint8_t>(std::get<PageResult>(result.data).page);
 *           SETTINGS.saveToFile();
 *         }
 *       });
 */
class ButtonActionSelectorActivity final : public Activity {
  public:
  enum class Mode {
    BUTTON_ACTION,
    SHORT_PWRBTN,
  };

  explicit ButtonActionSelectorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        uint8_t currentValue, Mode mode = Mode::BUTTON_ACTION)
      : Activity("ButtonActionSelector", renderer, mappedInput),
        currentValue(currentValue),
        mode(mode) {
    ButtonNavigator::setMappedInputManager(mappedInput);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  /**
   * Returns the label StrId for a given BUTTON_ACTION value.
   * Used by callers that need to display the label for a stored value.
   */
  static StrId actionToLabelId(CrossPointSettings::BUTTON_ACTION action);

  /**
   * Returns the vector of all label StrIds for the current mode, in order.
   */
  const std::vector<StrId>& getOptionLabels() const;

  private:
  uint8_t currentValue;  // Currently selected value
  Mode mode;
  int selectedIndex = 0;
  int startIndex = 0;    // First visible item in the scrolled list
  ButtonNavigator buttonNavigator;

  void confirmSelection();
  void cancelSelection();
};
