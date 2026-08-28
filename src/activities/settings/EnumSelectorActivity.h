#pragma once

#include <string>
#include <vector>

#include "I18nKeys.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "CrossPointSettings.h"

/**
 * Reusable popup-style list selector for enum settings.
 *
 * Shows all enum options in a vertically scrollable panel, each on its own row,
 * with the current value highlighted. The user navigates with Up/Down (side buttons)
 * and confirms with the Select button. The selected index (enum value) is returned
 * as the result via ActivityResult::PageResult::page.
 *
 * Usage:
 *   startActivityForResult(
 *       std::make_unique<EnumSelectorActivity>(renderer, mappedInput, currentValue, optionLabels),
 *       [this](const ActivityResult& result) {
 *         if (!result.isCancelled) {
 *           SETTINGS.someEnumSetting = static_cast<SomeEnumType>(std::get<PageResult>(result.data).page);
 *           SETTINGS.saveToFile();
 *         }
 *       });
 */
class EnumSelectorActivity final : public Activity {
public:
    explicit EnumSelectorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  uint8_t currentValue, const std::vector<StrId>& optionLabels)
        : Activity("EnumSelector", renderer, mappedInput),
          currentValue(currentValue),
          optionLabels(optionLabels) {
        ButtonNavigator::setMappedInputManager(mappedInput);
    }

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

private:
    uint8_t currentValue;  // Currently selected value
    std::vector<StrId> optionLabels;
    int selectedIndex = 0;
    int startIndex = 0;    // First visible item in the scrolled list
    ButtonNavigator buttonNavigator;

    void confirmSelection();
    void cancelSelection();
};