# Rebranding Completo a "Steroids" - Implementation Plan

## Executive Summary
Allineare il branch `integration/steroids-vcodex` al branding "Steroids" presente nel branch `master`, applicando modifiche estetiche e strutturali senza alterare la logica funzionale esistente.

---

## 1. Analisi Differenze Chiave (Master vs Current Branch)

### Branding & Naming
| Elemento | Master (steroids) | Current Branch | Azione |
|----------|-------------------|----------------|--------|
| Project name | "CPR-vCodex Steroids" | "CPR-vCodex" / "CPR-vCodex Steroids" | ✅ Uniformare a "Steroids" |
| Boot logo | 350×96 (Logo-steroids) | 174×24 (Logo vecchio) | 🔄 Sostituire + convertire |
| Sleep logo | 350×96 | 174×24 | 🔄 Sostituire |
| Web header | "CPR-vCodex Steroids" | "CPR-vCodex" | 🔄 Aggiornare |
| OTA repo | `franssjz/cpr-vcodex` | `marcoand75/cpr-vcodex-steroids` | 🔄 Verificare coerenza |

### File Logo Master (presenti in master, assenti/malformati in current)
- `src/images/Logo-steroids.png` (source artwork)
- `src/images/Logo-steroids-white.png` (versione white)
- `src/images/Logo-steroids-bn.png` (versione black&white)
- `scripts/convert_logo.py` (script di conversione)
- `src/images/Logo.h` (generato, 350×96 on-screen)

### File Logo Current Branch
- `src/images/Logo.png` (vecchio)
- `src/images/Logo.h` (vecchio, 174×24)
- `src/images/logo.svg` (vecchio)
- `src/images/vcodex steroids.png` (non standard)

---

## 2. Piano di Lavoro per Commit

### Commit 1: Logo Assets & Conversion Script
**File da aggiungere da master:**
- `src/images/Logo-steroids.png` 
- `src/images/Logo-steroids-white.png`
- `src/images/Logo-steroids-bn.png`
- `scripts/convert_logo.py`

**File da modificare:**
- `src/images/Logo.h` → rigenerare con nuovo logo (350×96)
- Rimuovere `src/images/Logo.png`, `src/images/logo.svg`, `src/images/vcodex steroids.png`

**Validazione:** Build compila, boot logo si vede correttamente

---

### Commit 2: BootActivity & SleepActivity - Logo Dimensions
**File da modificare:**
- `src/activities/boot_sleep/BootActivity.cpp`
  - `BOOT_LOGO_WIDTH = 350` (era 174)
  - `BOOT_LOGO_HEIGHT = 96` (era 24)
  - Testo: `STR_CPR_VCODEX` → `STR_STEROIDS` (nuova stringa i18n)

- `src/activities/boot_sleep/SleepActivity.cpp`
  - `logoWidth = 350`, `logoHeight = 96` in `renderDefaultSleepScreen()`
  - Testo: `STR_CPR_VCODEX` → `STR_STEROIDS`

**Nuove stringhe i18n necessarie:**
- `STR_STEROIDS` = "Steroids" (tutte le lingue)

---

### Commit 3: Web UI - HomePage.html (Device Web Server)
**File:** `src/network/html/HomePage.html`

**Modifiche:**
- Titolo: `<title>CPR-vCodex Steroids</title>`
- Header: Aggiungere `<img src="/logo.png" ...>` sopra h1
- h1: `📚 CPR-vCodex Steroids`
- Nav links: Aggiungere link "App Settings" e "Steroids" 
- Footer: `CPR-vCodex Steroids • Open Source`

**Nota:** Il file `/logo.png` viene servito da CrossPointWebServer - verificare endpoint

---

### Commit 4: Web UI - docs/index.html (GitHub Pages)
**File:** `docs/index.html`

**Modifiche:**
- Titolo: `CPR-vCodex Steroids Tools`
- Site brand: `CPR-vCodex Steroids`
- Hero h1: `CPR-vCodex Steroids`
- Subtitle: aggiornare con riferimento Steroids
- GitHub link: `https://github.com/marcoand75/cpr-vcodex-steroids`

---

### Commit 5: Web UI - docs/flash.html (Auto Flash)
**File:** `docs/flash.html`

**Modifiche:**
- Titolo: `Auto Flash - CPR-vCodex Steroids`
- Site brand: `CPR-vCodex Steroids Tools`
- Hero: `Flash CPR-vCodex Steroids`
- Testi: sostituire "CPR-vCodex" con "CPR-vCodex Steroids"
- Repo URLs: `marcoand75/cpr-vcodex-steroids`
- Firmware manifest: nome "CPR-vCodex Steroids"

