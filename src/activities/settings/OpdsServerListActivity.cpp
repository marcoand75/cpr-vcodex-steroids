#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

static void s_onBack(void* ctx) {
  auto* self = static_cast<OpdsServerListActivity*>(ctx);
  if (self->pickerMode) {
    activityManager.goHome();
  } else {
    self->finish();
  }
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<OpdsServerListActivity*>(ctx);
  self->handleSelection();
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<OpdsServerListActivity*>(ctx);
  const int itemCount = self->getItemCount();
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, itemCount);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, itemCount);
  }
  self->requestUpdate();
}

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  // In settings mode, append a virtual "Add Server" item; in picker mode, only show real servers
  if (!pickerMode) {
    count++;
  }
  return count;
}

void OpdsServerListActivity::onEnter() {
  Activity::onEnter();

  // Reload from disk in case servers were added/removed by a subactivity or the web UI
  OPDS_STORE.loadFromFile();
  selectedIndex = 0;

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavPressAndContinuous(s_onNav, s_onNav, this);

  requestUpdate();
}

void OpdsServerListActivity::onExit() { Activity::onExit(); }

void OpdsServerListActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void OpdsServerListActivity::handleSelection() {
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());

  if (pickerMode) {
    // Picker mode: selecting a server navigates to the OPDS browser
    if (selectedIndex < serverCount) {
      const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex));
      if (server) {
        activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server));
      }
    }
    return;
  }

  // Settings mode: open editor for selected server, or create a new one
  auto resultHandler = [this](const ActivityResult&) {
    // Reload server list when returning from editor
    OPDS_STORE.loadFromFile();
    selectedIndex = 0;
  };

  if (selectedIndex < serverCount) {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, selectedIndex), resultHandler);
  } else {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void OpdsServerListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, false, metrics.verticalSpacing);
  const int itemCount = getItemCount();

  ListRenderHelper::drawHeader(renderer, tr(STR_OPDS_SERVERS));

  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, layout.contentTop + 24, tr(STR_NO_SERVERS));
  } else {
    const auto& servers = OPDS_STORE.getServers();
    const auto serverCount = static_cast<int>(servers.size());

    // Primary label: server name (falling back to URL if unnamed).
    // Secondary label: server URL (shown as subtitle when name is set).
    ListRenderHelper::drawList(
        renderer, layout, itemCount, selectedIndex,
        [&servers, serverCount](int index) {
          if (index < serverCount) {
            const auto& server = servers[index];
            return server.name.empty() ? server.url : server.name;
          }
          return std::string(I18n::getInstance().get(StrId::STR_ADD_SERVER));
        },
        [&servers, serverCount](int index) {
          if (index < serverCount && !servers[index].name.empty()) {
            return servers[index].url;
          }
          return std::string("");
        });
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
