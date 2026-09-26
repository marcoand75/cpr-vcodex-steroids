#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <ArduinoJson.h>
#include <I18n.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class CrossPointSettings;

// Web settings types (shared with Settings API)
enum class WebSettingType : uint8_t { Toggle, Enum, Value, String };
enum class WebDynamicSetting : uint8_t { None, KoUsername, KoPassword, KoServerUrl, KoMatchMethod };

struct WebSettingDef {
  StrId nameId;
  StrId category;
  WebSettingType type;
  uint8_t CrossPointSettings::* valuePtr;
  const StrId* options;
  uint8_t optionCount;
  uint8_t min;
  uint8_t max;
  uint8_t step;
  WebDynamicSetting dynamic;
  const char* key;
};

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
  bool completed;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::array<uint8_t, UPLOAD_BUFFER_SIZE> buffer{};
    size_t bufferPos = 0;
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  using FileVisitor = void (*)(const FileInfo& info, void* context);
  void scanFiles(const char* path, FileVisitor visitor, void* context) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleLogo() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload(UploadState& state) const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();

  // Steroids settings handlers
  void handleSteroidsSettingsPage() const;
  void handleGetSteroidsSettings() const;
  void handlePostSteroidsSettings();

  // Wi-Fi credentials handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // If Found contact-card handlers
  void handleIfFoundPage() const;
  void handleGetIfFound() const;
  void handlePostIfFound();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::array<uint8_t, BUFFER_SIZE> buffer{};
    size_t bufferPos = 0;
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Steroids settings helper functions
  void addSteroidsSetting(const char* key, StrId nameId, const char* category,
                          WebSettingType type, int value, const std::vector<const char*>& options,
                          uint8_t CrossPointSettings::* valuePtr, bool& seenFirst) const;
  void addSteroidsSettingBuffered(const char* key, StrId nameId, const char* category,
                                  WebSettingType type, int value, const std::vector<const char*>& options,
                                  uint8_t CrossPointSettings::* valuePtr, bool& seenFirst) const;
  void applySteroidsSetting(JsonDocument& doc, const char* key,
                            uint8_t CrossPointSettings::* valuePtr, uint8_t maxValue,
                            bool& saveSettings, int& applied);
};
