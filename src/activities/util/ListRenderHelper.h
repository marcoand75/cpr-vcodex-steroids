#pragma once

#include <functional>

#include <GfxRenderer.h>
#include <I18n.h>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/HeaderDateUtils.h"

// Reusable rendering helpers for list-based activities.
namespace ListRenderHelper {

// Draw a standard header. If useDateHeader is true, uses
// HeaderDateUtils::drawHeaderWithDate; otherwise uses GUI.drawHeader with a rect.
inline void drawHeader(GfxRenderer& renderer, const char* title, const char* subtitle = nullptr,
                       bool useDateHeader = false) {
  if (useDateHeader) {
    HeaderDateUtils::drawHeaderWithDate(renderer, title, subtitle);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, subtitle);
}

// Draw standard button hints from mapped labels.
inline void drawHints(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* btn1, const char* btn2,
                      const char* btn3, const char* btn4) {
  const auto labels = mappedInput.mapLabels(btn1, btn2, btn3, btn4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// Convenience: draw hints with localized strings.
inline void drawHints(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* btn1, const char* btn2,
                      const char* btn3, const char* btn4, const StrId& s1, const StrId& s2, const StrId& s3,
                      const StrId& s4) {
  drawHints(renderer, mappedInput, I18N.get(s1), I18N.get(s2), I18N.get(s3), I18N.get(s4));
}

// Draw a standard list inside a computed layout rect.
inline void drawList(GfxRenderer& renderer, int contentTop, int contentHeight, int itemCount, int selectedIndex,
                     const std::function<std::string(int index)>& rowTitle,
                     const std::function<std::string(int index)>& rowSubtitle = nullptr,
                     const std::function<UIIcon(int index)>& rowIcon = nullptr,
                     const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                     const std::function<bool(int index)>& rowCompleted = nullptr) {
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex, rowTitle, rowSubtitle,
               rowIcon, rowValue, highlightValue, rowCompleted);
}

// Draw a standard list using a pre-computed ListLayout.
template <typename Layout>
inline void drawList(GfxRenderer& renderer, const Layout& layout, int itemCount, int selectedIndex,
                     const std::function<std::string(int index)>& rowTitle,
                     const std::function<std::string(int index)>& rowSubtitle = nullptr,
                     const std::function<UIIcon(int index)>& rowIcon = nullptr,
                     const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                     const std::function<bool(int index)>& rowCompleted = nullptr) {
  drawList(renderer, layout.contentTop, layout.contentHeight, itemCount, selectedIndex, rowTitle, rowSubtitle, rowIcon,
           rowValue, highlightValue, rowCompleted);
}

}  // namespace ListRenderHelper
