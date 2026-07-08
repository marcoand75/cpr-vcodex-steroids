#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
constexpr int MENU_ITEM_COUNT = 3;
}  // namespace

static void s_onBack(void* ctx) {
  auto* self = static_cast<NetworkModeSelectionActivity*>(ctx);
  self->onCancel();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<NetworkModeSelectionActivity*>(ctx);
  NetworkMode mode = NetworkMode::JOIN_NETWORK;
  if (self->selectedIndex == 1) {
    mode = NetworkMode::CONNECT_CALIBRE;
  } else if (self->selectedIndex == 2) {
    mode = NetworkMode::CREATE_HOTSPOT;
  }
  self->onModeSelected(mode);
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<NetworkModeSelectionActivity*>(ctx);
  if (delta > 0) {
    self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, MENU_ITEM_COUNT);
  } else if (delta < 0) {
    self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, MENU_ITEM_COUNT);
  }
  self->requestUpdate();
}

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavPressAndContinuous(s_onNav, s_onNav, this);

  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, false, metrics.verticalSpacing);

  ListRenderHelper::drawHeader(renderer, tr(STR_FILE_TRANSFER));

  static constexpr StrId menuItems[MENU_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS,
                                                        StrId::STR_CREATE_HOTSPOT};
  static constexpr StrId menuDescs[MENU_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC,
                                                        StrId::STR_HOTSPOT_DESC};
  static constexpr UIIcon menuIcons[MENU_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot};

  ListRenderHelper::drawList(renderer, layout, MENU_ITEM_COUNT, selectedIndex,
                             [](int index) { return std::string(I18N.get(menuItems[index])); },
                             [](int index) { return std::string(I18N.get(menuDescs[index])); },
                             [](int index) { return menuIcons[index]; });

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
