#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "DictionaryStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int DICTIONARY_ACTION_COUNT = 3;
constexpr int ACTION_DEFINITION_TEXT_SIZE = 0;
constexpr int ACTION_CLEAR_HISTORY = 1;
constexpr int ACTION_SET_LOOKUP_MODE = 2;
}  // namespace

void DictionaryActivity::onEnter() {
  Activity::onEnter();
  DICTIONARIES.ensureScanned();
  const auto activeEntries = DICTIONARIES.getActiveEntries();
  if (!activeEntries.empty()) {
    const std::string firstActivePath = activeEntries[0]->ifoPath;
    const auto& entries = DICTIONARIES.getEntries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
      if (entries[i].ifoPath == firstActivePath) {
        selectedIndex = i + DICTIONARY_ACTION_COUNT;
        requestUpdate(true);
        return;
      }
    }
  }
  selectedIndex = 0;
  requestUpdate(true);
}

void DictionaryActivity::selectCurrent() {
  if (selectedIndex == ACTION_DEFINITION_TEXT_SIZE) {
    const uint8_t nextSize =
        static_cast<uint8_t>((DICTIONARIES.getDefinitionTextSize() + 1) % DictionaryStore::DEF_TEXT_SIZE_COUNT);
    DICTIONARIES.setDefinitionTextSize(nextSize);
    requestUpdate();
    return;
  }
  if (selectedIndex == ACTION_CLEAR_HISTORY) {
    DICTIONARIES.clearHistory();
    GUI.drawPopup(renderer, tr(STR_CLEAR_HISTORY));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(650);
    requestUpdate();
    return;
  }
  if (selectedIndex == ACTION_SET_LOOKUP_MODE) {
    const auto current = DICTIONARIES.getLookupMode();
    const auto next = current == DictionaryStore::LookupMode::Failover ? DictionaryStore::LookupMode::Manual
                                                                      : DictionaryStore::LookupMode::Failover;
    DICTIONARIES.setLookupMode(next);
    GUI.drawPopup(renderer, next == DictionaryStore::LookupMode::Failover ? tr(STR_DICTIONARY_MODE_FAILOVER)
                                                                         : tr(STR_DICTIONARY_MODE_MANUAL));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(900);
    requestUpdate();
    return;
  }

  const auto& entries = DICTIONARIES.getEntries();
  if (selectedIndex < DICTIONARY_ACTION_COUNT || selectedIndex >= static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT)
    return;
  const auto& entry = entries[selectedIndex - DICTIONARY_ACTION_COUNT];
  if (entry.compressed) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_COMPRESSED_UNSUPPORTED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }
  if (entry.missingFiles) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_MISSING_FILES));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }

  const auto& activePaths = DICTIONARIES.getActiveIfoPaths();
  const bool isActive = std::find(activePaths.begin(), activePaths.end(), entry.ifoPath) != activePaths.end();

  if (isActive) {
    std::vector<std::string> newActivePaths;
    for (const auto& p : activePaths) {
      if (p != entry.ifoPath) newActivePaths.push_back(p);
    }
    DICTIONARIES.setActiveIfoPaths(newActivePaths);
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_DEACTIVATED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(900);
    requestUpdate();
  } else {
    std::vector<std::string> newActivePaths = activePaths;
    newActivePaths.push_back(entry.ifoPath);
    DICTIONARIES.setActiveIfoPaths(newActivePaths);

    Rect popup;
    {
      RenderLock lock(*this);
      popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_PREPARING));
    }
    const bool ready = DICTIONARIES.prepareEntry(entry.ifoPath, [this, &popup](int percent) {
      RenderLock lock(*this);
      GUI.fillPopupProgress(renderer, popup, percent);
    });
    GUI.drawPopup(renderer, ready ? tr(STR_DICTIONARY_READY) : tr(STR_DICTIONARY_PREPARE_FAILED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(900);
    requestUpdate();
  }
}

void DictionaryActivity::moveActiveDict(const int delta) {
  const auto& entries = DICTIONARIES.getEntries();
  if (selectedIndex < DICTIONARY_ACTION_COUNT || selectedIndex >= static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT)
    return;

  const int entryIndex = selectedIndex - DICTIONARY_ACTION_COUNT;
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries.size())) return;

  std::vector<std::string> ifoPaths;
  ifoPaths.reserve(entries.size());
  for (const auto& entry : entries) {
    ifoPaths.push_back(entry.ifoPath);
  }

  const int currentPos = entryIndex;
  const int newPos = currentPos + delta;
  if (newPos < 0 || newPos >= static_cast<int>(ifoPaths.size())) return;

  std::swap(ifoPaths[currentPos], ifoPaths[newPos]);
  DICTIONARIES.setEntriesOrder(ifoPaths);
  selectedIndex = newPos + DICTIONARY_ACTION_COUNT;
  requestUpdate();
}

