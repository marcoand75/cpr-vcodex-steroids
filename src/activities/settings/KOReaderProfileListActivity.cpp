#include "KOReaderProfileListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderProfileEditActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"

int KOReaderProfileListActivity::getItemCount() const {
  const int profileCount = static_cast<int>(KOREADER_STORE.getCount());
  return profileCount + (KOREADER_STORE.canAddProfile() ? 1 : 0);
}

void KOReaderProfileListActivity::onEnter() {
  Activity::onEnter();

  // Reload from disk in case profiles were added/removed by a subactivity or the web UI
  KOREADER_STORE.loadFromFile();
  selectedIndex = 0;
  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<KOReaderProfileListActivity*>(ctx);
    self->finish();
  }, this, false);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<KOReaderProfileListActivity*>(ctx);
    self->handleSelection();
  }, this, false);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<KOReaderProfileListActivity*>(ctx);
    const int itemCount = self->getItemCount();
    if (itemCount <= 0) return;
    if (delta > 0) {
      self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, itemCount);
    } else {
      self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, itemCount);
    }
    self->requestUpdate();
  };

  listInputMapper.setNavAll(onNav, this);
}

void KOReaderProfileListActivity::onExit() { Activity::onExit(); }

void KOReaderProfileListActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void KOReaderProfileListActivity::handleSelection() {
  const auto profileCount = static_cast<int>(KOREADER_STORE.getCount());

  auto resultHandler = [this](const ActivityResult&) {
    // Reload profile list when returning from the editor (covers add/edit/delete/set-active)
    KOREADER_STORE.loadFromFile();
    selectedIndex = 0;
  };

  if (selectedIndex < profileCount) {
    startActivityForResult(std::make_unique<KOReaderProfileEditActivity>(renderer, mappedInput, selectedIndex),
                           resultHandler);
  } else if (KOREADER_STORE.canAddProfile()) {
    startActivityForResult(std::make_unique<KOReaderProfileEditActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void KOReaderProfileListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_KOREADER_PROFILES));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = getItemCount();

  const auto& profiles = KOREADER_STORE.getProfiles();
  const auto profileCount = static_cast<int>(profiles.size());
  const int activeIndex = KOREADER_STORE.getActiveIndex();

  // Primary label: profile name (falling back to username if unnamed).
  // Secondary label: username (shown as subtitle when name is set).
  // Value column: an "Active" tag on whichever profile is currently active.
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
      [&profiles, profileCount](int index) {
        if (index < profileCount) {
          const auto& profile = profiles[index];
          return profile.name.empty() ? profile.username : profile.name;
        }
        return std::string(tr(STR_ADD_PROFILE));
      },
      [&profiles, profileCount](int index) {
        if (index < profileCount && !profiles[index].name.empty()) {
          return profiles[index].username;
        }
        return std::string("");
      },
      nullptr,
      [profileCount, activeIndex](int index) {
        if (index < profileCount && index == activeIndex) {
          return std::string(tr(STR_ACTIVE_PROFILE_TAG));
        }
        return std::string("");
      },
      true);

  ListRenderHelper::drawStandardHints(renderer, mappedInput);

  renderer.displayBuffer();
}