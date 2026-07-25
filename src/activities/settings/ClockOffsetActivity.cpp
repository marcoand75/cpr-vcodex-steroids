#include "ClockOffsetActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cmath>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClockOffsetActivity::onEnter() {
  Activity::onEnter();
  loadFromSettings();
}

void ClockOffsetActivity::loadFromSettings() {
  int totalQ = static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48;  // un-bias
  sign = (totalQ >= 0) ? 1 : -1;
  totalQ = std::abs(totalQ);
  hours = totalQ / 4;
  minutes = (totalQ % 4) * 15;
}

void ClockOffsetActivity::saveToSettings() const {
  SETTINGS.clockUtcOffsetQ = static_cast<uint8_t>(calcOffsetQ());
}

int ClockOffsetActivity::calcOffsetQ() const {
  int totalQ = hours * 4 + minutes / 15;
  return sign * totalQ + 48;  // re-bias
}

void ClockOffsetActivity::clampForSign() {
  const int maxHours = (sign >= 0) ? 14 : 12;
  if (hours > maxHours) hours = maxHours;
}

void ClockOffsetActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveToSettings();
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectedField = (selectedField + 2) % 3;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedField = (selectedField + 1) % 3;
    requestUpdate();
    return;
  }

  const int dir = (mappedInput.wasReleased(MappedInputManager::Button::Up) ? 1 : 0) -
                  (mappedInput.wasReleased(MappedInputManager::Button::Down) ? 1 : 0);
  if (dir != 0) {
    switch (selectedField) {
      case SIGN_INDEX:
        sign = -sign;
        clampForSign();
        break;
      case HOURS_INDEX:
        hours = std::max(0, std::min(14, hours + dir));
        break;
      case MINS_INDEX:
        minutes = std::max(0, std::min(45, minutes + dir * 15));
        break;
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    saveToSettings();
    finish();
  }
}

void ClockOffsetActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_CLOCK_UTC_OFFSET));

  const int y0 = metrics.topPadding + metrics.headerHeight + 40;
  const int xCenter = renderer.getScreenWidth() / 2;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);

  // Sign
  const char* signStr = (sign >= 0) ? "+" : "-";
  bool sel = selectedField == SIGN_INDEX;
  if (sel) renderer.fillRect(xCenter - 30, y0 - 5, 60, lineH + 10, true);
  renderer.drawText(SMALL_FONT_ID, xCenter - 8, y0, signStr, !sel);

  // Hours
  char hoursStr[8];
  snprintf(hoursStr, sizeof(hoursStr), "%d", hours);
  sel = selectedField == HOURS_INDEX;
  if (sel) renderer.fillRect(xCenter - 30, y0 + lineH + 15 - 5, 60, lineH + 10, true);
  renderer.drawText(SMALL_FONT_ID, xCenter - 8, y0 + lineH + 15, hoursStr, !sel);

  // Minutes
  char minsStr[8];
  snprintf(minsStr, sizeof(minsStr), ":%02d", minutes);
  sel = selectedField == MINS_INDEX;
  if (sel) renderer.fillRect(xCenter - 30, y0 + 2 * (lineH + 15) - 5, 60, lineH + 10, true);
  renderer.drawText(SMALL_FONT_ID, xCenter - 8, y0 + 2 * (lineH + 15), minsStr, !sel);

  renderer.displayBuffer();
}
