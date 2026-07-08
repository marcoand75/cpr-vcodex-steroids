#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"

namespace {
constexpr int MENU_ITEMS = 7;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_USERNAME, StrId::STR_PASSWORD, StrId::STR_SYNC_SERVER_URL,
                                     StrId::STR_DOCUMENT_MATCHING, StrId::STR_KO_AUTO_PULL_ON_OPEN,
                                     StrId::STR_KO_AUTO_PUSH_ON_CLOSE, StrId::STR_AUTHENTICATE};
}  // namespace

static void s_onBack(void* ctx) {
  static_cast<KOReaderSettingsActivity*>(ctx)->finish();
}

static void s_onConfirm(void* ctx) {
  auto* self = static_cast<KOReaderSettingsActivity*>(ctx);
  self->handleSelection();
}

static void s_onNav(void* ctx, int delta) {
  auto* self = static_cast<KOReaderSettingsActivity*>(ctx);
  if (delta > 0) {
    self->selectedIndex = (self->selectedIndex + 1) % MENU_ITEMS;
  } else if (delta < 0) {
    self->selectedIndex = (self->selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
  }
  self->requestUpdate();
}

void KOReaderSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  listInputMapper.setBackHandler(s_onBack, this, false);
  listInputMapper.setConfirmHandler(s_onConfirm, this, false);
  listInputMapper.setNavPressAndContinuous(s_onNav, s_onNav, this);

  requestUpdate();
}

void KOReaderSettingsActivity::onExit() { Activity::onExit(); }

void KOReaderSettingsActivity::loop() {
  listInputMapper.loop(mappedInput);
}

void KOReaderSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 1) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                KOREADER_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (selectedIndex == 2) {
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 3) {
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 4) {
    SETTINGS.koSyncAutoPullOnOpen = SETTINGS.koSyncAutoPullOnOpen ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 5) {
    SETTINGS.koSyncAutoPushOnClose = SETTINGS.koSyncAutoPushOnClose ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 6) {
    if (!KOREADER_STORE.hasCredentials()) {
      return;
    }
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void KOReaderSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = ListLayout::compute(renderer, true, false, metrics.verticalSpacing);

  ListRenderHelper::drawHeader(renderer, tr(STR_KOREADER_SYNC));

  ListRenderHelper::drawList(renderer, layout, static_cast<int>(MENU_ITEMS), static_cast<int>(selectedIndex),
                             [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
                             nullptr,
                             [this](int index) {
                               if (index == 0) {
                                 auto username = KOREADER_STORE.getUsername();
                                 return username.empty() ? std::string(tr(STR_NOT_SET)) : username;
                               } else if (index == 1) {
                                 return KOREADER_STORE.getPassword().empty() ? std::string(tr(STR_NOT_SET))
                                                                             : std::string("******");
                               } else if (index == 2) {
                                 auto serverUrl = KOREADER_STORE.getServerUrl();
                                 return serverUrl.empty() ? std::string(tr(STR_DEFAULT_VALUE)) : serverUrl;
                               } else if (index == 3) {
                                 return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                                            ? std::string(tr(STR_FILENAME))
                                            : std::string(tr(STR_BINARY));
                               } else if (index == 4) {
                                 return SETTINGS.koSyncAutoPullOnOpen ? std::string(tr(STR_STATE_ON))
                                                                      : std::string(tr(STR_STATE_OFF));
                               } else if (index == 5) {
                                 return SETTINGS.koSyncAutoPushOnClose ? std::string(tr(STR_STATE_ON))
                                                                       : std::string(tr(STR_STATE_OFF));
                               } else if (index == 6) {
                                 return KOREADER_STORE.hasCredentials() ? std::string("")
                                                                        : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) +
                                                                              "]";
                               }
                               return std::string(tr(STR_NOT_SET));
                             },
                             true);

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
