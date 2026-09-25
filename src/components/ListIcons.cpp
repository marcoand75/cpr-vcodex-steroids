#include "components/icons/listIcons.h"

#include <FreeInkUICore.h>
#include <FreeInkUIIcon.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/customListIcons.h"
#include "components/icons/readingstats.h"
#include "components/icons/heatmap.h"
#include "components/icons/readingprofile.h"
#include "components/icons/medal_alt.h"
#include "components/icons/gps_found.h"
#include "components/icons/recentbooks.h"
#include "components/icons/flashcardquiz.h"
#include "components/icons/dictionary2.h"
#include "components/icons/file_transfer.h"
#include "components/icons/sleep.h"
#include "components/icons/library_book.h"
#include "components/icons/library_new.h"
#include "components/icons/pageview.h"
#include "components/icons/opdsbrowser.h"
#include "components/icons/dictionary.h"
#include "components/icons/cleanmonitor.h"
#include "components/icons/lostdevice.h"
#include "components/icons/goalsmedal.h"
#include "components/icons/screensaver.h"
#include "components/icons/bookshelf.h"
#include "components/icons/search_plus.h"
#include "components/icons/search_minus.h"
#include "components/icons/sort_asc.h"
#include "components/icons/sort_desc.h"
#include "components/icons/notification_unread.h"
#include "components/icons/finish_flag.h"
#include "components/icons/cache_cleaner.h"
#include "components/icons/calendar_time.h"
#include "components/icons/apps_hub.h"
#include "components/icons/calibre.h"
#include "components/icons/wikipediaicon.h"
#include "components/icons/quickcards.h"
#include "components/icons/ClipIcon32.h"
#include "components/icons/rotation.h"
#include "components/icons/search.h"
#include "components/icons/time_fast.h"
#include "components/icons/sort_asc.h"
#include "components/icons/sort_desc.h"
#include "components/icons/delete_file.h"

// Firmware UIIcon -> FreeInkUI bitmap for list rows.
// The legacy Steroids 32x32 assets are pre-rotated 90° CCW for GfxRenderer::drawIcon;
// FreeInkUI's list() draws bitmaps without that extra rotation, so we rotate them
// 90° CW here to present them in their natural orientation.
namespace {
const uint8_t* rotateCW32(const uint8_t* src) {
  static uint8_t cache[12][128];
  static const uint8_t* sources[12] = {0};
  static int next = 0;

  for (int i = 0; i < 12; i++) {
    if (sources[i] == src) return cache[i];
  }

  uint8_t* dst = cache[next];
  sources[next] = src;
  next = (next + 1) % 12;

  memset(dst, 0xFF, 128);

  const int bytesPerRow = 4;
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      const int srcByte = y * bytesPerRow + (x >> 3);
      const int srcBit = 7 - (x & 7);
      const bool ink = (((src[srcByte] >> srcBit) & 1) == 0);

      const int dstX = 31 - y;
      const int dstY = x;
      const int dstByte = dstY * bytesPerRow + (dstX >> 3);
      const int dstBit = 7 - (dstX & 7);
      if (ink) {
        dst[dstByte] &= static_cast<uint8_t>(~(1 << dstBit));
      }
    }
  }

  return dst;
}
}

