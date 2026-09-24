#pragma once

#include <ArduinoJson.h>

class CrossPointSettings;

// Deprecated compatibility shim. All Steroids settings now live in
// JsonSettingsIOSteroids; these wrappers keep older callers compiling.
namespace SteroidsSettingsIO {

void loadScreenSaver(const JsonDocument& doc, CrossPointSettings& s, bool* needsResave);
void saveScreenSaver(JsonDocument& doc, const CrossPointSettings& s);

}  // namespace SteroidsSettingsIO
