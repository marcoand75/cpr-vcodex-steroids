#include "PluginBrowserActivity.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <strings.h>

#include "../ActivityManager.h"
#include "HalStorage.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "fontIds.h"
#include "I18n.h"
#include "components/UITheme.h"
#include "components/PanelDrawHelper.h"
#include "components/icons/pageview.h"
#include "../util/ListRenderHelper.h"
#include "util/HeaderDateUtils.h"
#include "Logging.h"

static const char* TAG = "PluginBrowser";

// "-- RESTART:" header value → whether the plugin launch should use a fast
// (silent) reboot. "no"/"false"/"0"/"off" disable the reboot (in-process).
static bool parseRestartHeader(const char* value) {
  const char* p = value;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  const auto isToken = [&](const char* token) {
    const size_t n = strlen(token);
    if (strncasecmp(p, token, n) != 0) return false;
    const char c = p[n];
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  if (*p == '0' || isToken("no") || isToken("false") || isToken("off")) {
    return false;
  }
  return true;  // default: fast reboot (also for empty/malformed values)
}

void PluginBrowserActivity::scanPlugins() {
  pluginList_.clear();
  selectedIndex_ = 0;

  std::vector<String> files = Storage.listFiles("/custom", 100);

  for (const String& name : files) {
    // Check for .lua extension
    if (name.endsWith(".lua")) {
      PluginEntry plugin;
      int slashIdx = name.lastIndexOf('/');
      // listFiles returns bare filenames without the directory prefix,
      // so construct the full path for reading.
      std::string baseName = (slashIdx >= 0) ? name.substring(slashIdx + 1).c_str() : name.c_str();
      if (slashIdx >= 0) {
        plugin.path = name.c_str();
      } else {
        plugin.path = std::string("/custom/") + baseName;
      }
      // Strip ".lua" suffix: LuaPluginActivity appends ".lua" to the name
      // when constructing the full plugin path.
      if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".lua") {
        baseName = baseName.substr(0, baseName.size() - 4);
      }
      plugin.filename = baseName;
      pluginList_.push_back(plugin);
    }
  }

  // Parse headers for each plugin
  for (auto& plugin : pluginList_) {
    parsePluginHeaders(plugin);
  }

  LOG_INF(TAG, "Found %d plugins", (int)pluginList_.size());
}

