#include "AppsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "AchievementsActivity.h"
#include "AchievementsStore.h"
#include "BookmarksAppActivity.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "DictionaryActivity.h"
#include "FavoritesAppActivity.h"
#include "FlashcardsAppActivity.h"
#include "IfFoundActivity.h"
#include "LibraryContextMenuActivity.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingProfileActivity.h"
#include "ReadingStatsActivity.h"
#include "ScreenCleanActivity.h"
#include "ScreenSaverActivity.h"
#include "ClippingsAppActivity.h"
#include "SleepAppActivity.h"
#include "SyncDayActivity.h"
#include "../home/FileBrowserActivity.h"
#include "../home/RecentBooksActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "OpdsServerStore.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutUiMetadata.h"
#include "util/LongPress.h"
#include "WikipediaActivity.h"
#include "QuickCardsActivity.h"

namespace {
std::string buildAppsHeaderSubtitle(const int selectedIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0) {
    return "";
  }

  const int safeItemsPerPage = std::max(1, itemsPerPage);
  const int currentPage = std::clamp(selectedIndex, 0, totalItems - 1) / safeItemsPerPage + 1;
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  return std::to_string(currentPage) + "/" + std::to_string(totalPages) + " | " + std::to_string(totalItems);
}

// Long-press threshold for the library context-menu. Match HomeActivity's
// RECENT_BOOK_LONG_PRESS_MS (1500ms) for consistent cross-screen behavior.
constexpr unsigned long LIBRARY_LONG_PRESS_MS = 1500;
}  // namespace

void AppsActivity::onEnter() {
  Activity::onEnter();
  appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
  if (!OPDS_STORE.hasServers()) {
    appShortcuts.erase(std::remove_if(appShortcuts.begin(), appShortcuts.end(),
                                      [](const ShortcutDefinition* definition) {
                                        return definition && definition->id == ShortcutId::OpdsBrowser;
                                      }),
                       appShortcuts.end());
  }
  selectedIndex = 0;
  READING_STATS.ensureLoaded();
  RECENT_BOOKS.ensureLoaded();
  FAVORITES.ensureLoaded();
  ACHIEVEMENTS.ensureLoaded();
  rebuildShortcutSubtitles();
  requestUpdate();

  listInputMapper.setBackHandler([](void* ctx) {
    auto* self = static_cast<AppsActivity*>(ctx);
    self->onGoHome();
  }, this, false);

  listInputMapper.setConfirmHandler([](void* ctx) {
    auto* self = static_cast<AppsActivity*>(ctx);
    self->openSelectedApp();
  }, this, false);

  auto onNavPress = [](void* ctx, int delta) {
    auto* self = static_cast<AppsActivity*>(ctx);
    if (self->appShortcuts.empty()) return;
    if (delta > 0) {
      self->selectedIndex = ButtonNavigator::nextIndex(self->selectedIndex, static_cast<int>(self->appShortcuts.size()));
    } else {
      self->selectedIndex = ButtonNavigator::previousIndex(self->selectedIndex, static_cast<int>(self->appShortcuts.size()));
    }
    self->requestUpdate();
  };

  auto onNavContinuous = [](void* ctx, int delta) {
    auto* self = static_cast<AppsActivity*>(ctx);
    if (self->appShortcuts.empty()) return;
    const int pageItems = UITheme::getNumberOfItemsPerPage(self->renderer, true, false, true, true);
    if (delta > 0) {
      self->selectedIndex = ButtonNavigator::nextPageIndex(self->selectedIndex, static_cast<int>(self->appShortcuts.size()), pageItems);
    } else {
      self->selectedIndex = ButtonNavigator::previousPageIndex(self->selectedIndex, static_cast<int>(self->appShortcuts.size()), pageItems);
    }
    self->requestUpdate();
  };

  listInputMapper.setNavPressAndContinuous(onNavPress, onNavContinuous, this);
}

void AppsActivity::loop() {
  listInputMapper.loop(mappedInput);

  // Long-press detect: if the user is currently holding Confirm on the
  // library shortcut for >= LIBRARY_LONG_PRESS_MS, open the library context
  // menu. We can't do this from the setConfirmHandler (which fires on the
  // press edge where getHeldTime is 0), so we poll isPressed+getHeldTime
  // each frame. LongPress::Button makes sure we fire the menu exactly
  // once per hold, even if the held duration is checked many times.
  static long_press::Button confirmPress_;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmPress_.reset();
  } else if (confirmPress_.fired(mappedInput.getHeldTime(), LIBRARY_LONG_PRESS_MS)) {
    if (appShortcuts.size() > selectedIndex &&
        appShortcuts[selectedIndex] && appShortcuts[selectedIndex]->id == ShortcutId::Library) {
      startActivityForResult(std::make_unique<LibraryContextMenuActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
                               rebuildShortcutSubtitles();
                               selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
                               requestUpdate();
                             });
    }
  }
}

void AppsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  const std::string headerSubtitle =
      buildAppsHeaderSubtitle(selectedIndex, static_cast<int>(appShortcuts.size()), pageItems);

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_APPS), headerSubtitle.empty() ? nullptr : headerSubtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (appShortcuts.empty()) {
    ListRenderHelper::drawEmptyCentered(renderer, contentTop, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(appShortcuts.size()),
                 selectedIndex,
                 [this](const int index) { return std::string(I18N.get(appShortcuts[index]->nameId)); },
                 [this](const int index) {
                   return (index >= 0 && index < static_cast<int>(shortcutSubtitles.size())) ? shortcutSubtitles[index]
                                                                                              : std::string{};
                 },
                 [this](const int index) { return appShortcuts[index]->icon; });
  }

  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}

void AppsActivity::rebuildShortcutSubtitles() {
  shortcutSubtitles.clear();
  shortcutSubtitles.reserve(appShortcuts.size());

  for (const ShortcutDefinition* definition : appShortcuts) {
    if (definition == nullptr) {
      shortcutSubtitles.emplace_back();
      continue;
    }
    shortcutSubtitles.push_back(ShortcutUiMetadata::getSubtitle(*definition));
  }
}

void AppsActivity::openSelectedApp() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(appShortcuts.size())) {
    return;
  }

  std::unique_ptr<Activity> activity;
  switch (appShortcuts[selectedIndex]->id) {
   case ShortcutId::BrowseFiles:
     startActivityForResult(std::make_unique<FileBrowserActivity>(renderer, mappedInput),
                              [this](const ActivityResult&) {
                                appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
                                rebuildShortcutSubtitles();
                                requestUpdate();
                              });
     return;
   case ShortcutId::ReadingStats:
      activity = std::make_unique<ReadingStatsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::SyncDay:
      activity = std::make_unique<SyncDayActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Settings:
      activityManager.goToSettings();
      return;
    case ShortcutId::ReadingHeatmap:
      activity = std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput);
      break;
    case ShortcutId::ReadingProfile:
      activity = std::make_unique<ReadingProfileActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Achievements:
      activity = std::make_unique<AchievementsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::IfFound:
      activity = std::make_unique<IfFoundActivity>(renderer, mappedInput);
      break;
     case ShortcutId::RecentBooks:
       startActivityForResult(std::make_unique<RecentBooksActivity>(renderer, mappedInput),
                              [this](const ActivityResult&) {
                                appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
                                rebuildShortcutSubtitles();
                                requestUpdate();
                              });
       return;
     case ShortcutId::Bookmarks:
      activity = std::make_unique<BookmarksAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Favorites:
      activity = std::make_unique<FavoritesAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Flashcards:
      activity = std::make_unique<FlashcardsAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Dictionary:
      activity = std::make_unique<DictionaryActivity>(renderer, mappedInput);
      break;
    case ShortcutId::FileTransfer:
      activityManager.goToFileTransfer();
      return;
    case ShortcutId::Library:
      // Direct launch of the library. The library context menu is reached via
      // long-press on the confirm button — see AppsActivity::loop() which
      // polls isPressed+getHeldTime each frame. We must NOT branch on
      // getHeldTime() here: openSelectedApp() is called from a press-edge
      // setConfirmHandler (useRelease=false), where getHeldTime() is always 0.
      activityManager.goToLibrary(true);
      return;
    case ShortcutId::ScreenClean:
      activity = std::make_unique<ScreenCleanActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Sleep:
      activity = std::make_unique<SleepAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::ScreenSaver:
      activity = std::make_unique<ScreenSaverActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Clippings:
      activity = std::make_unique<ClippingsAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::OpdsBrowser:
      activityManager.goToBrowser();
      return;
     case ShortcutId::Wikipedia:
       startActivityForResult(std::make_unique<WikipediaActivity>(renderer, mappedInput, true),
                              [this](const ActivityResult&) {
                                appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
                                rebuildShortcutSubtitles();
                                selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
                                requestUpdate();
                              });
       return;
    case ShortcutId::QuickCards:
      activity = std::make_unique<QuickCardsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Plugins:
      activityManager.goToPluginBrowser();
      return;
   }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
    rebuildShortcutSubtitles();
    selectedIndex = ButtonNavigator::clampIndex(selectedIndex, static_cast<int>(appShortcuts.size()));
    requestUpdate();
  });
}
