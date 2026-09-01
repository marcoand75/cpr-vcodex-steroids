#include "EndOfBookOptions.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"

namespace {
std::string displayName(const std::string& filename) {
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}
}  // namespace

void EndOfBookOptions::loadOnce(const std::string& currentBookPath) {
  if (isLoaded.load(std::memory_order_acquire)) return;
  folder = FsHelpers::extractFolderPath(currentBookPath);
  names = NextBookFinder::findNextBooks(currentBookPath, MAX_SUGGESTIONS);
  selector = 0;
  isLoaded.store(true, std::memory_order_release);
}

bool EndOfBookOptions::menuActive() const { return isLoaded.load(std::memory_order_acquire) && !names.empty(); }

std::string EndOfBookOptions::fullPath(const size_t index) const {
  if (index >= names.size()) return {};
  return folder == "/" ? "/" + names[index] : folder + "/" + names[index];
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selector < static_cast<int>(names.size())) {
      if (openPath) *openPath = fullPath(selector);
      return Action::OpenBook;
    }
    return Action::GoHome;
  }
  if (input.wasReleased(MappedInputManager::Button::Back) && input.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    return Action::LastPage;
  }

  const int itemCount = static_cast<int>(names.size()) + 1;
  const auto pageTurn = ReaderUtils::detectPageTurn(input);
  if (pageTurn.prev) {
    selector = ButtonNavigator::previousIndex(selector, itemCount);
    return Action::Redraw;
  }
  if (pageTurn.next) {
    selector = ButtonNavigator::nextIndex(selector, itemCount);
    return Action::Redraw;
  }
  return Action::None;
}

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!menuActive()) {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_END_OF_BOOK), true,
                              EpdFontFamily::BOLD);
    return;
  }

  const Rect safe{0, metrics.topPadding, renderer.getScreenWidth(),
                  renderer.getScreenHeight() - metrics.topPadding - metrics.buttonHintsHeight};
  const int titleY = safe.y + safe.height / 8;
  const int subtitleY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  const int listTop = subtitleY + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, subtitleY, tr(STR_EOB_CONTINUE_WITH));

  const int listHeight = safe.y + safe.height - listTop - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{safe.x, listTop, safe.width, listHeight}, static_cast<int>(names.size()) + 1, selector,
               [this](const int index) {
                 return index < static_cast<int>(names.size()) ? displayName(names[index])
                                                               : std::string(tr(STR_EOB_HOME));
               });
  ListRenderHelper::drawHints(renderer, input, tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
}
