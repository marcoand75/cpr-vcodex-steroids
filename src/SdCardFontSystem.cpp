#include "SdCardFontSystem.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <cctype>

#include "CrossPointSettings.h"

static uint8_t fontSizeEnumFromSettings() {
  uint8_t e = SETTINGS.fontSize;
  if (e >= CrossPointSettings::FONT_SIZE_COUNT) e = 1;  // default to MEDIUM
  return e;
}

static bool familyLooksCjk(const std::string& name) {
  std::string lower = name;
  for (auto& c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return lower.find("cjk") != std::string::npos || lower.find("swei") != std::string::npos ||
         lower.find("chinese") != std::string::npos;
}

SdCardFontSystem::~SdCardFontSystem() {
  delete cjkFont_;
  cjkFont_ = nullptr;
}

void SdCardFontSystem::dropCjkExtra(GfxRenderer& renderer) {
  if (cjkFontId_ != 0) {
    renderer.removeFont(cjkFontId_);
  }
  delete cjkFont_;
  cjkFont_ = nullptr;
  cjkFontId_ = 0;
  cjkFamilyName_.clear();
  cjkPointSize_ = 0;
}

void SdCardFontSystem::reRegisterCjkExtra(GfxRenderer& renderer) {
  if (!cjkFont_ || cjkFontId_ == 0) return;
  if (renderer.isSdCardFont(cjkFontId_)) return;
  // The manager unload cycle cleared the renderer's SD font table; re-register
  // the still-valid extra CJK font object (no disk reload needed).
  renderer.registerSdCardFont(cjkFontId_, cjkFont_);
  EpdFontFamily fontFamily(cjkFont_->getEpdFont(0), cjkFont_->getEpdFont(1), cjkFont_->getEpdFont(2),
                           cjkFont_->getEpdFont(3));
  renderer.insertFont(cjkFontId_, fontFamily);
}

static uint32_t countCjkCoverage(SdCardFont& font, const char* utf8Sample, int* totalNonLatin) {
  EpdFontFamily family(font.getEpdFont(0), font.getEpdFont(1), font.getEpdFont(2), font.getEpdFont(3));
  int covered = 0;
  int nonLatin = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8Sample ? utf8Sample : "");
  while (*p) {
    uint32_t cp = 0;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0) { cp = (*p++ & 0x1F) << 6; cp |= (*p++ & 0x3F); }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p++ & 0x0F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
    else if ((*p & 0xF8) == 0xF0) { cp = (*p++ & 0x07) << 18; cp |= (*p++ & 0x3F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
    else { ++p; continue; }
    if (cp < 0x80) continue;
    ++nonLatin;
    if (family.hasCodepoint(cp, EpdFontFamily::BOLD) || family.hasCodepoint(cp, EpdFontFamily::REGULAR)) {
      ++covered;
    }
  }
  if (totalNonLatin) *totalNonLatin = nonLatin;
  return covered;
}

int SdCardFontSystem::ensureCjkFontLoaded(GfxRenderer& renderer, const char* utf8Sample) {
  const bool wantFullSample = (utf8Sample && *utf8Sample);

  // Fast path: the already loaded extra still covers the whole request.
  if (cjkFont_ && cjkFontId_ != 0 && renderer.isSdCardFont(cjkFontId_)) {
    if (!wantFullSample) return cjkFontId_;
    int total = 0;
    const int covered = static_cast<int>(countCjkCoverage(*cjkFont_, utf8Sample, &total));
    if (total > 0 && covered >= total) return cjkFontId_;
    // Partial coverage: fall through so a better family can be selected.
  }

  if (registry_.getFamilyCount() == 0) registry_.discover();

  // Gather candidate CJK families (sorted as discovered).
  std::vector<const SdCardFontFamilyInfo*> candidates;
  for (const auto& f : registry_.getFamilies()) {
    if (familyLooksCjk(f.name)) candidates.push_back(&f);
  }
  if (candidates.empty()) {
    LOG_DBG("SDFS", "ensureCjkFontLoaded: no CJK SD family installed");
    return 0;
  }

  // If no sample was given, keep the previously loaded candidate if it exists
  // and is still usable, else load the first candidate.
  if (!wantFullSample && cjkFont_ && cjkFontId_ != 0 && !renderer.isSdCardFont(cjkFontId_)) {
    reRegisterCjkExtra(renderer);
    return cjkFontId_;
  }

  // Choose the candidate with the best coverage of the sample (falling back to
  // the first candidate when no sample / no coverage data).
  int bestCover = -1;
  const SdCardFontFamilyInfo* chosen = nullptr;
  SdCardFont* chosenFont = nullptr;
  uint32_t chosenContentHash = 0;
  for (const auto* fam : candidates) {
    const auto sizes = fam->availableSizes();
    if (sizes.empty()) continue;
    const uint8_t pt = sizes.front();
    const auto* file = fam->findFile(pt);
    if (!file) continue;

    auto* font = new (std::nothrow) SdCardFont();
    if (!font) continue;
    if (!font->load(file->path.c_str())) {
      delete font;
      continue;
    }

    int coverage = 0;
    if (wantFullSample) {
      int total = 0;
      coverage = static_cast<int>(countCjkCoverage(*font, utf8Sample, &total));
      if (coverage < total) {
        // Partial family: only useful when better than what we already have.
        if (coverage <= bestCover) {
          delete font;
          continue;
        }
      }
    } else {
      coverage = 0;  // no sample: first candidate wins
    }

    if (coverage > bestCover || (!wantFullSample && bestCover < 0)) {
      delete chosenFont;  // previous best
      chosenFont = font;
      chosen = fam;
      chosenContentHash = font->contentHash();
      bestCover = coverage;
      if (!wantFullSample || (wantFullSample && coverage >= 0)) {
        // When no sample requested we are done after the first candidate.
        if (!wantFullSample) break;
      }
    } else {
      delete font;
    }
  }

  if (!chosenFont || !chosen) {
    LOG_DBG("SDFS", "ensureCjkFontLoaded: no loadable CJK family");
    return 0;
  }

  // Swap the newly chosen family into the registered extra slot.
  dropCjkExtra(renderer);
  cjkFont_ = chosenFont;
  cjkFamilyName_ = chosen->name;
  cjkPointSize_ = chosen->availableSizes().empty() ? 0 : chosen->availableSizes().front();
  {
    uint32_t hash = chosenContentHash;
    static constexpr uint32_t FNV_PRIME = 16777619u;
    for (char ch : chosen->name) {
      hash ^= static_cast<uint8_t>(ch);
      hash *= FNV_PRIME;
    }
    hash ^= cjkPointSize_;
    hash *= FNV_PRIME;
    cjkFontId_ = (static_cast<int>(hash) != 0) ? static_cast<int>(hash) : 1;
  }
  reRegisterCjkExtra(renderer);
  LOG_DBG("SDFS", "CJK fallback font ready: %s size=%u id=%d covered=%d",
          chosen->name.c_str(), cjkPointSize_, cjkFontId_, bestCover);
  return cjkFontId_;
}

bool SdCardFontSystem::needsReload() const {
  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t sizeEnum = fontSizeEnumFromSettings();

  if (wantedFamily[0] == '\0') return !currentFamily.empty();

  const auto* family = registry_.findFamily(wantedFamily);
  if (!family) return !currentFamily.empty();

  if (currentFamily != wantedFamily) return true;

  const uint8_t wantedPt = manager_.getTargetSizeForEnum(*family, sizeEnum);
  return wantedPt != manager_.currentPointSize();
}

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();
  registryReleasedForNetwork_.store(false, std::memory_order_release);

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (manager_.loadFamily(*family, renderer, fontSizeEnumFromSettings())) {
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.sdFontFamilyName[0] = '\0';
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.sdFontFamilyName[0] = '\0';
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  const bool registryWasReleased = registryReleasedForNetwork_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty || registryWasReleased) {
    LOG_DBG("SDFS", "Re-discovering SD fonts%s", registryWasReleased ? " after network release" : "");
    registry_.discover();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t sizeEnum = fontSizeEnumFromSettings();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
      bumpGeneration();
    }
    reRegisterCjkExtra(renderer);
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      bumpGeneration();
      SETTINGS.sdFontFamilyName[0] = '\0';
      reRegisterCjkExtra(renderer);
      return;
    }
    auto sizes = family->availableSizes();
    uint8_t idx = sizeEnum;
    if (idx >= sizes.size()) idx = sizes.size() - 1;
    uint8_t wantedPt = sizes.empty() ? 0 : sizes[idx];
    if (!registryWasDirty && !registryWasReleased && wantedPt == manager_.currentPointSize()) return;
    const char* reason = registryWasDirty ? " [registry dirty]" : (registryWasReleased ? " [network restore]" : "");
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (enum %u)%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            sizeEnum, reason);
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, sizeEnum)) {
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
      bumpGeneration();
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      bumpGeneration();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    bumpGeneration();
  }

  reRegisterCjkExtra(renderer);
}

bool SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  const bool hadLoadedFont = manager_.hasLoadedFont();
  if (hadLoadedFont) {
    LOG_DBG("SDFS", "Unloading SD font family before network: %s", manager_.currentFamilyName().c_str());
    manager_.unloadAll(renderer);
    bumpGeneration();
  }
  // The extra CJK fallback also drops its registered/loaded state to free RAM.
  dropCjkExtra(renderer);

  registry_.releaseMemory();
  registryReleasedForNetwork_.store(true, std::memory_order_release);
  return hadLoadedFont;
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}
