# CPR-vCodex Steroids — i18n Extensions

> **SCOPE:** New translation strings, language persistence as ISO code, 24-language support. Low merge risk.

---

## 1. New Translation Strings

| Key | English | Context |
|-----|---------|---------|
| `STR_STEROIDS` | "Steroids" | Boot logo, sleep logo, web UI |
| `STR_CPR_VCODEX_STEROIDS` | "CPR-vCodex Steroids" | About section, web UI |

Added to all 24 language files:
- `src/i18n/strings_en.cpp` (English - base)
- `src/i18n/strings_it.cpp` (Italian)
- `src/i18n/strings_de.cpp` (German)
- `src/i18n/strings_fr.cpp` (French)
- `src/i18n/strings_es.cpp` (Spanish)
- `src/i18n/strings_pt.cpp` (Portuguese)
- `src/i18n/strings_ru.cpp` (Russian)
- `src/i18n/strings_pl.cpp` (Polish)
- `src/i18n/strings_nl.cpp` (Dutch)
- `src/i18n/strings_cs.cpp` (Czech)
- `src/i18n/strings_da.cpp` (Danish)
- `src/i18n/strings_fi.cpp` (Finnish)
- `src/i18n/strings_he.cpp` (Hebrew)
- `src/i18n/strings_hu.cpp` (Hungarian)
- `src/i18n/strings_ko.cpp` (Korean)
- `src/i18n/strings_lt.cpp` (Lithuanian)
- `src/i18n/strings_no.cpp` (Norwegian)
- `src/i18n/strings_ro.cpp` (Romanian)
- `src/i18n/strings_sk.cpp` (Slovak)
- `src/i18n/strings_sl.cpp` (Slovenian)
- `src/i18n/strings_sv.cpp` (Swedish)
- `src/i18n/strings_tr.cpp` (Turkish)
- `src/i18n/strings_uk.cpp` (Ukrainian)
- `src/i18n/strings_vi.cpp` (Vietnamese)

---

## 2. Language Persistence (ISO Code)

### Decision: `i18n.language.persistence`
```cpp
// Settings stored as:
"language": "IT"  // ISO 639-1 code

// Boot logic (main.cpp):
1. Read settings-steroids.json → "language": "IT"
2. Apply via I18n::setLanguage("IT")
3. Ignore legacy language.bin on SD
4. If not configured → default English + save
```

### Migration from language.bin
```cpp
// Old: language.bin on SD (binary, single byte)
// New: settings-steroids.json (JSON, ISO string)
// Migration: automatic at first boot with new settings format
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/i18n/strings_*.cpp` | 24 language files with new strings |
| `src/I18n.h/cpp` | Language loading, ISO code support |
| `src/main.cpp` | Boot language initialization |
| `src/CrossPointSettings.h` | `language` field as string |
| `src/activities/settings/SettingsActivity.cpp` | Language selector UI |

---

## 4. Upstream Merge Notes

### LOW RISK
- 24 language files — simple string additions
- `I18n` ISO code support (backward compatible)
- Settings language field (string vs binary)

### Conflicts Unlikely
- String additions only
- Language persistence logic isolated

### Safe Cherry-Picks
- ISO code language persistence (cleaner than binary)
- `STR_STEROIDS` / `STR_CPR_VCODEX_STEROIDS` (fork-specific)

---

## 5. Validation Checklist

- [ ] All 24 languages have `STR_STEROIDS` translation
- [ ] All 24 languages have `STR_CPR_VCODEX_STEROIDS` translation
- [ ] Language selector shows all 24 languages
- [ ] Selected language persists across reboot
- [ ] Language saved as ISO code in `settings-steroids.json`
- [ ] Legacy `language.bin` ignored
- [ ] Default English when not configured
- [ ] Boot shows correct language immediately

---

## 6. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-BRANDING.md` — Branding strings usage
- `STEROIDS-ADDICTIONS-SETTINGS.md` — Settings language field

---

*Last updated: 2026-09-28 | Commit: c36a85f8 (24 languages STR_STEROIDS)*