freeink::ui::BitmapRef listIconFor(const UIIcon icon, const int size) {
  if (size >= 32) {
    switch (icon) {
      case UIIcon::Folder:
        return freeink::ui::bitmapFromIcon(icon_folder_32);
      case UIIcon::Text:
        return freeink::ui::bitmapFromIcon(icon_file_text_32);
      case UIIcon::Image:
        return freeink::ui::bitmapFromIcon(icon_image_32);
      case UIIcon::Book:
        return freeink::ui::bitmapFromIcon(icon_book_32);
      case UIIcon::File:
        return {ClipIcon32, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Wifi:
        return freeink::ui::bitmapFromIcon(icon_wifi_32);
      case UIIcon::Library:
        return freeink::ui::bitmapFromIcon(icon_library_32);
      case UIIcon::Hotspot:
        return freeink::ui::bitmapFromIcon(icon_radio_tower_32);
      case UIIcon::Usb:
        return freeink::ui::bitmapFromIcon(icon_usb_32);
      case UIIcon::Bookmark:
        return freeink::ui::bitmapFromIcon(icon_bookmark_32);
      case UIIcon::Trophy:
        return freeink::ui::bitmapFromIcon(icon_trophy_32);
      case UIIcon::Heart:
        return freeink::ui::bitmapFromIcon(icon_heart_32);
      case UIIcon::Recent:
        return freeink::ui::bitmapFromIcon(icon_history_32);
      case UIIcon::Settings:
        return freeink::ui::bitmapFromIcon(icon_settings_2_32);
      case UIIcon::Transfer:
        return freeink::ui::bitmapFromIcon(icon_arrow_right_left_32);
      case UIIcon::ScreenSaver:
        return {ScreenSaverIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Bookshelf:
        return {BookshelfIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::SleepMode:
        return {rotateCW32(SleepModeIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::CleanMonitor:
        return {rotateCW32(CleanMonitorIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::Heatmap:
        return {rotateCW32(HeatmapReadingIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::FlashcardQuiz:
        return {rotateCW32(FlashcardQuizIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::ReadingProfile:
        return {rotateCW32(ReadingProfileIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::LostDevice:
        return {rotateCW32(LostDeviceIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::OpdsBrowser:
        return {rotateCW32(OPDSBrowserIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::Dictionary:
        return {rotateCW32(DictionaryIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::GoalsMedal:
        return {GoalsMedalIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::ReadingStatsIcon:
        return {rotateCW32(ReadingStatsIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::RecentBooks:
        return {rotateCW32(RecentBooksIcon32), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::MedalAlt:
        return {rotateCW32(MedalAltIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::GpsFound:
        return {rotateCW32(GpsFoundIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::Search:
        return {SearchIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Rotation:
        return {RotationIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Pageview:
        return {rotateCW32(PageviewIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::SearchPlus:
        return {SearchPlusIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::SearchMinus:
        return {SearchMinusIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::TimeFast:
        return {TimeFastIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::SortAsc:
        return {SortAscIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::SortDesc:
        return {SortDescIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::LibraryNew:
        return {LibraryNewIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Dictionary2:
        return {Dictionary2Icon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::AppsHub:
        return {AppsHubIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::CalendarTime:
        return {CalendarTimeIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::DeleteFile:
        return {DeleteFileIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::CacheCleaner:
        return {CacheCleanerIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::FinishFlag:
        return {FinishFlagIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::NotificationUnread:
        return {NotificationUnreadIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::FileTransfer:
        return {FileTransferIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Calibre:
        return {CalibreIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
      case UIIcon::Wikipedia:
        return {rotateCW32(WikipediaIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      case UIIcon::QuickCards:
        return {rotateCW32(QuickCardsIcon), 32, 32, freeink::ui::BitmapFormat::Mask1, false};
      default:
        return {};
    }
  }
  switch (icon) {
    case UIIcon::Folder:
      return freeink::ui::bitmapFromIcon(icon_folder_24);
    case UIIcon::Text:
      return freeink::ui::bitmapFromIcon(icon_file_text_24);
    case UIIcon::Image:
      return freeink::ui::bitmapFromIcon(icon_image_24);
    case UIIcon::Book:
      return freeink::ui::bitmapFromIcon(icon_book_24);
    case UIIcon::File:
      return freeink::ui::bitmapFromIcon(icon_file_24);
    case UIIcon::Wifi:
      return freeink::ui::bitmapFromIcon(icon_wifi_24);
    case UIIcon::Library:
      return freeink::ui::bitmapFromIcon(icon_library_24);
    case UIIcon::Hotspot:
      return freeink::ui::bitmapFromIcon(icon_radio_tower_24);
    case UIIcon::Usb:
      return freeink::ui::bitmapFromIcon(icon_usb_24);
    case UIIcon::Bookmark:
      return freeink::ui::bitmapFromIcon(icon_bookmark_24);
    case UIIcon::Trophy:
      return freeink::ui::bitmapFromIcon(icon_trophy_24);
    case UIIcon::Heart:
      return freeink::ui::bitmapFromIcon(icon_heart_24);
    case UIIcon::LibraryBook:
      return {LibraryBookIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
    case UIIcon::LibraryNew:
      return {LibraryNewIcon, 32, 32, freeink::ui::BitmapFormat::Mask1, true};
    default:
      return {};
  }
}
