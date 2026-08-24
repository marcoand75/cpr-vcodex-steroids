#include "PluginBrowserActivity.h"

#include <esp_log.h>

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include "HalStorage.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "fontIds.h"
#include "I18n.h"
#include "components/UITheme.h"

static const char* TAG = "PluginBrowser";

void PluginBrowserActivity::scanPlugins() {
  pluginList_.clear();
  selectedIndex_ = 0;

  std::vector<String> files = Storage.listFiles("/custom", 100, false);

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

  ESP_LOGI(TAG, "Found %d plugins", (int)pluginList_.size());
}

void PluginBrowserActivity::parsePluginHeaders(PluginEntry& entry) {
  char buffer[1025];
  const size_t len = Storage.readFileToBuffer(entry.path.c_str(), buffer, sizeof(buffer) - 1, 0);
  if (len == 0) return;
  buffer[len] = '\0';

  // Parse first 20 lines for headers
  char lineBuf[256];
  const char* p = buffer;
  int lineNum = 0;
  while (*p && lineNum < 20) {
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

    // Check for header comments
    if (strncmp(trimmed, "-- NAME:", 9) == 0) {
      entry.name = trimmed + 9;
      // Trim trailing whitespace
      while (entry.name.size() > 0 &&
             (entry.name.back() == ' ' || entry.name.back() == '\t' ||
              entry.name.back() == '\r' || entry.name.back() == '\n')) {
        entry.name.pop_back();
      }
    } else if (strncmp(trimmed, "-- DESC:", 9) == 0) {
      entry.description = trimmed + 9;
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

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Launch the selected plugin via silent restart
    if (selectedIndex_ < (int)pluginList_.size()) {
       const std::string& pluginName = pluginList_[selectedIndex_].filename;
       const bool fromApps = false;  // launched from PluginBrowser (not Apps hub)
       const bool returnToPluginBrowser = true;  // return to PluginBrowser after plugin exits

       ESP_LOGI(TAG, "Launching plugin: %s (fromApps=%d, returnToPluginBrowser=%d)",
                pluginName.c_str(), fromApps, returnToPluginBrowser);
       silentRestartToPlugin(pluginName.c_str(), fromApps, returnToPluginBrowser);
    }
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
  renderer.clearScreen(0xFF);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getDisplayWidth();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 20, tr(STR_PLUGINS), true, EpdFontFamily::BOLD);

  if (pluginList_.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 400, tr(STR_NO_PLUGINS), true);
    renderer.drawCenteredText(UI_10_FONT_ID, 420, tr(STR_NO_PLUGINS_SUB), true);
  } else {
    const int itemHeight = 60;
    const int startY = 80;
    const int itemCount = (int)pluginList_.size();
    const int maxVisible = 7;
    const int startIdx = max(0, min(selectedIndex_ - maxVisible / 2, itemCount - maxVisible));

    for (int i = startIdx; i < itemCount && i < startIdx + maxVisible; i++) {
      const auto& plugin = pluginList_[i];
      const int y = startY + (i - startIdx) * itemHeight;
      const bool selected = (i == selectedIndex_);

      if (selected) {
        renderer.fillRect(0, y, pageWidth, itemHeight, true);
        renderer.drawText(UI_10_FONT_ID, 10, y + 5, plugin.name.c_str(), false);
      } else {
        renderer.drawText(UI_10_FONT_ID, 10, y + 5, plugin.name.c_str(), true);
      }

      if (!plugin.description.empty()) {
        renderer.drawText(UI_10_FONT_ID, 10, y + 25, plugin.description.c_str(), selected ? false : true);
      }
    }

    // Scroll indicator
    if (itemCount > maxVisible) {
      const int scrollH = 200;
      const int scrollY = 80;
      const int thumbY = scrollY + (selectedIndex_ * (scrollH - 20) / max(1, itemCount - 1));
      renderer.drawRect(pageWidth - 10, scrollY, 4, scrollH, true);
      renderer.fillRect(pageWidth - 10, thumbY, 4, 20, true);
    }
  }

  // Footer
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), tr(STR_CONFIRM),
      pluginList_.empty() ? "" : "",
      pluginList_.empty() ? "" : tr(STR_SELECT));

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
