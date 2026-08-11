#pragma once

#include <cstdint>
#include <string>

class GfxRenderer;

class SleepScreenCache {
 public:
  static bool load(GfxRenderer& renderer, const std::string& sourcePath);
  static void save(const GfxRenderer& renderer, const std::string& sourcePath);
  static int invalidateAll();

 private:
  static uint32_t hashKey(const std::string& sourcePath, uint32_t fileSize);
  // CACHE_DIR e buildCachePath() sono ora nel .cpp (namespace anonimo)
  // per evitare dipendenze dall'header.
};