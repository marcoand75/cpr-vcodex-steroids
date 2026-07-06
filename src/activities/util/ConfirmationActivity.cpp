#include "ConfirmationActivity.h"

#include <I18n.h>

#include "../../components/UITheme.h"
#include "HalDisplay.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - (margin * 2);

  if (!heading.empty()) {
    wrappedHeading = renderer.wrappedText(fontId, heading.c_str(), maxWidth, maxLines, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    safeBody = renderer.truncatedText(fontId, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }

  const int headingLineCount = static_cast<int>(wrappedHeading.size());
  int totalHeight = headingLineCount * lineHeight;
  if (!safeBody.empty()) {
    if (headingLineCount > 0) totalHeight += spacing;
    totalHeight += lineHeight;
  }

  startY = (renderer.getScreenHeight() - totalHeight) / 2;

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  int currentY = startY;

  // Draw Heading (word-wrapped, up to maxLines)
  for (const auto& line : wrappedHeading) {
    renderer.drawCenteredText(fontId, currentY, line.c_str(), true, EpdFontFamily::BOLD);
    currentY += lineHeight;
  }
  if (!wrappedHeading.empty() && !safeBody.empty()) {
    currentY += spacing;
  }

  // Draw Body
  if (!safeBody.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeBody.c_str(), true, EpdFontFamily::REGULAR);
  }

  // Draw UI Elements
  const auto labels = mappedInput.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
}