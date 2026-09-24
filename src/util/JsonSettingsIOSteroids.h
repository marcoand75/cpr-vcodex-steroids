#pragma once

#include "CrossPointSettings.h"

// Steroids fork-only settings (de)serialization.
//
// Kept out of JsonSettingsIO.cpp so upstream merges of that file stay clean.
// Missing keys keep the CrossPointSettings defaults, so a settings.json written
// by upstream (or by an older Steroids build) loads without loss, and unknown
// keys are ignored.
namespace JsonSettingsIOSteroids {

void loadSteroidsSettings(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave);
void saveSteroidsSettings(const CrossPointSettings& s, JsonDocument& doc);

}  // namespace JsonSettingsIOSteroids
