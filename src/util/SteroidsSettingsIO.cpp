#include "SteroidsSettingsIO.h"

#include "CrossPointSettings.h"
#include "util/JsonSettingsIOSteroids.h"

namespace SteroidsSettingsIO {

void loadScreenSaver(const JsonDocument& doc, CrossPointSettings& s, bool* needsResave) {
  JsonSettingsIOSteroids::loadSteroidsSettings(s, doc, needsResave);
}

void saveScreenSaver(JsonDocument& doc, const CrossPointSettings& s) {
  JsonSettingsIOSteroids::saveSteroidsSettings(s, doc);
}

}  // namespace SteroidsSettingsIO
