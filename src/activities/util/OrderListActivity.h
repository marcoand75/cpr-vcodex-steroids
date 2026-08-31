#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "../util/ListInputMapper.h"
#include "../util/ListLayout.h"
#include "../util/ListRenderHelper.h"
#include "util/ButtonNavigator.h"

// Shared base for order-style list activities.
//
// Provides:
//  - onEnter/onExit/loop/render lifecycle (render is virtual; default draws a standard list)
//  - moveMode toggle behavior
//  - standard Back/Select/Up/Down hints
//  - ListInputMapper setup
//
// Derived classes must implement:
//  - void reloadEntries()
//  - void save()
//  - void moveSelectedEntry(int delta)
//  - const char* getTitle() const
//  - std::string getEntryTitle(const Entry& entry) const
//
// Derived classes may override:
//  - void render(RenderLock&&)         -- when a custom layout/header is needed (e.g. date header, icons)
//  - bool handleConfirmHold(unsigned long heldMs) -- return true to consume the confirm press
//                                                   (e.g. for hold-to-delete). When true, the default
//                                                   moveMode toggle is skipped this frame.
template <typename Derived, typename Entry>
class OrderListActivity : public Activity {
 public:
  explicit OrderListActivity(const char* activityName, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(activityName, renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    static_cast<Derived*>(this)->reloadEntries();
    setupInput();
    requestUpdate();
  }

  void onExit() override {
    Activity::onExit();
    static_cast<Derived*>(this)->save();
  }

  void loop() override {
    inputMapper_.loop(mappedInput);
  }

  virtual void render(RenderLock&& lock) {
    renderer.clearScreen();
    const auto layout = ListLayout::compute(renderer);
    ListRenderHelper::drawHeader(renderer, static_cast<Derived*>(this)->getTitle());
    ListRenderHelper::drawListOrEmpty(renderer, layout, static_cast<int>(entries_.size()), selectedIndex_,
                                      [this](int index) { return static_cast<Derived*>(this)->getEntryTitle(entries_[index]); },
                                      tr(STR_NO_ENTRIES));
    ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK),
                                moveMode_ ? tr(STR_DONE) : tr(STR_SELECT),
                                tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    renderer.displayBuffer();
    (void)lock;
  }

 protected:
  std::vector<Entry> entries_;
  int selectedIndex_ = 0;
  bool moveMode_ = false;

  virtual void reloadEntries() = 0;
  virtual void save() = 0;
  virtual void moveSelectedEntry(int delta) = 0;
  virtual const char* getTitle() const = 0;
  virtual std::string getEntryTitle(Entry entry) const = 0;

  // Hook: derived classes may return true to consume the confirm release before moveMode is toggled.
  // Used by FavoritesOrderActivity for hold-to-delete.
  virtual bool handleConfirmHold(unsigned long /*heldMs*/) { return false; }

  bool isInMoveMode() const { return moveMode_; }
  int entryCount() const { return static_cast<int>(entries_.size()); }

 private:
  ListInputMapper inputMapper_;

  void setupInput() {
    inputMapper_.setBackHandler([](void* ctx) {
      auto* self = static_cast<Derived*>(ctx);
      if (self->moveMode_) {
        self->moveMode_ = false;
        self->requestUpdate();
      } else {
        self->finish();
      }
    }, this, false);

    inputMapper_.setConfirmHandler([](void* ctx) {
      auto* self = static_cast<Derived*>(ctx);
      if (self->entries_.empty()) return;
      const unsigned long heldMs = self->mappedInput.getHeldTime();
      if (!self->moveMode_ && self->handleConfirmHold(heldMs)) {
        return;
      }
      self->moveMode_ = !self->moveMode_;
      self->requestUpdate();
    }, this, false);

    inputMapper_.setNavHandlers(nullptr, [](void* ctx, int delta) {
      auto* self = static_cast<Derived*>(ctx);
      if (self->entries_.empty()) return;
      if (self->moveMode_) {
        self->moveSelectedEntry(delta);
        return;
      }
      if (delta > 0) {
        self->selectedIndex_ = ButtonNavigator::nextIndex(self->selectedIndex_, static_cast<int>(self->entries_.size()));
      } else {
        self->selectedIndex_ = ButtonNavigator::previousIndex(self->selectedIndex_, static_cast<int>(self->entries_.size()));
      }
      self->requestUpdate();
    }, nullptr, this);
  }
};