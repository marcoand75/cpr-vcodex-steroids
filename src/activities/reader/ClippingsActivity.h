#pragma once

#include <Epub.h>

#include <functional>
#include <memory>
#include <vector>

#include "../Activity.h"
#include "ClippingStore.h"
#include "util/ButtonNavigator.h"

class ClippingsActivity final : public Activity {
 public:
  explicit ClippingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::vector<ClippingStore::Clipping>& clippings,
                             std::function<bool(size_t)> onDeleteClipping = nullptr)
      : Activity("Clippings", renderer, mappedInput),
        clippings(clippings),
        onDeleteClipping(std::move(onDeleteClipping)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<ClippingStore::Clipping> clippings;
  std::function<bool(size_t)> onDeleteClipping;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  int getPageItems() const;
  std::string getItemLabel(int index) const;
  void confirmDeleteSelectedClipping();
};