void PluginBrowserActivity::parsePluginHeaders(PluginEntry& entry) {
  char buffer[4096];
  const size_t len = Storage.readFileToBuffer(entry.path.c_str(), buffer, sizeof(buffer) - 1, 0);
  if (len == 0) return;
  buffer[len] = '\0';

  // Parse the first 50 lines for headers (long explanatory comment blocks at
  // the top of a script must not push the -- NAME:/DESC:/ICON:/RESTART: lines
  // past the scan window).
  char lineBuf[256];
  const char* p = buffer;
  int lineNum = 0;
  while (*p && lineNum < 50) {
    // Extract one line
    const char* lineStart = p;
    while (*p && *p != '\n' && *p != '\r' && (p - buffer) < (int)len) p++;
    size_t lineLen = p - lineStart;
    if (lineLen >= sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;
    memcpy(lineBuf, lineStart, lineLen);
    lineBuf[lineLen] = '\0';
    lineNum++;

    // Skip leading whitespace
    char* trimmed = lineBuf;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

    // Check for header comments. Note: the "-- " prefix plus the key plus ':'
    // must match exactly — "-- NAME:" / "-- DESC:" are 8 chars, "-- ICON:" is
    // 8 chars, "-- RESTART:" is 11 chars.
    if (strncmp(trimmed, "-- NAME:", 8) == 0) {
      entry.name = trimmed + 8;
      // Trim trailing whitespace
      while (entry.name.size() > 0 &&
             (entry.name.back() == ' ' || entry.name.back() == '\t' ||
              entry.name.back() == '\r' || entry.name.back() == '\n')) {
        entry.name.pop_back();
      }
    } else if (strncmp(trimmed, "-- DESC:", 8) == 0) {
      entry.description = trimmed + 8;
      while (entry.description.size() > 0 &&
             (entry.description.back() == ' ' || entry.description.back() == '\t' ||
              entry.description.back() == '\r' || entry.description.back() == '\n')) {
        entry.description.pop_back();
      }
    } else if (strncmp(trimmed, "-- ICON:", 8) == 0) {
      entry.icon = trimmed + 8;
      while (entry.icon.size() > 0 &&
             (entry.icon.back() == ' ' || entry.icon.back() == '\t' ||
              entry.icon.back() == '\r' || entry.icon.back() == '\n')) {
        entry.icon.pop_back();
      }
    } else if (strncmp(trimmed, "-- RESTART:", 11) == 0) {
      entry.fastReboot = parseRestartHeader(trimmed + 11);
    }

    // Move to next line
    while (*p == '\n' || *p == '\r') p++;
  }

  // If no NAME header, use the filename (without extension)
  if (entry.name.empty()) {
    entry.name = entry.filename;
    if (entry.name.size() > 4 && entry.name.substr(entry.name.size() - 4) == ".lua") {
      entry.name = entry.name.substr(0, entry.name.size() - 4);
    }
  }
}

bool PluginBrowserActivity::togglePluginReboot(PluginEntry& plugin) {
  const std::string newValue = plugin.fastReboot ? "no" : "yes";
  const std::string marker = "-- RESTART:";
  const std::string replacement = marker + " " + newValue;

  HalFile file = Storage.open(plugin.path.c_str(), O_RDWR);
  if (!file) {
    LOG_ERR(TAG, "Failed to open plugin for reboot toggle: %s", plugin.path.c_str());
    return false;
  }

  const size_t fileSize = file.fileSize();
  if (fileSize == 0) {
    file.close();
    return false;
  }

  std::vector<char> buffer(fileSize + 1);
  const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer.data()), fileSize);
  file.close();
  if (bytesRead != fileSize) {
    LOG_ERR(TAG, "Short read while toggling reboot for %s", plugin.path.c_str());
    return false;
  }
  buffer[fileSize] = '\0';

  const char* start = strstr(buffer.data(), marker.c_str());
  if (!start) {
    // No RESTART header yet: append after the last leading comment block.
    const char* insert = buffer.data();
    const char* scan = buffer.data();
    while (*scan) {
      if (scan[0] == '\n' || scan[0] == '\r') {
        const char* next = scan + 1;
        if (*next == '-' && *(next + 1) == '-') {
          insert = next;
        }
        break;
      }
      ++scan;
    }
    std::string updated(buffer.data(), insert - buffer.data());
    updated += "-- RESTART: " + newValue + "\n";
    updated += insert;
    file = Storage.open(plugin.path.c_str(), O_WRONLY | O_TRUNC);
    if (!file) return false;
    const bool ok = file.write(reinterpret_cast<const uint8_t*>(updated.c_str()), updated.size()) == updated.size();
    file.close();
    if (ok) plugin.fastReboot = !plugin.fastReboot;
    return ok;
  }

  const char* lineEnd = start;
  while (*lineEnd && *lineEnd != '\n' && *lineEnd != '\r') ++lineEnd;

  std::string updated(buffer.data(), start - buffer.data());
  updated += replacement;
  updated += lineEnd;

  file = Storage.open(plugin.path.c_str(), O_WRONLY | O_TRUNC);
  if (!file) {
    LOG_ERR(TAG, "Failed to reopen plugin for reboot toggle: %s", plugin.path.c_str());
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(updated.c_str()), updated.size()) == updated.size();
  file.close();
  if (ok) plugin.fastReboot = !plugin.fastReboot;
  return ok;
}

bool PluginBrowserActivity::deletePlugin(const PluginEntry& plugin) {
  if (Storage.remove(plugin.path.c_str())) {
    LOG_INF(TAG, "Deleted plugin: %s", plugin.path.c_str());
    return true;
  }
  LOG_ERR(TAG, "Failed to delete plugin: %s", plugin.path.c_str());
  return false;
}

void PluginBrowserActivity::onEnter() {
  Activity::onEnter();
  scanPlugins();
  requestUpdate();
}

void PluginBrowserActivity::onExit() {
  Activity::onExit();
}

