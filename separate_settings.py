import re

with open('src/JsonSettingsIO.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Replace include
content = content.replace('#include "util/SteroidsSettingsIO.h"', '#include "util/JsonSettingsIOSteroids.h"')

# 2. Remove migrateLegacyStatsShortcut and the namespace close + comment
old_migrate = '''void migrateLegacyStatsShortcut(CrossPointSettings& settings, const JsonDocument& doc, bool* needsResave) {
  const bool hasLegacyStatsShortcut =
      !doc["statsShortcut"].isNull() || !doc["statsShortcutOrder"].isNull() || !doc["statsShortcutVisible"].isNull();
  if (!hasLegacyStatsShortcut) {
    return;
  }

  const bool legacyVisible = settings.statsShortcutVisible != 0;
  const auto legacyLocation = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(settings.statsShortcut);
  if (legacyVisible &&
      (legacyLocation == CrossPointSettings::SHORTCUT_HOME || legacyLocation == CrossPointSettings::SHORTCUT_APPS)) {
    settings.readingStatsShortcut = settings.statsShortcut;
    settings.readingStatsShortcutOrder = settings.statsShortcutOrder;
    settings.readingStatsShortcutVisible = 1;
  }

  settings.statsShortcutVisible = 0;
  if (needsResave) *needsResave = true;
}
}  // namespace

// Convert legacy settings.'''
new_migrate = '''// Convert legacy settings.'''
content = content.replace(old_migrate, new_migrate)

# 3. Replace the Steroids block in loadSettingsDirect
# Find the start: shortcutLocationCount definition
start_marker = "  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;"
start_idx = content.find(start_marker)
if start_idx == -1:
    print("ERROR: Could not find shortcutLocationCount start marker")
    exit(1)

# Find the end: SteroidsSettingsIO::loadScreenSaver call
end_marker = "  SteroidsSettingsIO::loadScreenSaver(doc, s, needsResave);"
end_idx = content.find(end_marker)
if end_idx == -1:
    print("ERROR: Could not find loadScreenSaver end marker")
    exit(1)

# Find the end of that line
line_end = content.find('\n', end_idx)
steroids_load_block = content[start_idx:line_end+1]

# Replace with JsonSettingsIOSteroids call
new_load_block = "  JsonSettingsIOSteroids::loadSteroidsSettings(s, doc, needsResave);\n"
content = content.replace(steroids_load_block, new_load_block)

# 4. Replace SteroidsSettingsIO::saveScreenSaver in saveSettings
content = content.replace(
    "  // Steroids fork-only settings (own file to keep upstream merges clean).\n  SteroidsSettingsIO::saveScreenSaver(doc, s);",
    "  // Steroids fork-only settings (own file to keep upstream merges clean).\n  JsonSettingsIOSteroids::saveSteroidsSettings(s, doc);"
)

# 5. Remove shortcut save block from saveSettings
# Find the start: appsHubShortcutOrder save
save_start_marker = "  doc[\"appsHubShortcutOrder\"] = s.appsHubShortcutOrder;"
save_start_idx = content.find(save_start_marker)
if save_start_idx == -1:
    print("ERROR: Could not find appsHubShortcutOrder save start marker")
    exit(1)

# Find the end: libraryLastCleanupDay save
save_end_marker = "  doc[\"libraryLastCleanupDay\"] = s.libraryLastCleanupDay;"
save_end_idx = content.find(save_end_marker)
if save_end_idx == -1:
    print("ERROR: Could not find libraryLastCleanupDay save end marker")
    exit(1)

# Find the end of that line
save_line_end = content.find('\n', save_end_idx)
steroids_save_block = content[save_start_idx:save_line_end+1]

# Remove it
content = content.replace(steroids_save_block, "")

with open('src/JsonSettingsIO.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print("Done. File updated.")
print('New line count:', content.count(chr(10)))
