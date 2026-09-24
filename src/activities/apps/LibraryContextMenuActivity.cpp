#include "LibraryContextMenuActivity.h"

#include <GfxRenderer.h>
#include <MappedInputManager.h>

#include "../util/ListRenderHelper.h"
#include "I18n.h"
#include "LibraryActivity.h"
#include "components/LibraryCache.h"
#include "components/LibraryIndex.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Helper: resolve a runtime StrId to its translated string.
static const char* resolveStr(StrId id) { return I18n::getInstance().get(id); }

LibraryContextMenuActivity::LibraryContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("LibraryContextMenu", renderer, mappedInput) {
  items_.reserve(3);
  items_.push_back({MenuAction::ScanAndOpen, StrId::STR_LIBRARY_POPUP_SCAN_AND_OPEN});
  items_.push_back({MenuAction::RebuildLibrary, StrId::STR_LIBRARY_POPUP_REBUILD});
  items_.push_back({MenuAction::ClearCorruptCovers, StrId::STR_LIBRARY_POPUP_CLEAR_COVERS});
}

void LibraryContextMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
  confirmed_ = false;

  listInputMapper_.setBackHandler(
      [](void* ctx) {
        auto* self = static_cast<LibraryContextMenuActivity*>(ctx);
        self->finish();
      },
      this, false);

  listInputMapper_.setConfirmHandler(
      [](void* ctx) {
        auto* self = static_cast<LibraryContextMenuActivity*>(ctx);
        self->onConfirm();
      },
      this, false);

  auto onNav = [](void* ctx, int delta) {
    auto* self = static_cast<LibraryContextMenuActivity*>(ctx);
    if (delta > 0) {
      if (self->selectedIndex_ < static_cast<int>(self->items_.size()) - 1) {
        self->selectedIndex_++;
        self->requestUpdate();
      }
    } else {
      if (self->selectedIndex_ > 0) {
        self->selectedIndex_--;
        self->requestUpdate();
      }
    }
  };

  listInputMapper_.setNavPressAndContinuous(onNav, onNav, this);
}

void LibraryContextMenuActivity::loop() {
  if (confirmed_) return;

  listInputMapper_.loop(mappedInput);

  if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
    finish();
  }

  delay(50);
}

void LibraryContextMenuActivity::render(RenderLock&&) {
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();

  const int itemCount = static_cast<int>(items_.size());
  const int visibleRows = std::min(itemCount, PanelDrawHelper::kMaxVisibleRows);

  auto layout = PanelDrawHelper::calculatePanel(pageW, pageH, visibleRows);

  PanelDrawHelper::drawBackground(renderer, layout);
  PanelDrawHelper::drawTitle(renderer, layout, resolveStr(StrId::STR_LIBRARY_POPUP_MENU));
  PanelDrawHelper::drawSeparator(renderer, layout);

  for (int i = 0; i < visibleRows; ++i) {
    if (i >= itemCount) break;

    PanelDrawHelper::drawRowHighlight(renderer, layout, i, i == selectedIndex_);

    const char* label = resolveStr(items_[i].labelId);
    int textX = PanelDrawHelper::getRowTextX(layout);
    int lh = renderer.getLineHeight(UI_10_FONT_ID);
    int rowY = PanelDrawHelper::getSeparatorY(layout) + PanelDrawHelper::kPadY + i * PanelDrawHelper::kRowH;
    int textY = rowY + (PanelDrawHelper::kRowH - lh) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, i == selectedIndex_ ? false : true,
                      i == selectedIndex_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  ListRenderHelper::drawStandardHints(renderer, mappedInput);

  renderer.displayBuffer();
}

void LibraryContextMenuActivity::onConfirm() {
  confirmed_ = true;

  switch (items_[selectedIndex_].action) {
    case MenuAction::ScanAndOpen: {
      // Force scan even in manual mode, then open library.
      LibraryActivity::forceScanOnNextOpen_ = true;
      activityManager.goToLibrary();
      break;
    }
    case MenuAction::RebuildLibrary: {
      // Full library index rebuild. The scan walks the whole SD card and can
      // take tens of seconds on a large library, so:
      //   1. show a progress popup (the user previously saw nothing);
      //   2. run the scan here, in the context-menu activity, so the rebuild
      //      is visible instead of being deferred to the next Library open;
      //   3. feed the task watchdog every iteration (a long scan otherwise
      //      triggers a hardware WDT reset — the "system resets after the
      //      rebuild" symptom);
      //   4. gate on free heap: a rebuild under a fragmented heap can abort()
      //      in the streaming sort, so defer to the next Library open instead
      //      of crashing mid-index.
      if (ESP.getMaxAllocHeap() < 48 * 1024) {
        RenderLock lock(*this);
        renderer.clearScreen();
        GUI.drawPopup(renderer, tr(STR_LOW_HEAP_REBUILD));
        renderer.displayBuffer();
        delay(2500);
        finish();
        break;
      }
      LibraryIndex::invalidate();

      renderer.clearScreen();
      Rect popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
      GUI.fillPopupProgress(renderer, popupRect, 0);
      renderer.displayBuffer();

      const bool ok = LibraryIndex::scan(renderer, popupRect, SETTINGS.libraryRootDir);
      if (ok) {
        LibraryIndex::buildCollectionsIndex();
        LibraryIndex::buildIndices();
      }

      {
        RenderLock lock(*this);
        renderer.clearScreen();
        GUI.drawPopup(renderer, ok ? tr(STR_REBUILD_LIBRARY_DONE) : tr(STR_REBUILD_FAILED));
        renderer.displayBuffer();
        delay(1500);
      }
      activityManager.goToLibrary();
      break;
    }
    case MenuAction::ClearCorruptCovers: {
      int removedCount = 0;
      auto root = Storage.open("/.crosspoint");
      if (root && root.isDirectory()) {
        char name[128];
        for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
          file.getName(name, sizeof(name));
          const std::string itemName(name);
          file.close();

          const bool isCacheDir =
              itemName.size() > 5 && (itemName.compare(0, 5, "epub_") == 0 || itemName.compare(0, 4, "xtc_") == 0 ||
                                      itemName.compare(0, 4, "txt_") == 0);
          if (!isCacheDir) continue;

          std::string dirPath = "/.crosspoint/" + itemName;
          auto dir = Storage.open(dirPath.c_str());
          if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            continue;
          }

          char fileName[128];
          for (auto bmpFile = dir.openNextFile(); bmpFile; bmpFile = dir.openNextFile()) {
            bmpFile.getName(fileName, sizeof(fileName));
            const std::string fname(fileName);
            const size_t len = fname.size();
            if (len >= 4 && fname.compare(len - 4, 4, ".bmp") == 0 && bmpFile.fileSize() == 0) {
              bmpFile.close();
              std::string fullBmpPath = dirPath + "/" + fname;
              if (Storage.remove(fullBmpPath.c_str())) ++removedCount;
              continue;
            }
            bmpFile.close();
          }
          dir.close();
        }
        root.close();
      }

      {
        RenderLock lock(*this);
        renderer.clearScreen();
        char msg[64];
        if (removedCount > 0) {
          std::snprintf(msg, sizeof(msg), "%d %s", removedCount, tr(STR_CORRUPT_COVERS_REMOVED));
        } else {
          std::snprintf(msg, sizeof(msg), "%s", tr(STR_NO_CORRUPT_COVERS));
        }
        GUI.drawPopup(renderer, msg);
        renderer.displayBuffer();
        delay(removedCount > 0 ? 1500 : 2000);
      }
      finish();
      break;
    }
  }
}
