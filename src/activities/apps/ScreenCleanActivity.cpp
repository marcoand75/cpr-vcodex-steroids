#include "ScreenCleanActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ACTION_COUNT = 2;
constexpr uint32_t STAGE_HOLD_MS = 450;
constexpr int CHECKER_TILE_SIZE = 32;

const char* titleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK) : tr(STR_SCREEN_CLEAN_DEEP);
}

const char* subtitleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK_DESC) : tr(STR_SCREEN_CLEAN_DEEP_DESC);
}

}  // namespace

void ScreenCleanActivity::onBack(void* ctx) {
  auto* self = static_cast<ScreenCleanActivity*>(ctx);
  if (self->cleaning) {
    self->finishCleaning(false);
  } else {
    self->finish();
  }
}

void ScreenCleanActivity::onConfirm(void* ctx) {
  auto* self = static_cast<ScreenCleanActivity*>(ctx);
  self->startCleaning(self->selectedIndex == 0 ? Mode::Quick : Mode::Deep);
}

void ScreenCleanActivity::onNav(void* ctx, int delta) {
  auto* self = static_cast<ScreenCleanActivity*>(ctx);
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, ACTION_COUNT);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, ACTION_COUNT);
  }
  self->requestUpdate();
}

void ScreenCleanActivity::onEnter() {
  Activity::onEnter();
  cleaning = false;
  completed = false;
  selectedIndex = 0;

  listInputMapper.setBackHandler(onBack, this, false);
  listInputMapper.setConfirmHandler(onConfirm, this, false);
  listInputMapper.setNavHandlers(nullptr, onNav, nullptr, this);

  requestUpdate();
}

void ScreenCleanActivity::onExit() {
  restoreDarkMode();
  Activity::onExit();
}

void ScreenCleanActivity::restoreDarkMode() {
  if (!darkModeSaved) {
    return;
  }
  renderer.setDarkMode(savedDarkMode);
  darkModeSaved = false;
}

void ScreenCleanActivity::startCleaning(const Mode cleanMode) {
  completed = false;
  mode = cleanMode;
  stageIndex = 0;
  cleaning = true;
  savedDarkMode = renderer.isDarkMode();
  darkModeSaved = true;
  renderer.setDarkMode(false);
  requestUpdateAndWait();
}

void ScreenCleanActivity::finishCleaning(const bool markCompleted) {
  cleaning = false;
  completed = markCompleted;
  restoreDarkMode();
  renderer.requestNextFullRefresh();
  requestUpdateAndWait();
}

int ScreenCleanActivity::stageCount() const { return mode == Mode::Quick ? 5 : 11; }

ScreenCleanActivity::Pattern ScreenCleanActivity::patternForStage(const uint8_t index) const {
  static constexpr Pattern QUICK_SEQUENCE[] = {
      Pattern::White, Pattern::Black, Pattern::White, Pattern::Black, Pattern::White,
  };
  static constexpr Pattern DEEP_SEQUENCE[] = {
      Pattern::White,   Pattern::Black, Pattern::White, Pattern::Checker, Pattern::InverseChecker, Pattern::LightGray,
      Pattern::DarkGray, Pattern::Black, Pattern::White, Pattern::Black,   Pattern::White,
  };

  if (mode == Mode::Quick) {
    return QUICK_SEQUENCE[index % (sizeof(QUICK_SEQUENCE) / sizeof(QUICK_SEQUENCE[0]))];
  }
  return DEEP_SEQUENCE[index % (sizeof(DEEP_SEQUENCE) / sizeof(DEEP_SEQUENCE[0]))];
}

void ScreenCleanActivity::drawPattern(const Pattern pattern) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  switch (pattern) {
    case Pattern::White:
      renderer.clearScreen(0xFF);
      break;
    case Pattern::Black:
      renderer.clearScreen(0x00);
      break;
    case Pattern::LightGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::LightGray);
      break;
    case Pattern::DarkGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::DarkGray);
      break;
    case Pattern::Checker:
    case Pattern::InverseChecker: {
      renderer.clearScreen(0xFF);
      const bool inverse = pattern == Pattern::InverseChecker;
      for (int y = 0; y < height; y += CHECKER_TILE_SIZE) {
        for (int x = 0; x < width; x += CHECKER_TILE_SIZE) {
          const bool fillBlack = (((x / CHECKER_TILE_SIZE) + (y / CHECKER_TILE_SIZE)) & 1) != (inverse ? 0 : 1);
          if (!fillBlack) {
            continue;
          }
          const int tileWidth = std::min(CHECKER_TILE_SIZE, width - x);
          const int tileHeight = std::min(CHECKER_TILE_SIZE, height - y);
          renderer.fillRect(x, y, tileWidth, tileHeight);
        }
      }
      break;
    }
  }
}

void ScreenCleanActivity::loop() {
  if (cleaning) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishCleaning(false);
      return;
    }

    if (millis() - lastStageRenderedAt < STAGE_HOLD_MS) {
      return;
    }

    stageIndex++;
    if (stageIndex >= stageCount()) {
      finishCleaning(true);
      return;
    }

    requestUpdateAndWait();
    return;
  }

  listInputMapper.loop(mappedInput);
}

void ScreenCleanActivity::render(RenderLock&&) {
  if (cleaning) {
    drawPattern(patternForStage(stageIndex));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    lastStageRenderedAt = millis();
    return;
  }

  renderer.clearScreen();

  const auto layout = ListLayout::compute(renderer);

  ListRenderHelper::drawHeader(renderer, tr(STR_SCREEN_CLEAN), nullptr, true);

  ListRenderHelper::drawList(
      renderer, layout, ACTION_COUNT, selectedIndex,
      [](const int index) { return std::string(titleForAction(index)); },
      [](const int index) { return std::string(subtitleForAction(index)); },
      [](const int) { return UIIcon::Image; });

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  if (completed) {
    GUI.drawPopup(renderer, tr(STR_SCREEN_CLEAN_DONE));
    return;
  }
  renderer.displayBuffer();
}