void DictionaryActivity::loop() {
  const auto& entries = DICTIONARIES.getEntries();
  const int totalItems = static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (orderingMode) {
      orderingMode = false;
      DICTIONARIES.scan();
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  if (orderingMode) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      moveActiveDict(-1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      moveActiveDict(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      DICTIONARIES.saveConfig();
      orderingMode = false;
      GUI.drawPopup(renderer, tr(STR_DICTIONARY_ORDER_SAVED));
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      delay(700);
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= 500) {
      orderingMode = true;
      GUI.drawPopup(renderer, tr(STR_DICTIONARY_ORDER_MODE));
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      delay(700);
      requestUpdate();
      return;
    }
    selectCurrent();
    return;
  }

  buttonNavigator.onNext([this, totalItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this, totalItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
      requestUpdate();
    }
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    if (totalItems > 0) {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
      requestUpdate();
    }
  });
}

void DictionaryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& entries = DICTIONARIES.getEntries();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const auto& activePaths = DICTIONARIES.getActiveIfoPaths();
  std::string activeLabel;
  if (!activePaths.empty()) {
    const auto& allEntries = DICTIONARIES.getEntries();
    for (const auto& p : activePaths) {
      for (const auto& e : allEntries) {
        if (e.ifoPath == p) {
          if (!activeLabel.empty()) activeLabel += ", ";
          activeLabel += e.languageId;
          break;
        }
      }
    }
  }
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_DICTIONARY),
                                      activeLabel.empty() ? nullptr : activeLabel.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  auto textSizeLabel = []() -> const char* {
    switch (DICTIONARIES.getDefinitionTextSize()) {
      case DictionaryStore::DEF_TEXT_SMALL:
        return tr(STR_SMALL);
      case DictionaryStore::DEF_TEXT_LARGE:
        return tr(STR_LARGE);
      default:
        return tr(STR_SMALL);
    }
  };

  auto lookupModeLabel = []() -> const char* {
    switch (DICTIONARIES.getLookupMode()) {
      case DictionaryStore::LookupMode::Failover:
        return tr(STR_DICTIONARY_MODE_FAILOVER);
      case DictionaryStore::LookupMode::Manual:
        return tr(STR_DICTIONARY_MODE_MANUAL);
      default:
        return tr(STR_DICTIONARY_MODE_FAILOVER);
    }
  };

  auto isEntryActive = [&activePaths](const DictionaryEntry& entry) -> bool {
    return std::find(activePaths.begin(), activePaths.end(), entry.ifoPath) != activePaths.end();
  };

  if (entries.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, metrics.listRowHeight * DICTIONARY_ACTION_COUNT},
        DICTIONARY_ACTION_COUNT, selectedIndex,
        [](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(tr(STR_DEFINITION_TEXT_SIZE));
          if (index == ACTION_CLEAR_HISTORY) return std::string(tr(STR_CLEAR_HISTORY));
          return std::string(tr(STR_DICTIONARY_LOOKUP_MODE));
        },
        nullptr, nullptr,
        [&textSizeLabel, lookupModeLabel](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(textSizeLabel());
          if (index == ACTION_SET_LOOKUP_MODE) return std::string(lookupModeLabel());
          return std::string();
        },
        true);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + metrics.listRowHeight * DICTIONARY_ACTION_COUNT + 22,
                              tr(STR_NO_DICTIONARIES));
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + metrics.listRowHeight * DICTIONARY_ACTION_COUNT + 48,
                              "/dictionaries/<language>/");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(entries.size()) + DICTIONARY_ACTION_COUNT, selectedIndex,
        [&entries](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(tr(STR_DEFINITION_TEXT_SIZE));
          if (index == ACTION_CLEAR_HISTORY) return std::string(tr(STR_CLEAR_HISTORY));
          if (index == ACTION_SET_LOOKUP_MODE) return std::string(tr(STR_DICTIONARY_LOOKUP_MODE));
          return entries[index - DICTIONARY_ACTION_COUNT].languageId;
        },
        [&entries](int index) {
          if (index < DICTIONARY_ACTION_COUNT) return std::string();
          return entries[index - DICTIONARY_ACTION_COUNT].name;
        },
        nullptr,
        [&entries, &textSizeLabel, lookupModeLabel, isEntryActive](int index) {
          if (index == ACTION_DEFINITION_TEXT_SIZE) return std::string(textSizeLabel());
          if (index == ACTION_CLEAR_HISTORY) return std::string();
          if (index == ACTION_SET_LOOKUP_MODE) return std::string(lookupModeLabel());
          const int entryIndex = index - DICTIONARY_ACTION_COUNT;
          if (entries[entryIndex].compressed) return std::string("ZIP");
          if (entries[entryIndex].missingFiles) return std::string("!");
          return isEntryActive(entries[entryIndex]) ? std::string(tr(STR_DICTIONARY_ACTIVE)) : std::string();
        },
        true);
  }

  if (orderingMode) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