---

### Commit 6: OTA & Artifact Naming
**File da modificare:**
- `src/network/OtaUpdater.cpp`
  - `firmwareManifestUrl` → `https://franssjz.github.io/cpr-vcodex-steroids/firmware/manifest.json`
  - `firmwareManifestFallbackUrl` → raw.githubusercontent.com equivalent
  - `latestReleaseUrl` → `api.github.com/repos/marcoand75/cpr-vcodex-steroids/releases/latest`

- `scripts/package_vcodex_bin.py`
  - `artifact_stem`: mantenere `cpr-vcodex` suffix (già corretto)
  - Verificare `board_suffix` logic per x4pro

- `scripts/sync_autoflash_firmware.py`
  - `DEFAULT_REPO = "marcoand75/cpr-vcodex-steroids"`
  - Manifest name: `"CPR-vCodex Steroids"`
  - GitHub URLs aggiornate

- `docs/firmware/manifest.json` (template)
  - `name: "CPR-vCodex Steroids"`
  - `repo: "marcoand75/cpr-vcodex-steroids"`

---

### Commit 7: Settings Pages & Navigation
**File da verificare/creare:**
- `src/network/html/SteroidsSettingsPage.html` (nuovo file - SOLO settings Steroids)
- `src/network/html/SettingsPage.html` - aggiungere link "Steroids" in nav
- `src/network/html/AppSettingsPage.html` - verificare branding

**Contenuto SteroidsSettingsPage.html (SOLO settings Steroids):**
**Sleep Screen / Screensaver:**
- `sleepScreen` (SLEEP_SCREEN_MODE: BLANK, CUSTOM, COVER, COVER_CUSTOM, READING_DASHBOARD, COVER_STATS, COVER_STATS_V2, CUSTOM_STATS, CUSTOM_STATS_V2)
- `sleepScreenCoverMode` (FIT, CROP)
- `sleepScreenCoverFilter` (NO_FILTER, INVERTED_BLACK_AND_WHITE, GRAYSCALE)
- `sleepImageOrder` (SEQUENTIAL, SHUFFLE)
- `sleepImageDir` (string - percorso directory immagini custom)
- `quickResumeSleepScreen` (QUICK_RESUME_SLEEP_SCREEN enum)

**Library:**
- `libraryLayout` (LIBRARY_LAYOUT: 4X4, 3X3, 2X2)
- `libraryFilter` (LIBRARY_FILTER: ALL, FAVOURITES, LATEST_READ, UNREAD, COMPLETED, HIDDEN)
- `librarySort` (LIBRARY_SORT: TITLE_ASC, TITLE_DESC, AUTHOR_ASC, AUTHOR_DESC, RECENT, PROGRESS, COLLECTIONS, MIXED)
- `libraryViewMode` (view mode enum)
- `libraryUpdateMode` (LIBRARY_UPDATE_MODE: MANUAL, AUTO)
- `libraryFolderCollections` (bool)

**Power Button (short press):**
- `shortPwrBtn` (SHORT_PWRBTN: IGNORE, SLEEP, PAGE_TURN, FORCE_REFRESH, TOGGLE_STATUS_BAR, FOOTNOTES, SLEEP_IMAGE_CYCLE, PWR_CONFIRM)
  - Nota: `SLEEP_IMAGE_CYCLE` cicla la sleep screen alla pressione breve

**ESCLUDERE da questa pagina (vanno in SettingsPage.html o AppSettingsPage.html):**
- Settings generali (WiFi, OTA, lingua, ora, tema, frontend)
- Reading settings (font, margin, line spacing, hyphenation)
- Sync/Cloud (KOReader, Calibre, OPDS)
- Dictionary, Flashcards, Wikipedia
- Achievements, Reading Stats, Heatmap
- Quick Cards, Plugins, File Transfer

**CrossPointWebServer.cpp** - registrare:
- Route pagina: `/steroids-settings` → serve `SteroidsSettingsPage.html`
- API GET: `/api/steroids-settings` → ritorna array settings con categorie (Sleep Screen, Library, Power Button)
- API POST: `/api/steroids-settings` → salva modifiche
- Route logo: `/logo.png` → serve `src/images/Logo-steroids.png` (o Logo.png generato)

---

### Commit 8: i18n Strings
**File:** `lib/I18n/I18nKeys.h` + tutti `lib/I18n/translations/*.yaml`

**Nuove chiavi:**
- `STR_STEROIDS` = "Steroids" / "Steroids" (IT) / etc.

