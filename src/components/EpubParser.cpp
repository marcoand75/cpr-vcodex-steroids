#include "EpubParser.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <CoverDebugLog.h>
#include <PngToBmpConverter.h>
#include <ZipFile.h>

#include <cstdio>
#include <cstring>

namespace EpubParser {
namespace {

// Reads title and author from the persistent book.bin cache
bool readFromCache(const std::string& cacheDir, std::string& outTitle, std::string& outAuthor) {
  const std::string cachePath = cacheDir + "/book.bin";
  FsFile file;
  if (!Storage.openFileForRead("BSC", cachePath, file)) return false;

  uint8_t version;
  if (file.read(&version, 1) != 1) { file.close(); return false; }
  file.seekCur(12); // Skip lutOffset, spineCount, tocCount

  // Read title
  uint32_t len;
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
  outTitle.resize(len);
  if (file.read(reinterpret_cast<uint8_t*>(&outTitle[0]), len) != static_cast<int>(len)) { file.close(); return false; }

  // Read author
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
  outAuthor.resize(len);
  if (file.read(reinterpret_cast<uint8_t*>(&outAuthor[0]), len) != static_cast<int>(len)) { file.close(); return false; }

  // Skip remaining 8 strings
  for (int i = 0; i < 8; ++i) {
    if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) { file.close(); return false; }
    if (file.seekCur(len) < 0) { file.close(); return false; }
  }

  file.close();
  return true;
}

// Lightweight direct parsing of container.xml and content.opf
bool readDirectFromZip(const std::string& epubPath, std::string& outTitle, std::string& outAuthor) {
  ZipFile zip(epubPath);

  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize) || containerSize == 0 || containerSize > 8192) return false;

  uint8_t* containerData = zip.readFileToMemory("META-INF/container.xml", &containerSize);
  if (!containerData) return false;

  std::string contentOpfPath;
  const char* fpAttr = strstr((const char*)containerData, "full-path=\"");
  if (fpAttr) {
    fpAttr += 11;
    const char* fpEnd = strchr(fpAttr, '"');
    if (fpEnd) contentOpfPath.assign(fpAttr, fpEnd - fpAttr);
  }
  free(containerData);

  if (contentOpfPath.empty()) return false;
  contentOpfPath = FsHelpers::normalisePath(contentOpfPath);

  size_t opfSize = 0;
  if (!zip.getInflatedFileSize(contentOpfPath.c_str(), &opfSize) || opfSize == 0) return false;
  if (opfSize > 64 * 1024) opfSize = 64 * 1024;

  uint8_t* opfData = zip.readFileToMemory(contentOpfPath.c_str(), &opfSize);
  if (!opfData) return false;

  const char* opfStr = (const char*)opfData;
  const char* opfEnd = opfStr + opfSize;

  auto findDcTag = [opfStr, opfEnd](const char* localName, std::string& out) -> bool {
    char openPattern[32];
    snprintf(openPattern, sizeof(openPattern), "<dc:%s", localName);
    const size_t patternLen = strlen(openPattern);
    const char* pos = opfStr;

    while (pos < opfEnd) {
      const char* tagStart = strstr(pos, openPattern);
      if (!tagStart || tagStart >= opfEnd) break;

      const char* openEnd = tagStart + patternLen;
      while (openEnd < opfEnd && *openEnd != '>') openEnd++;
      if (openEnd >= opfEnd) break;
      openEnd++;

      char closePattern[40];
      snprintf(closePattern, sizeof(closePattern), "</dc:%s>", localName);
      const char* closePos = strstr(openEnd, closePattern);
      if (!closePos || closePos >= opfEnd) { pos = openEnd; continue; }

      out.assign(openEnd, closePos - openEnd);
      size_t start = 0, end = out.size();
      while (start < end && (out[start] == ' ' || out[start] == '\n' || out[start] == '\r' || out[start] == '\t')) start++;
      while (end > start && (out[end - 1] == ' ' || out[end - 1] == '\n' || out[end - 1] == '\r' || out[end - 1] == '\t')) end--;
      
      if (start < end) { out = out.substr(start, end - start); return true; }
      pos = closePos + strlen(closePattern);
    }
    return false;
  };

  findDcTag("title", outTitle);
  findDcTag("creator", outAuthor);
  free(opfData);
  return !outTitle.empty() || !outAuthor.empty();
}

} // namespace

bool extractMetadata(const std::string& epubPath, const std::string& cacheDir, std::string& outTitle, std::string& outAuthor) {
  outTitle.clear();
  outAuthor.clear();

  if (readDirectFromZip(epubPath, outTitle, outAuthor)) {
    return !outTitle.empty() || !outAuthor.empty();
  }

  // Fallback to book.bin cache
  if (Storage.exists((cacheDir + "/book.bin").c_str())) {
    return readFromCache(cacheDir, outTitle, outAuthor);
  }
  return false;
}

