#pragma once

#include "KOReaderCredentialStore.h"
#include "activities/Activity.h"
#include "../util/ListInputMapper.h"

/**
 * Edit screen for a single KOReader sync profile.
 * Shows Name, Username, Password, Server URL, and Document Matching fields.
 * Existing profiles also show "Set as Active" and "Delete" options.
 * Used for both adding new profiles and editing existing ones.
 */
class KOReaderProfileEditActivity final : public Activity {
 public:
  /**
   * @param profileIndex Index into KOReaderCredentialStore's profile list, or -1 for a new profile
   */
  explicit KOReaderProfileEditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int profileIndex = -1)
      : Activity("KOReaderProfileEdit", renderer, mappedInput), profileIndex(profileIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ListInputMapper listInputMapper;

  size_t selectedIndex = 0;
  int profileIndex;
  KOReaderProfile editProfile;
  bool isNewProfile = false;
  bool showSaveError = false;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveProfile();
};