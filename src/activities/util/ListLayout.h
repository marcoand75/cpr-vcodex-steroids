#pragma once

#include <GfxRenderer.h>
#include "components/UITheme.h"

namespace ListLayout {

// Common layout parameters for list-based activities.
struct ComputedLayout {
  int contentTop = 0;
  int contentHeight = 0;
  int pageItems = 0;
};

// Compute standard list layout from the renderer.
// hasHeader: whether the activity draws a header above the list.
// hasSubtitle: whether the header includes a subtitle line.
// extraReservedHeight: additional space to reserve below the list (e.g. preview area).
inline ComputedLayout compute(const GfxRenderer& renderer, bool hasHeader = true, bool hasSubtitle = false,
                              int extraReservedHeight = 0) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int reservedBelow = metrics.buttonHintsHeight + metrics.verticalSpacing + extraReservedHeight;
  const int contentHeight = pageHeight - contentTop - reservedBelow;

  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, hasHeader, false, hasSubtitle, false, extraReservedHeight);

  return {contentTop, contentHeight, pageItems};
}

}  // namespace ListLayout
