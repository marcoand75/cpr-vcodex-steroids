# Upstream Alignment Plan: 1.5.0.20 → 1.5.0.22

## Overview

Steroids is currently synced through upstream **1.5.0.9** with freeink-sdk already migrated.
The gap is upstream **1.5.0.20** (reader improvements + EPUB image hardening) and **1.5.0.21** (wifi credential store addition).
1.5.0.22 is the freeink-sdk migration (already present in Steroids).

## Execution Phases

### Phase 1 (Critical — Security): WifiCredentialStore + CredentialIntegrity
**Status:** TODO
- [ ] Add `lib/Serialization/CredentialIntegrity.h` (CRC32 constexpr)
- [ ] Update `lib/Serialization/ObfuscationUtils.h/cpp` (bounded deobfuscate overload, thread-safe key init)
- [ ] Update `src/WifiCredentialStore.h/cpp` (mutex, MAX_PASSWORD_LENGTH, saveToFileUnlocked, findCredential→optional, getLastConnectedSsid→string, hasCredentials)
- [ ] Update `src/JsonSettingsIO.cpp` (saveWifi/loadWifi with CRC32 + password_len)
- [ ] Update `src/JsonSettingsIOSteroids.cpp` if it has wifi save/load overrides (should NOT — wifi is shared)

### Phase 2 (Critical — OTA Safety): Cross-chip firmware rejection
**Status:** TODO
- [ ] Add `WRONG_DEVICE_ERROR` to `OtaUpdater.h` enum
- [ ] Add `BAD_CHIP` → `WRONG_DEVICE_ERROR` mapping in `OtaUpdater.cpp` (KEEP Steroids preReleaseNum fix)
- [ ] Add `STR_FIRMWARE_WRONG_DEVICE` to i18n
- [ ] Update `OtaUpdateActivity.cpp/h` to display wrong-device error
- [ ] Add `BAD_CHIP` to `FirmwareFlasher.h/cpp` Result enum if not present

### Phase 3 (Medium — Features): Hide File Extension + Extra Wide
**Status:** TODO
- [ ] Add `EXTRA_WIDE=3` to LINE_COMPRESSION enum in `CrossPointSettings.h`
- [ ] Add `hideFileExtension` member to `CrossPointSettings.h`
- [ ] Add `STR_EXTRA_WIDE` and `STR_HIDE_FILE_EXTENSION` to english.yaml + italian.yaml
- [ ] Implement hideFileExtension in `FileBrowserActivity.cpp`
- [ ] Add EXTRA_WIDE option in `SettingsList.cpp`

### Phase 4 (Medium-High — Heap Stability): SdCardFont fragmentation-resistant storage
**Status:** TODO (high risk, deep integration)
- [ ] Add 4 KiB chunk bitmap storage to `SdCardFont.h/cpp`
- [ ] Add TextGetter callback to `SdCardFont::prewarm()`
- [ ] Add `loadKernLig` param, `miniGlyphBitmap()` callback, `coverageHandler`
- [ ] Update `SdCardFontManager` (loadFamilyExtraSize, deque support)
- [ ] Update `SdCardFontRegistry` (case-insensitive via resolveRootDirectoryIgnoreCase)
- [ ] Add `FsHelpers::resolveRootDirectoryIgnoreCase()`
- [ ] Update `GfxRenderer` (fallbackFontMap_, resolveTextFontId, prewarmFallbackText)
- [ ] Update `FontCacheManager` (scanFontIdSet_ flag)
- [ ] Update all callers: `FontInstaller`, `SdCardFontSystem`, `DictionaryStore`

### Phase 4b (Medium): Case-insensitive directory resolution
**Status:** TODO (depends on Phase 4 or independent)
- [ ] Add `FsHelpers::resolveRootDirectoryIgnoreCase()` 
- [ ] Add `equalsIgnoreCase()` helper
- [ ] Update `FontInstaller.cpp` to use new API
- [ ] Update `DictionaryStore.cpp` to use new API

### Phase 5 (Medium): HAL improvements
**Status:** TODO
- [ ] Add `PANIC_CAPTURE_MAGIC` + watchdog crash detection to `HalSystem.cpp/.h`
- [ ] Add `statusBarBatteryIconOnlyWidth = 20` to `BaseTheme.cpp`
- [ ] Update `CrashActivity.cpp` for watchdog-only crash reporting
- [ ] HalPowerManager: migrate to BoardConfig::ACTIVE (if freeink-sdk supports it)

### Phase 6 (Medium): Web server + File Browser
**Status:** TODO
- [ ] Write-only KOReader password in `CrossPointWebServer.cpp`
- [ ] File Browser atomic snapshots in `FileBrowserActivity.cpp`
- [ ] File Browser JSON batching (TCP-sized sends)
- [ ] FilesPage.html branding restoration

### Phase 7 (Medium): Other upstream improvements
**Status:** TODO
- [ ] Utf8: add `utf8IsCjkCodepoint()` to `Utf8.h/cpp`
- [ ] XmlParserUtils: add `xmlLocalName()`/`xmlLocalNameEquals()` for OPF namespace prefixes
- [ ] KOReaderSyncClient: accept 2xx, handle 204, silent return on no-credentials
- [ ] RecentBooksActivity: string prewarming (depends on Phase 4)

### DO NOT MERGE (Steroids diverges intentionally):
| Feature | Steroids approach | Upstream approach |
|---------|-------------------|-------------------|
| BookmarkStore v5 | v4 with `absoluteWordStart` | v5 with `visibleTextOffset` |
| Highlights | ClippingStore (separate binary store) | HighlightTextMatcher + BookmarkStore.addTextHighlight |
| Section cache | v60 (ahead of upstream v47) | v45/v46/v47 |
| EPUB engine | Steroids CrossInk variant | Upstream CrossInk variant |
| EpubReaderActivity | Steroids clipping integration | Upstream HighlightTextMatcher integration |
| ActivityManager goToEpubBookmark | spineIndex+page+absoluteWordStart | hasVisibleTextOffset+visibleTextOffset |

## Protected Files (NEVER overwrite):
See STEROIDS-ALIGN-TO-UPSTREAM.md §4 for the complete list.
