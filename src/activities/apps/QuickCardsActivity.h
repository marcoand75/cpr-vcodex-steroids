#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class QuickCardsActivity final : public Activity {
 public:
  enum class CardType { IMAGE, QR, BARCODE };

  struct CardEntry {
    std::string path;
    std::string displayName;
    CardType type = CardType::IMAGE;
  };

  QuickCardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }

 private:
  enum class State { FILE_LIST, CARD_VIEW, CREATE_QR, CREATE_BARCODE, EMPTY };

  void scanDirectory();
  void loadCard(int index);
  void deleteCurrentCard();
  void deleteCurrentCardBmpCache();
  void navigateCard(int delta);

  // Rendering
  void drawHeaderWithIcon();
  void renderFileList();
  void renderImageView(const std::string& path, int index, int total);
  void renderQrCard(const std::string& primary, const std::string& description, int index, int total, const std::string& title);
  void renderBarcodeCard(const std::string& primary, const std::string& description, int index, int total, const std::string& title);
  void renderEmpty();

  // Convert JPEG/PNG to BMP and cache result on SD (returns path to cached BMP)
  std::string convertJpegToBmp(const std::string& sourcePath);
  std::string convertPngToBmp(const std::string& sourcePath);

  // Split QR/barcode text: first line = primary code, rest = description
  void splitCardText(const std::string& fullText, std::string& primary, std::string& description);

  State state = State::EMPTY;
  int selectedIndex = 0;
  bool fullscreenMode = false;

  std::vector<CardEntry> cards;
  std::string currentText;  // QR or barcode text content

  std::string keyboardInput;

  static constexpr const char* CARDS_DIR = "/cards";

  // Barcode rendering helpers
  uint16_t barcodeCodeC(uint8_t val);
  uint8_t barcodeChecksum(const uint8_t* vals, size_t n);
  void drawBarcode(const char* digits, int x, int y, int maxW, int maxH);
};
