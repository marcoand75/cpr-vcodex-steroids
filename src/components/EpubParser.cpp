#include "EpubParser.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
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
bool readDirectFromZip(const std::string& epubPath, std::string& outTitle, std::string& outAuthor,
                       std::string* outSeries, float* outSeriesIndex) {
  ZipFile zip(epubPath);

  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize) || containerSize == 0 || containerSize > 8192) return false;

  uint8_t* containerData = zip.readFileToMemory("META-INF/container.xml", &containerSize, true);
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

  uint8_t* opfData = zip.readFileToMemory(contentOpfPath.c_str(), &opfSize, true);
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

  // ---- Calibre series extraction ----
  // <meta name="calibre:series" content="Series Name"/>
  // <meta name="calibre:series_index" content="1.0"/>
  if (outSeries || outSeriesIndex) {
    if (outSeries) outSeries->clear();
    if (outSeriesIndex) *outSeriesIndex = 0.0f;

    const char* scan = opfStr;
    const char* contentValue = nullptr;
    while ((scan = strstr(scan, "<meta")) != nullptr && scan < opfEnd) {
      const char* tagEnd = strchr(scan, '>');
      if (!tagEnd) break;

      const char* nameAttr = strstr(scan, "name=\"calibre:series\"");
      if (nameAttr && nameAttr < tagEnd) {
        const char* c = strstr(nameAttr, "content=\"");
        if (c && c < tagEnd) {
          c += 9;
          const char* ce = strchr(c, '"');
          if (ce && ce <= tagEnd && outSeries) {
            outSeries->assign(c, ce - c);
            contentValue = ce;
          }
        }
      }

      const char* idxAttr = strstr(scan, "name=\"calibre:series_index\"");
      if (idxAttr && idxAttr < tagEnd) {
        const char* c = strstr(idxAttr, "content=\"");
        if (c && c < tagEnd) {
          c += 9;
          const char* ce = strchr(c, '"');
          if (ce && ce <= tagEnd && outSeriesIndex) {
            *outSeriesIndex = strtof(c, nullptr);
          }
        }
      }

      // EPUB3 belongs-to-collection (only if no calibre:series found)
      if (!contentValue && outSeries) {
        const char* belongs = strstr(scan, "property=\"belongs-to-collection\"");
        if (belongs && belongs < tagEnd) {
          const char* idAttr = strstr(scan, "id=\"");
          if (idAttr && idAttr < tagEnd) {
            idAttr += 4;
            const char* idEnd = strchr(idAttr, '"');
            if (idEnd && idEnd < tagEnd) {
              std::string colId(idAttr, idEnd - idAttr);
              // Search for <meta refines="#colId" property="collection-type">series</meta>
              // and <meta refines="#colId" property="group-position">1</meta>
              char refinesPat[64];
              snprintf(refinesPat, sizeof(refinesPat), "refines=\"#%s\"", colId.c_str());
              const char* refScan = opfStr;
              while ((refScan = strstr(refScan, refinesPat)) != nullptr && refScan < opfEnd) {
                const char* refEnd = strchr(refScan, '>');
                if (!refEnd) break;
                const char* content = strstr(refScan, "content=\"");
                if (content && content < refEnd) {
                  content += 9;
                  const char* ceContent = strchr(content, '"');
                  if (ceContent && ceContent < refEnd) {
                    outSeries->assign(content, ceContent - content);
                    break;
                  }
                }
                refScan = refEnd + 1;
              }
              // Look for group-position refines
              if (outSeriesIndex) {
                const char* posScan = opfStr;
                while ((posScan = strstr(posScan, refinesPat)) != nullptr && posScan < opfEnd) {
                  const char* posEnd = strchr(posScan, '>');
                  if (!posEnd) break;
                  const char* propPos = strstr(posScan, "property=\"group-position\"");
                  if (propPos && propPos < posEnd) {
                    const char* posContent = strstr(propPos, "content=\"");
                    if (posContent && posContent < posEnd) {
                      posContent += 9;
                      *outSeriesIndex = strtof(posContent, nullptr);
                    }
                    break;
                  }
                  posScan = posEnd + 1;
                }
              }
              if (!outSeries->empty()) contentValue = "1";  // mark as found
            }
          }
        }
      }

      scan = tagEnd + 1;
    }
  }

  free(opfData);
  return !outTitle.empty() || !outAuthor.empty();
}

} // namespace

bool extractMetadata(const std::string& epubPath, const std::string& cacheDir, std::string& outTitle, std::string& outAuthor,
                     std::string* outSeries, float* outSeriesIndex) {
  outTitle.clear();
  outAuthor.clear();

  if (readDirectFromZip(epubPath, outTitle, outAuthor, outSeries, outSeriesIndex)) {
    return !outTitle.empty() || !outAuthor.empty();
  }

  // Fallback to book.bin cache
  if (Storage.exists((cacheDir + "/book.bin").c_str())) {
    return readFromCache(cacheDir, outTitle, outAuthor);
  }
  return false;
}

} // namespace EpubParser
