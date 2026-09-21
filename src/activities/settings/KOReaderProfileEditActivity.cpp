#include "KOReaderProfileEditActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "KOReaderAuthActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/ListRenderHelper.h"

namespace {
// Editable fields: Name, Username, Password, Server URL, Document Matching.
// Existing profiles also show Set as Active and Delete (BASE_ITEMS + 2).
constexpr int BASE_ITEMS = 8;
}  // namespace

int KOReaderProfileEditActivity::getMenuItemCount() const {
  return isNewProfile ? BASE_ITEMS : BASE_ITEMS + 2;  // +1 Set as Active, +1 Delete
}

void KOReaderProfileEditActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  isNewProfile = (profileIndex < 0);
  showSaveError = false;

  if (!isNewProfile) {
    // Edit flow: copy the selected profile into local editable state.
    // Changes are persisted field-by-field through saveProfile().
    const auto* profile = KOREADER_STORE.getProfile(static_cast<size_t>(profileIndex));
    if (profile) {
      editProfile = *profile;
    } else {
      // Profile was deleted between navigation and entering this screen — treat as new
      isNewProfile = true;
      profileIndex = -1;
    }
  }

  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<KOReaderProfileEditActivity*>(ctx);
    self->finish();
  }, this, false);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<KOReaderProfileEditActivity*>(ctx);
    self->handleSelection();
  }, this, false);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<KOReaderProfileEditActivity*>(ctx);
    const int menuItems = self->getMenuItemCount();
    if (delta > 0) {
      self->selectedIndex = (self->selectedIndex + 1) % menuItems;
    } else {
      self->selectedIndex = (self->selectedIndex + menuItems - 1) % menuItems;
    }
    self->requestUpdate();
  };

  listInputMapper.setNavAll(onNav, this);
}

void KOReaderProfileEditActivity::onExit() { Activity::onExit(); }

void KOReaderProfileEditActivity::loop() {
  listInputMapper.loop(mappedInput);
}

bool KOReaderProfileEditActivity::saveProfile() {
  bool success = false;

  if (isNewProfile) {
    // Create flow: first save inserts a new profile record into the multi-profile store.
    success = KOREADER_STORE.addProfile(editProfile);
    if (success) {
      // After the first successful save, promote to an existing profile so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewProfile = false;
      profileIndex = static_cast<int>(KOREADER_STORE.getCount()) - 1;
    } else {
      LOG_ERR("KRS", "Failed to add KOReader profile");
    }
  } else {
    // Edit flow: update the same profile entry in-place.
    success = KOREADER_STORE.updateProfile(static_cast<size_t>(profileIndex), editProfile);
    if (!success) {
      LOG_ERR("KRS", "Failed to update KOReader profile at index %d", profileIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void KOReaderProfileEditActivity::handleSelection() {
  // Each field edit is saved immediately so partially configured profiles
  // survive navigation and power-loss scenarios.
  if (selectedIndex == 0) {
    // Profile Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.name = kb.text;
        saveProfile();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PROFILE_NAME),
                                                                   editProfile.name, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 1) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.username = kb.text;
        saveProfile();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   editProfile.username, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 2) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.password = kb.text;
        saveProfile();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                                   editProfile.password, 63, InputType::Password),
                           handler);
  } else if (selectedIndex == 3) {
    // Sync Server URL - prefill with https:// if empty to save typing
    const std::string prefillUrl = editProfile.serverUrl.empty() ? "https://" : editProfile.serverUrl;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.serverUrl = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveProfile();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (selectedIndex == 4) {
    // Document Matching - toggle between Filename and Binary
    editProfile.matchMethod = (editProfile.matchMethod == DocumentMatchMethod::FILENAME)
                                  ? DocumentMatchMethod::BINARY
                                  : DocumentMatchMethod::FILENAME;
    saveProfile();
    requestUpdate();
  } else if (selectedIndex == 5) {
    editProfile.sendMetadata = !editProfile.sendMetadata;
    saveProfile();
    requestUpdate();
  } else if (selectedIndex == 6) {
    editProfile.syncBehavior = editProfile.syncBehavior == KOReaderSyncBehavior::SMART
                                   ? KOReaderSyncBehavior::ASK_EVERY_TIME
                                   : KOReaderSyncBehavior::SMART;
    saveProfile();
    requestUpdate();
  } else if (selectedIndex == 7) {
    if (editProfile.username.empty() || editProfile.password.empty() || !saveProfile()) return;
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP, editProfile),
        [this](const ActivityResult&) { requestUpdate(true); });
  } else if (selectedIndex == 8 && !isNewProfile) {
    // Set as Active — persists as the new default for auto-sync and future syncs.
    if (!KOREADER_STORE.setActiveIndex(static_cast<size_t>(profileIndex))) {
      LOG_ERR("KRS", "Failed to set active KOReader profile at index %d", profileIndex);
      showSaveError = true;
    } else {
      showSaveError = false;
    }
    requestUpdate();
  } else if (selectedIndex == 9 && !isNewProfile) {
    const std::string profileLabel = editProfile.name.empty() ? editProfile.username : editProfile.name;
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_PROFILE), profileLabel),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            requestUpdate(true);
            return;
          }
          if (!KOREADER_STORE.removeProfile(static_cast<size_t>(profileIndex))) {
            LOG_ERR("KRS", "Failed to remove KOReader profile at index %d", profileIndex);
            showSaveError = true;
            requestUpdate(true);
            return;
          }
          finish();
        });
  }
}

void KOReaderProfileEditActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const char* header = isNewProfile ? tr(STR_ADD_PROFILE) : tr(STR_EDIT_PROFILE);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int menuItems = getMenuItemCount();
  const bool isActive = !isNewProfile && KOREADER_STORE.getActiveIndex() == profileIndex;

  const StrId fieldNames[] = {StrId::STR_PROFILE_NAME,      StrId::STR_USERNAME,      StrId::STR_PASSWORD,
                              StrId::STR_SYNC_SERVER_URL,   StrId::STR_DOCUMENT_MATCHING,
                              StrId::STR_SEND_METADATA,     StrId::STR_SYNC_BEHAVIOR, StrId::STR_SIGN_UP};

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, menuItems, static_cast<int>(selectedIndex),
      [this, &fieldNames](int index) {
        if (index < BASE_ITEMS) {
          return std::string(I18N.get(fieldNames[index]));
        }
        return index == BASE_ITEMS ? std::string(tr(STR_SET_ACTIVE_PROFILE)) : std::string(tr(STR_DELETE_PROFILE));
      },
      nullptr, nullptr,
      [this, isActive](int index) {
        if (index == 0) {
          return editProfile.name.empty() ? std::string(tr(STR_NOT_SET)) : editProfile.name;
        } else if (index == 1) {
          return editProfile.username.empty() ? std::string(tr(STR_NOT_SET)) : editProfile.username;
        } else if (index == 2) {
          return editProfile.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        } else if (index == 3) {
          return editProfile.serverUrl.empty() ? std::string(tr(STR_DEFAULT_VALUE)) : editProfile.serverUrl;
        } else if (index == 4) {
          return editProfile.matchMethod == DocumentMatchMethod::FILENAME ? std::string(tr(STR_FILENAME))
                                                                          : std::string(tr(STR_BINARY));
        } else if (index == 5) {
          return editProfile.sendMetadata ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
        } else if (index == 6) {
          return editProfile.syncBehavior == KOReaderSyncBehavior::SMART ? std::string(tr(STR_SMART_SYNC))
                                                                          : std::string(tr(STR_ASK_EVERY_TIME));
        } else if (index == 7) {
          return editProfile.username.empty() || editProfile.password.empty()
                     ? std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]"
                     : std::string();
        } else if (index == BASE_ITEMS && isActive) {
          return std::string(tr(STR_ACTIVE_PROFILE_TAG));
        }
        return std::string("");
      },
      true);

  ListRenderHelper::drawStandardHints(renderer, mappedInput);

  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  }

  renderer.displayBuffer();
}
