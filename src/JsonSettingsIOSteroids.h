#pragma once

#include <ArduinoJson.h>

class CrossPointSettings;

namespace JsonSettingsIO {

// Steroids-only settings — stored in /.crosspoint/settings-steroids.json
// (separate file from the upstream /.crosspoint/settings.json).
//
// These functions are implemented in JsonSettingsIOSteroids.cpp so that
// JsonSettingsIO.cpp stays byte-identical to upstream, eliminating merge
// conflicts during upstream releases.
bool saveSettingsSteroids(const CrossPointSettings& s, const char* path);
bool loadSettingsSteroids(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);

}  // namespace JsonSettingsIO