bool generateCover(const std::string& epubPath, int coverW, int coverH) {
  ZipFile zip(epubPath);

  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize) || containerSize == 0 || containerSize > 8192) return false;

  uint8_t* containerData = zip.readFileToMemory("META-INF/container.xml", &containerSize);
  if (!containerData) return false;

  std::string contentOpfPath;
  const char* fpAttr = strstr((const char*)containerData, "full-path=\"");
  if (fpAttr) {
    fpAttr += 11;
    const char* fpEnd = strchr(fpAttr, '"');
    if (fpEnd) contentOpfPath.assign(fpAttr, fpEnd - fpAttr);
  }
  free(containerData);
  if (contentOpfPath.empty()) return false;
  
  contentOpfPath = FsHelpers::normalisePath(contentOpfPath);
  std::string basePath;
  size_t lastSlash = contentOpfPath.find_last_of('/');
  if (lastSlash != std::string::npos) basePath = contentOpfPath.substr(0, lastSlash + 1);

  size_t opfSize = 0;
  if (!zip.getInflatedFileSize(contentOpfPath.c_str(), &opfSize) || opfSize == 0) return false;
  if (opfSize > 32 * 1024) opfSize = 32 * 1024;

  uint8_t* opfData = zip.readFileToMemory(contentOpfPath.c_str(), &opfSize);
  if (!opfData) return false;

  const char* opfStr = (const char*)opfData;
  const char* opfEnd = opfStr + opfSize;
  std::string coverImageHref;

  // Strategy 1: EPUB 2 <meta name="cover" content="ID"/>
  const char* metaPos = opfStr;
  while ((metaPos = strstr(metaPos, "<meta ")) != nullptr && metaPos < opfEnd) {
    const char* tagEnd = strchr(metaPos, '>');
    if (!tagEnd || tagEnd >= opfEnd) break;
    
    const char* nameAttr = strstr(metaPos, "name=\"cover\"");
    if (nameAttr && nameAttr < tagEnd) {
      const char* contentAttr = strstr(metaPos, "content=\"");
      if (contentAttr && contentAttr < tagEnd) {
        contentAttr += 9;
        const char* contentEnd = strchr(contentAttr, '"');
        if (contentEnd && contentEnd <= tagEnd) {
          std::string coverId(contentAttr, contentEnd - contentAttr);
          char idPattern[128];
          snprintf(idPattern, sizeof(idPattern), "id=\"%s\"", coverId.c_str());
          const char* idAttr = strstr(opfStr, idPattern);
          while (idAttr && idAttr < opfEnd) {
            const char* itemTagStart = idAttr;
            while (itemTagStart > opfStr && *(itemTagStart - 1) != '<') itemTagStart--;
            if (strncmp(itemTagStart, "<item ", 6) == 0) {
              const char* itemTagEnd = strchr(itemTagStart, '>');
              if (itemTagEnd && itemTagEnd < opfEnd) {
                const char* hrefAttr = strstr(itemTagStart, "href=\"");
                if (hrefAttr && hrefAttr < itemTagEnd) {
                  hrefAttr += 6;
                  const char* hrefEnd = strchr(hrefAttr, '"');
                  if (hrefEnd && hrefEnd <= itemTagEnd) {
                    coverImageHref = FsHelpers::normalisePath(basePath + FsHelpers::decodeUriEscapes(std::string(hrefAttr, hrefEnd - hrefAttr)));
                  }
                }
              }
            }
            if (!coverImageHref.empty()) break;
            idAttr = strstr(idAttr + strlen(idPattern), idPattern);
          }
        }
      }
    }
    if (!coverImageHref.empty()) break;
    metaPos = tagEnd + 1;
  }

  // Strategy 2: EPUB 3 <item properties="cover-image" href="..."/>
  if (coverImageHref.empty()) {
    const char* itemPos = opfStr;
    while ((itemPos = strstr(itemPos, "<item ")) != nullptr && itemPos < opfEnd) {
      const char* tagEnd = strchr(itemPos, '>');
      if (!tagEnd || tagEnd >= opfEnd) break;
      const char* propAttr = strstr(itemPos, "properties=\"");
      if (propAttr && propAttr < tagEnd) {
        propAttr += 12;
        const char* propEnd = strchr(propAttr, '"');
        if (propEnd && propEnd <= tagEnd) {
          std::string props(propAttr, propEnd - propAttr);
          if (props.find("cover-image") != std::string::npos) {
            const char* hrefAttr = strstr(itemPos, "href=\"");
            if (hrefAttr && hrefAttr < tagEnd) {
              hrefAttr += 6;
              const char* hrefEnd = strchr(hrefAttr, '"');
              if (hrefEnd && hrefEnd <= tagEnd) {
                coverImageHref = FsHelpers::normalisePath(basePath + FsHelpers::decodeUriEscapes(std::string(hrefAttr, hrefEnd - hrefAttr)));
              }
            }
          }
        }
      }
      if (!coverImageHref.empty()) break;
      itemPos = tagEnd + 1;
    }
  }
  free(opfData);

  if (coverImageHref.empty()) return false;

  unsigned long long hash = static_cast<unsigned long long>(std::hash<std::string>{}(epubPath));
  char cacheDir[64];
  snprintf(cacheDir, sizeof(cacheDir), "/.crosspoint/epub_%llu", hash);
  
  std::string coverTempPath;
  if (FsHelpers::hasJpgExtension(coverImageHref)) coverTempPath = std::string(cacheDir) + "/.cover.jpg";
  else if (FsHelpers::hasPngExtension(coverImageHref)) coverTempPath = std::string(cacheDir) + "/.cover.png";
  else return false;

  if (!Storage.exists(cacheDir) && !Storage.mkdir(cacheDir)) return false;

  FsFile coverFile;
  if (!Storage.openFileForWrite("BSC", coverTempPath, coverFile)) return false;
  if (!zip.readFileToStream(coverImageHref.c_str(), coverFile, 4096)) {
    coverFile.close();
    Storage.remove(coverTempPath.c_str());
    return false;
  }
  coverFile.close();

  // Note: thumbPathFor logic is kept in LibraryCache, so we reconstruct it here or pass it.
  // For simplicity in this isolated module, we duplicate the hash logic or assume caller handles path.
  // Let's use the same logic:
  char thumbBuf[96];
  snprintf(thumbBuf, sizeof(thumbBuf), "/.crosspoint/epub_%llu/thumb_%dx%d.bmp", hash, coverW, coverH);
  const std::string thumbPath = thumbBuf;

  if (!Storage.exists(cacheDir) && !Storage.mkdir(cacheDir)) {
    Storage.remove(coverTempPath.c_str());
    return false;
  }

  if (FsHelpers::hasJpgExtension(coverTempPath)) {
    if (ESP.getMaxAllocHeap() < 28 * 1024) { Storage.remove(coverTempPath.c_str()); return false; }
    FsFile coverJpg;
    if (!Storage.openFileForRead("BSC", coverTempPath, coverJpg)) { Storage.remove(coverTempPath.c_str()); return false; }
    
    std::string thumbTmpPath = thumbPath + ".tmp";
    FsFile thumbBmp;
    if (!Storage.openFileForWrite("BSC", thumbTmpPath, thumbBmp)) {
      coverJpg.close(); Storage.remove(coverTempPath.c_str()); return false;
    }

    bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(coverJpg, thumbBmp, coverW, coverH, nullptr);
    thumbBmp.close();

    if (success) {
      coverJpg.close(); Storage.remove(coverTempPath.c_str()); Storage.remove(thumbPath.c_str());
      if (Storage.rename(thumbTmpPath.c_str(), thumbPath.c_str())) return true;
      Storage.remove(thumbTmpPath.c_str());
      return false;
    }

    Storage.remove(thumbTmpPath.c_str());
    coverJpg.close();
    if (!Storage.openFileForRead("BSC", coverTempPath, coverJpg)) { Storage.remove(coverTempPath.c_str()); return false; }
    if (!Storage.openFileForWrite("BSC", thumbTmpPath, thumbBmp)) { coverJpg.close(); Storage.remove(coverTempPath.c_str()); return false; }
    
    bool exifOk = JpegToBmpConverter::jpegExifThumbnailTo1BitBmpStreamWithSize(coverJpg, thumbTmpPath, thumbBmp, coverW, coverH, nullptr);
    coverJpg.close(); thumbBmp.close(); Storage.remove(coverTempPath.c_str());

    if (exifOk) {
      Storage.remove(thumbPath.c_str());
      if (Storage.rename(thumbTmpPath.c_str(), thumbPath.c_str())) return true;
      Storage.remove(thumbTmpPath.c_str());
    } else {
      Storage.remove(thumbTmpPath.c_str());
    }
    return exifOk;

  } else if (FsHelpers::hasPngExtension(coverTempPath)) {
    if (ESP.getMaxAllocHeap() < 40 * 1024) { Storage.remove(coverTempPath.c_str()); return false; }
    FsFile coverPng;
    if (!Storage.openFileForRead("BSC", coverTempPath, coverPng)) { Storage.remove(coverTempPath.c_str()); return false; }
    FsFile thumbBmp;
    if (!Storage.openFileForWrite("BSC", thumbPath, thumbBmp)) { coverPng.close(); Storage.remove(coverTempPath.c_str()); return false; }
    
    bool success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(coverPng, thumbBmp, coverW, coverH);
    coverPng.close(); thumbBmp.close(); Storage.remove(coverTempPath.c_str());
    if (!success) Storage.remove(thumbPath.c_str());
    return success;
  }
  return false;
}

} // namespace EpubParser