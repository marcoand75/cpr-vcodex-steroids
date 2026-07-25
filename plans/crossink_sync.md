# Development Plan: Branch feat/crossink-upstream-sync

Branch base: master (commit 56fd948f)
Obiettivo: integrare funzionalita mancanti da CrossInk e upstream CrossPoint Reader.

Ogni feature va sviluppata in un commit separato e testata individualmente.

---

## Fase 1 - Stabilita e Robustezza Reader (Alta Priorita)

### 1.1 EPUB Safe Mode Retry Chain
**Files:**
- EpubReaderActivity.cpp/h - render retry su OOM
- CrossPointSettings.h - EPUB_RENDER_SAFE = 3
- SettingsList.cpp / JsonSettingsIO.cpp / i18n

**Logica:** Se page->render() fallisce per OOM, cambia userRenderMode al successivo piu leggero e riprova.

**Stima:** ~50 righe | **Complessita:** FACILE

### 1.2 Line Spacing Continuo (MIN..MAX)
**Files:**
- CrossPointSettings.h - sostituire lineSpacing con lineHeightPercent range
- JsonSettingsIO.cpp - migrazione
- Section.h/cpp - aggiungere parametro lineHeightPercent
- EpubReaderActivity.cpp - passare lineHeightPercent

**Stima:** ~100 righe | **Complessita:** MEDIA (API change Section)

### 1.3 EPUB <hr> Section Break Rendering
**Files:**
- ChapterHtmlSlimParser.cpp - rilevare <hr>
- Page.h/cpp - nuovo PageSeparator
- Block.h - SEPARATOR_BLOCK

**Stima:** ~80 righe | **Complessita:** MEDIA

---

## Fase 2 - UI/UX Lettura (Media Priorita)

### 2.1 In-book Reader Options Submenu
**Files da creare:**
- ReaderOptionsActivity.h/.cpp - activity con opzioni raggruppate

**Files da modificare:**
- EpubReaderActivity.cpp - aggiungere voce menu
- ReaderQuickSettingsActivity.cpp
- MappedInputManager.cpp

**Stima:** ~200 righe | **Complessita:** MEDIA-ALTA

### 2.2 Thicker <u> Underlines
**Files:**
- TextBlock.cpp - raddoppiare drawLine per underline

**Stima:** ~5 righe | **Complessita:** FACILISSIMA

### 2.3 Side Button Orientation Awareness
**Files:**
- MappedInputManager.cpp - scambiare PageBack/PageForward per orientamento
- CrossPointSettings.h - sideButtonOrientationAware
- JsonSettingsIO.cpp / SettingsList.cpp / WebServer.cpp

**Stima:** ~30 righe | **Complessita:** FACILE

### 2.4 File Browser Hide Extension
**Files:**
- CrossPointSettings.h - hideFileExtension
- JsonSettingsIO.cpp
- FileBrowserActivity.cpp - nascondere estensione se attivo
- SettingsList.cpp / WebServer.cpp

**Stima:** ~25 righe | **Complessita:** FACILE

---

## Fase 3 - Funzionalita Extra (Bassa Priorita)

### 3.1 Unicode Emoji Support
**Files:**
- lib/EpdFont/fonts/NotoEmoji/ - dati font
- platformio.ini - build flag
- EpubReaderActivity.cpp - font switching per emoji

**Stima:** ~500 righe (font data ~500KB flash) | **Complessita:** ALTA

### 3.2 USB Serial File Transfer Protocol
**Files da creare:**
- src/network/SerialFileTransfer.h/.cpp

**Files:**
- main.cpp - init
- ActivityManager.cpp - goToFileTransfer() gia esiste

**Stima:** ~300 righe | **Complessita:** ALTA

### 3.3 Clock UTC Offset Picker + HalClock formatTime/formatDate
**Files da creare/copiare (da crossink):**
- lib/hal/HalClock.cpp/h - formatTime / formatDate con utcOffset
- src/activities/settings/ClockOffsetActivity.h/.cpp
- src/activities/settings/ClockSyncActivity.h/.cpp

**Files:**
- CrossPointSettings.h - utcOffsetQuarterHours, use12HourClock
- JsonSettingsIO.cpp
- SettingsActivity.cpp / SettingsList.cpp / WebServer.cpp

**Stima:** ~350 righe | **Complessita:** MEDIA

### 3.4 More Short Power Button Actions (da 5 a ~15)
**Files:**
- CrossPointSettings.h - nuovi LONG_PRESS values
- EpubReaderActivity.cpp - implementare azioni
- SettingsList.cpp / WebServer.cpp / i18n

**Stima:** ~100 righe | **Complessita:** MEDIA

---

## Riepilogo

| # | Feature | Fase | Complessita | Stima righe |
|---|---------|------|-------------|-------------|
| 1 | EPUB Safe Mode retry | 1 | FACILE | 50 |
| 2 | Thicker <u> underlines | 2 | FACILISSIMA | 5 |
| 3 | Line spacing continuo | 1 | MEDIA | 100 |
| 4 | <hr> section break | 1 | MEDIA | 80 |
| 5 | Side button orientation | 2 | FACILE | 30 |
| 6 | File browser hide ext | 2 | FACILE | 25 |
| 7 | In-book Reader Options | 2 | MEDIA-ALTA | 200 |
| 8 | More power button actions | 3 | MEDIA | 100 |
| 9 | Clock UTC + format | 3 | MEDIA | 350 |
| 10 | USB Serial File Transfer | 3 | ALTA | 300 |
| 11 | Unicode Emoji support | 3 | ALTA | 500 |

## Branch Strategy

git checkout -b feat/crossink-upstream-sync master
# sviluppare in ordine, commit per commit