void PluginBrowserActivity::loop() {
  if (pluginList_.empty()) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      onGoHome();
    }
    return;
  }

  if (showingMenu_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingMenu_ = false;
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (menuIndex_ > 0) menuIndex_--;
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (menuIndex_ < 1) menuIndex_++;
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (menuIndex_ == 0) {
        PluginEntry& plugin = pluginList_[selectedIndex_];
        if (togglePluginReboot(plugin)) {
          LOG_INF(TAG, "Plugin reboot toggled: %s -> %s", plugin.name.c_str(),
                  plugin.fastReboot ? "RST" : "LIVE");
        }
      } else if (menuIndex_ == 1) {
        const PluginEntry& plugin = pluginList_[selectedIndex_];
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                   tr(STR_PLUGIN_OPTION_DELETE), plugin.name),
            [this](const ActivityResult& result) {
              if (!result.isCancelled && selectedIndex_ < (int)pluginList_.size()) {
                const PluginEntry& target = pluginList_[selectedIndex_];
                if (deletePlugin(target)) {
                  scanPlugins();
                  if (selectedIndex_ >= (int)pluginList_.size()) {
                    selectedIndex_ = std::max(0, (int)pluginList_.size() - 1);
                  }
                }
              }
              requestUpdate();
            });
      }
      showingMenu_ = false;
      requestUpdate();
      return;
    }

    delay(10);
    return;
  }

  // Track Confirm press for long-press detection.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld_ = true;
    longPressFired_ = false;
    confirmPressStartMs_ = millis();
  }

  if (confirmHeld_ && !longPressFired_ && millis() - confirmPressStartMs_ >= kPluginLongPressMs) {
    longPressFired_ = true;
    showingMenu_ = true;
    menuIndex_ = 0;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld_ && !longPressFired_) {
      // Short press: launch the selected plugin
      if (selectedIndex_ < (int)pluginList_.size()) {
         const PluginEntry& plugin = pluginList_[selectedIndex_];
         const std::string& pluginName = plugin.filename;
         const bool fromApps = false;
         const bool returnToPluginBrowser = true;

         if (plugin.fastReboot) {
           LOG_INF(TAG, "Launching plugin with fast reboot: %s", pluginName.c_str());
           silentRestartToPlugin(pluginName.c_str(), fromApps, returnToPluginBrowser);
         } else {
           LOG_INF(TAG, "Launching plugin in-process (no reboot): %s", pluginName.c_str());
           activityManager.goToPluginInProcess(pluginName.c_str(), returnToPluginBrowser);
         }
      }
    }
    confirmHeld_ = false;
    longPressFired_ = false;
    confirmPressStartMs_ = 0;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedIndex_ > 0) selectedIndex_--;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedIndex_ < (int)pluginList_.size() - 1) selectedIndex_++;
    requestUpdate();
    return;
  }

  delay(10);
}

void PluginBrowserActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pw = renderer.getScreenWidth();
  const int ph = renderer.getScreenHeight();

  renderer.clearScreen();

  // Header (QuickCards style): date line + icon + bold title
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
  constexpr int iconSize = 32;
  const int iconY = metrics.topPadding + 12;
  renderer.drawIcon(PageviewIcon, 20, iconY, iconSize, iconSize);
  const int lh12 = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, 20 + iconSize + 10, iconY + (iconSize - lh12) / 2,
                    tr(STR_PLUGINS), true, EpdFontFamily::BOLD);

  if (pluginList_.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 400, tr(STR_NO_PLUGINS), true);
    renderer.drawCenteredText(UI_10_FONT_ID, 420, tr(STR_NO_PLUGINS_SUB), true);
  } else {
    // QuickCards-style panels with CONTENT-ADAPTIVE heights: each panel grows
    // to fit its description (word-wrapped into up to 2 lines), so a
    // multi-line description is fully contained instead of overflowing.
    // All text stays inside the panel width, so nothing can render past the
    // screen edge (no GFX "Outside range" errors).
    constexpr int margin = 20;
    constexpr int panelGap = 10;
    constexpr int padTop = 8;
    constexpr int padMid = 4;
    constexpr int padBottom = 10;
    const int contentTop = metrics.topPadding + 12 + iconSize + 16;
    const int btnHintsTop = ph - metrics.buttonHintsHeight - 8;
    const int availH = btnHintsTop - contentTop - 12;
    const int usablePanelW = pw - margin * 2;
    const int maxTW = usablePanelW - 32;  // inside the panel, never past the screen edge
    const int textX = margin + 16;
    const int lh10 = renderer.getLineHeight(UI_10_FONT_ID);
    const int total = (int)pluginList_.size();

    // Pre-compute per-panel heights (name + wrapped description lines) and
    // the cumulative Y layout, so panels stack without overlaps.
    std::vector<int> heights(static_cast<size_t>(total), 0);
    std::vector<int> tops(static_cast<size_t>(total), 0);
    std::vector<std::vector<std::string>> descLines(static_cast<size_t>(total));
    int runningY = 0;
    for (int i = 0; i < total; i++) {
      tops[i] = runningY;
      int h = padTop + lh12 + padMid;  // name line
      if (!pluginList_[i].description.empty()) {
        descLines[i] = renderer.wrappedText(UI_10_FONT_ID, pluginList_[i].description.c_str(), maxTW, 2);
        h += static_cast<int>(descLines[i].size()) * lh10 + padMid;
      }
      h += padBottom;
      heights[i] = h;
      runningY += h + panelGap;
    }
    const int contentH = runningY > 0 ? runningY - panelGap : 0;

    // Visible window that contains the selected item (centered when possible).
    int first = 0;
    if (contentH > availH) {
      first = selectedIndex_;
      while (first > 0 && tops[selectedIndex_] - tops[first - 1] < availH) first--;
      const int selBottom = tops[selectedIndex_] + heights[selectedIndex_];
      while (first < selectedIndex_ && selBottom - tops[first] > availH) first++;
    }
    int last = first;
    while (last + 1 < total && tops[last + 1] + heights[last + 1] - tops[first] <= availH) last++;

    for (int i = first; i <= last; i++) {
      const auto& plugin = pluginList_[i];
      const int px = margin;
      const int py = contentTop + (tops[i] - tops[first]);
      const int h = heights[i];
      const bool sel = (i == selectedIndex_);

      // Panel background + cyberpunk border (selected = filled black)
      renderer.fillRect(px, py, usablePanelW, h, sel ? 1 : 0);
      PanelDrawHelper::drawCyberpunkPanel(renderer, px, py, usablePanelW, h, sel);

      const bool tb = !sel;  // black text on white panel, white on selected black panel

      // Name — bold, truncated to fit (leaves room for the reboot badge)
      const int nameTW = maxTW - 52;
      const std::string nameText = renderer.truncatedText(UI_12_FONT_ID, plugin.name.c_str(), nameTW);
      renderer.drawText(UI_12_FONT_ID, textX, py + padTop, nameText.c_str(), tb, EpdFontFamily::BOLD);

      // Reboot indicator badge: shared sizing/centering for both states.
      constexpr int badgeW = 30;
      constexpr int badgeRightMargin = 22;  // was 10; move left by at least 12 px
      const int badgeX = px + usablePanelW - badgeRightMargin - badgeW;
      const int badgeH = std::max(14, lh10 + 4);
      const int badgeY = py + padTop - 1;
      const int textY = badgeY + (badgeH - lh10) / 2;

      if (plugin.fastReboot) {
        renderer.fillRect(badgeX, badgeY, badgeW, badgeH, sel ? 0 : 1);
        renderer.drawText(UI_10_FONT_ID, badgeX + 2, textY, "RST", sel ? true : false,
                          EpdFontFamily::BOLD);
      } else {
        renderer.drawText(UI_10_FONT_ID, badgeX + 2, textY, "LIVE", tb, EpdFontFamily::BOLD);
      }

      // Description — the wrapped lines drawn inside this panel's own height
      int descY = py + padTop + lh12 + padMid;
      for (const auto& line : descLines[i]) {
        renderer.drawText(UI_10_FONT_ID, textX, descY, line.c_str(), tb);
        descY += lh10;
      }
    }

    if (showingMenu_) {
      constexpr int menuItemCount = 2;
      const int menuW = 240;
      const int menuRowH = lh12 + 10;
      const int menuH = menuItemCount * menuRowH + 24;
      const int menuX = (pw - menuW) / 2;
      const int menuY = (ph - menuH) / 2;

      renderer.fillRect(menuX, menuY, menuW, menuH, 0);
      PanelDrawHelper::drawCyberpunkPanel(renderer, menuX, menuY, menuW, menuH, true);

      const char* items[menuItemCount];
      if (pluginList_[selectedIndex_].fastReboot) {
        items[0] = tr(STR_PLUGIN_OPTION_TOGGLE_REBOOT);
      } else {
        items[0] = tr(STR_PLUGIN_OPTION_TOGGLE_REBOOT);
      }
      items[1] = tr(STR_PLUGIN_OPTION_DELETE);

      for (int i = 0; i < menuItemCount; ++i) {
        const int rowY = menuY + 12 + i * menuRowH;
        const bool selected = (i == menuIndex_);
        if (selected) {
          renderer.fillRect(menuX + 4, rowY, menuW - 8, menuRowH, true);
        }
        const int textY = rowY + (menuRowH - lh12) / 2;
        renderer.drawText(UI_12_FONT_ID, menuX + 12, textY, items[i], !selected,
                          selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }

      ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    }
  }

  // Footer
  ListRenderHelper::drawHints(renderer, mappedInput, tr(STR_BACK), tr(STR_CONFIRM), "",
                              pluginList_.empty() ? "" : tr(STR_SELECT));

  renderer.displayBuffer();
}