**Aggiornare chiavi esistenti usate nel boot/sleep:**
- `STR_CPR_VCODEX` → mantenere per compatibilità o deprecate
- Usare `STR_STEROIDS` nei nuovi punti

---

### Commit 9: Version & Build Metadata
**File:**
- `src/version.cpp` - `project_name = "cpr-vcodex-steroids"` (già corretto in master)
- `platformio.ini` - verificare `build_flags` per `VCODEX_VERSION`

---

## 3. Analisi Discrepanze Funzionali (Risk Assessment)

| Area | Rischio | Mitigazione |
|------|---------|-------------|
| **Boot logo dimensions** | Cambio dimensioni rompe layout se hardcoded altrove | Verificare solo BootActivity e SleepActivity usano queste costanti |
| **OTA URLs** | Firmware esistenti non trovano aggiornamenti | Mantenere fallback URL; testare OTA su device |
| **Web server logo** | `/logo.png` endpoint potrebbe non esistere | Verificare `CrossPointWebServer.cpp` serve `/logo.png` da SPIFFS/SD |
| **i18n strings** | Stringhe mancanti causano fallback inglese | Aggiungere `STR_STEROIDS` in tutte le 24 lingue |
| **Settings page** | Nuova pagina SteroidsSettings non integrata | Verificare routing e menu SettingsActivity |

---

## 4. Ordine di Esecuzione Consigliato

1. **Commit 1** - Logo assets + convert_logo.py (foundation)
2. **Commit 2** - Boot/Sleep logo dimensions (device UI)
3. **Commit 8** - i18n strings (prerequisito per commit 2, 3, 4, 5)
4. **Commit 3** - HomePage.html (web server)
5. **Commit 4** - docs/index.html (Pages)
6. **Commit 5** - docs/flash.html (Pages)
6. **Commit 6** - OTA/Artifacts (backend)
7. **Commit 7** - Settings pages
8. **Commit 9** - Version metadata

---

## 5. Validazione Post-Implementazione

### Build & Flash
- [ ] `python -X utf8 -m platformio run -e default -j 1` → SUCCESS
- [ ] Flash su X4/X3 → boot mostra logo Steroids 350×96
- [ ] Sleep screen mostra logo Steroids

### Web UI
- [ ] `http://device-ip/` → HomePage con logo + branding Steroids
- [ ] `http://device-ip/flash.html` → Auto flash con branding Steroids
- [ ] `http://device-ip/settings` → nav include "Steroids"

### OTA
- [ ] Controllo aggiornamenti da device → trova release corrette
- [ ] Auto-flash da browser → scarica firmware corretto

### GitHub Pages
- [ ] `docs/index.html` → branding corretto
- [ ] `docs/flash.html` → funziona con nuovo manifest
- [ ] `sync_autoflash_firmware.py` → aggiorna manifest correttamente

---

## 6. Note per l'Implementatore

### Logo Conversion
```bash
python scripts/convert_logo.py src/images/Logo-steroids.png \
  --width 350 --height 96 --name Logo \
  --output src/images/Logo.h
```

### i18n Generation
Dopo aver aggiunto `STR_STEROIDS` in `I18nKeys.h`:
```bash
python -X utf8 scripts/gen_i18n.py
```

### File da NON toccare (logica funzionale)
- `ReadingStatsStore.cpp/h` - logica statistiche
- `BookmarkStore.h` - bookmark/highlight system
- `LibraryIndex.cpp` - indicizzazione libreria
- Parser EPUB/TXT/XTC
- Input handling, rendering core

---

## 7. Open Questions (Chiedere conferma)

1. **Repository OTA**: Master usa `franssjz/cpr-vcodex` per manifest, ma `marcoand75/cpr-vcodex-steroids` per release. Quale repo usare per produzione?

2. **Logo source**: Usare `Logo-steroids.png` da master così com'è, o rigenerare da SVG?

3. **Stringa boot**: `STR_STEROIDS` sola o `STR_CPR_VCODEX_STEROIDS` ("CPR-vCodex Steroids")?

4. **X4 Pro**: Mantenere build x4pro separata (withdrawn) o rimuovere completamente?

5. **shortPwrBtn enum**: Il valore `SLEEP_IMAGE_CYCLE` (indice 6) fa ciclare la sleep screen. Confermare che deve essere esposto nella pagina Steroids come opzione "Cicla Sleep Screen" per la pressione breve del tasto power?

---

*Plan generato da analisi comparativa master vs integration/steroids-vcodex al 2026-09-25*