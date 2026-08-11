#include "SleepScreenCache.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"

#include <cstring>

// Must be file-scope so SleepScreenCache static methods and the anonymous
// namespace functions can both reference it.
static constexpr char kCacheDir[] = "/.crosspoint/sleep_cache";

namespace {

constexpr size_t kMaxCachePath = 128;

uint32_t getSourceFileSize(const std::string& sourcePath) {
    FsFile file;
    if (!Storage.openFileForRead("SLC", sourcePath, file)) {
        return 0;
    }
    const uint32_t size = file.fileSize();
    file.close();
    return size;
}

void buildCachePath(char* outBuf, size_t bufSize, uint32_t hash) {
    snprintf(outBuf, bufSize, "%s/%08x.raw", kCacheDir, hash);
}

}  // namespace

uint32_t SleepScreenCache::hashKey(const std::string& sourcePath, const uint32_t fileSize) {
    uint32_t hash = 2166136261u;
    for (char c : sourcePath) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    for (int i = 0; i < 4; i++) {
        hash ^= static_cast<uint8_t>((fileSize >> (i * 8)) & 0xFF);
        hash *= 16777619u;
    }
    hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverFilter);
    hash *= 16777619u;
    hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverMode);
    hash *= 16777619u;
    return hash;
}

bool SleepScreenCache::load(GfxRenderer& renderer, const std::string& sourcePath) {
    const uint32_t sourceSize = getSourceFileSize(sourcePath);
    if (sourceSize == 0) return false;

    char cachePath[kMaxCachePath];
    buildCachePath(cachePath, sizeof(cachePath), hashKey(sourcePath, sourceSize));

    FsFile file;
    if (!Storage.openFileForRead("SLC", cachePath, file)) return false;

    const uint32_t bufferSize = display.getBufferSize();
    if (file.fileSize() != bufferSize) {
        LOG_ERR("SLC", "Invalid cache size for %s", cachePath);
        file.close();
        Storage.remove(cachePath);
        return false;
    }

    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bytesRead = file.read(frameBuffer, bufferSize);
    file.close();

    if (bytesRead != static_cast<int>(bufferSize)) {
        LOG_ERR("SLC", "Incomplete cache read for %s", cachePath);
        return false;
    }

    LOG_DBG("SLC", "Loaded cache: %s", cachePath);
    return true;
}

void SleepScreenCache::save(const GfxRenderer& renderer, const std::string& sourcePath) {
    Storage.mkdir(kCacheDir);

    const uint32_t sourceSize = getSourceFileSize(sourcePath);
    if (sourceSize == 0) return;

    char cachePath[kMaxCachePath];
    buildCachePath(cachePath, sizeof(cachePath), hashKey(sourcePath, sourceSize));

    FsFile file;
    if (!Storage.openFileForWrite("SLC", cachePath, file)) {
        LOG_ERR("SLC", "Could not open cache file %s", cachePath);
        return;
    }

    const uint32_t bufferSize = display.getBufferSize();
    const uint8_t* frameBuffer = renderer.getFrameBuffer();
    const size_t bytesWritten = file.write(frameBuffer, bufferSize);
    file.close();

    if (bytesWritten != bufferSize) {
        LOG_ERR("SLC", "Incomplete cache write for %s", cachePath);
        Storage.remove(cachePath);
        return;
    }

    LOG_DBG("SLC", "Saved cache: %s", cachePath);
}

int SleepScreenCache::invalidateAll() {
    auto dir = Storage.open(kCacheDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    int count = 0;
    char name[128];
    char fullPath[kMaxCachePath];
    const size_t prefixLen = strnlen(kCacheDir, sizeof(fullPath) - 2);
    
    memcpy(fullPath, kCacheDir, prefixLen);
    fullPath[prefixLen] = '/';
    
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
        file.getName(name, sizeof(name));
        file.close();
        
        const size_t nameLen = strnlen(name, sizeof(name));
        if (prefixLen + 1 + nameLen < sizeof(fullPath)) {
            memcpy(fullPath + prefixLen + 1, name, nameLen + 1);
            if (Storage.remove(fullPath)) count++;
        }
    }
    dir.close();
    return count;
}