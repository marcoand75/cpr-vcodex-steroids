#include "QuickCardsActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "FlashcardsStore.h"
#include "components/PanelDrawHelper.h"
#include "components/UITheme.h"
#include "components/icons/quickcards.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/QrCardParser.h"
#include "util/QrUtils.h"
#include <JpegToBmpConverter.h>


// ─────────────────────────────────────────────────────────────────────────────
// Code-128 barcode helpers
// ─────────────────────────────────────────────────────────────────────────────

uint16_t QuickCardsActivity::barcodeCodeC(uint8_t val) {
    static const uint16_t p[] = {
        0b11011001100, 0b11001101100, 0b11001100110, 0b10010011000, 0b10010001100,
        0b10001001100, 0b10011001000, 0b10011000100, 0b10001100100, 0b11001001000,
        0b11001000100, 0b11000100100, 0b10110011100, 0b10011011100, 0b10011001110,
        0b10111001100, 0b10011101100, 0b10011100110, 0b11001110010, 0b11001011100,
        0b11001001110, 0b11011100100, 0b11001110100, 0b11101101110, 0b11101001100,
        0b11100101100, 0b11100100110, 0b11101100100, 0b11100110100, 0b11100110010,
        0b11011011000, 0b11011000110, 0b11000110110, 0b10100011000, 0b10001011000,
        0b10001000110, 0b10110001000, 0b10001101000, 0b10001100010, 0b11010001000,
        0b11000101000, 0b11000100010, 0b10110111000, 0b10110001110, 0b10001101110,
        0b10111011000, 0b10111000110, 0b10001110110, 0b11101110110, 0b11010001110,
        0b11000101110, 0b11011101000, 0b11011100010, 0b11011101110, 0b11101011000,
        0b11101000110, 0b11100010110, 0b11101101000, 0b11101100010, 0b11100011010,
        0b11101111010, 0b11001000010, 0b11110001010, 0b10100110000, 0b10100001100,
        0b10010110000, 0b10010000110, 0b10000101100, 0b10000100110, 0b10110010000,
        0b10110000100, 0b10011010000, 0b10011000010, 0b10000110100, 0b10000110010,
        0b11000010010, 0b11001010000, 0b11110111010, 0b11000010100, 0b10001111010,
        0b10100111100, 0b10010111100, 0b10010011110, 0b10111100100, 0b10011110100,
        0b10011110010, 0b11110100100, 0b11110010100, 0b11110010010, 0b11011011110,
        0b11011110110, 0b11110110110, 0b10101111000, 0b10100011110, 0b10001011110,
        0b10111101000, 0b10111100010, 0b11110101000, 0b11110100010, 0b10111011110
    };
    return (val <= 99) ? p[val] : p[0];
}

uint8_t QuickCardsActivity::barcodeChecksum(const uint8_t* vals, size_t n) {
    uint32_t sum = 105;
    for (size_t i = 0; i < n; ++i) sum += vals[i] * static_cast<uint32_t>(i + 1);
    return static_cast<uint8_t>(sum % 103);
}

void QuickCardsActivity::drawBarcode(const char* digits, int x, int y, int maxW, int maxH) {
    auto len = strlen(digits);
    if (len == 0 || len > 40) return;

    // Validate: Code-128C requires even number of digits
    bool allDigits = true;
    for (size_t i = 0; i < len; ++i) {
        if (digits[i] < '0' || digits[i] > '9') { allDigits = false; break; }
    }
    if (!allDigits || (len % 2) != 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, y + maxH / 2 - 10, "Invalid barcode");
        renderer.drawCenteredText(UI_10_FONT_ID, y + maxH / 2 + 4, "digits only, even length");
        return;
    }

    std::vector<uint8_t> sym;
    sym.reserve(len / 2 + 3);
    sym.push_back(105);

    for (size_t i = 0; i < len; i += 2)
        sym.push_back(static_cast<uint8_t>((digits[i] - '0') * 10 + (digits[i + 1] - '0')));

    sym.push_back(barcodeChecksum(sym.data() + 1, sym.size() - 1));
    sym.push_back(106);

    std::vector<bool> bars;
    for (size_t s = 0; s < sym.size(); ++s) {
        uint32_t pat; uint8_t bits;
        if (s == 0)                  { pat = 0b11010011100;   bits = 11; }
        else if (s == sym.size() - 1){ pat = 0b1100011101011; bits = 13; }
        else                         { pat = barcodeCodeC(sym[s]); bits = 11; }
        for (int b = bits - 1; b >= 0; --b) bars.push_back((pat >> b) & 1);
    }

    int mw = std::max(1, maxW / static_cast<int>(bars.size()));
    int tw = mw * static_cast<int>(bars.size());
    int sx = x + (maxW - tw) / 2;
    int uh = maxH - 26;

    for (size_t i = 0; i < bars.size(); ++i)
        if (!bars[i]) renderer.fillRect(sx + static_cast<int>(i) * mw, y, mw, uh, true);

    int tw2 = renderer.getTextWidth(SMALL_FONT_ID, digits);
    renderer.drawText(SMALL_FONT_ID, x + (maxW - tw2) / 2, y + uh + 4, digits, true, EpdFontFamily::REGULAR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

QuickCardsActivity::QuickCardsActivity(GfxRenderer& r, MappedInputManager& m)
    : Activity("QuickCards", r, m) {}

void QuickCardsActivity::onEnter() {
    Activity::onEnter();
    FLASHCARDS.ensureLoaded();
    selectedIndex = 0; fullscreenMode = false;
    cards.clear(); currentText.clear();
    scanDirectory(); requestUpdate();
}

void QuickCardsActivity::onExit() {
    cards.clear(); currentText.clear(); Activity::onExit();
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory & Data Handling
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::scanDirectory() {
    cards.clear();
    Storage.ensureDirectoryExists(CARDS_DIR);
    auto dir = Storage.open(CARDS_DIR);
    if (!dir || !dir.isDirectory()) { if(dir) dir.close(); state = State::EMPTY; return; }

    char name[256];
    std::vector<CardEntry> found;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
        if (!file.isDirectory()) {
            file.getName(name, sizeof(name));
            const char* ext = strrchr(name, '.');
            if (!ext) { file.close(); continue; }

            CardEntry card;
            card.path = std::string(CARDS_DIR) + "/" + name;
            card.displayName = name;

            // JPG/BMP support verified and restored
            if (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
                card.type = CardType::IMAGE;
            else if (strcasecmp(ext, ".qr") == 0)
                card.type = CardType::QR;
            else if (strcasecmp(ext, ".barcode") == 0 || strcasecmp(ext, ".bc") == 0)
                card.type = CardType::BARCODE;
            else { file.close(); continue; }

            found.push_back(std::move(card));
        }
        file.close();
    }
    dir.close();
    cards = std::move(found);
    state = cards.empty() ? State::EMPTY : State::FILE_LIST;
    if (selectedIndex >= static_cast<int>(cards.size())) selectedIndex = 0;
}

void QuickCardsActivity::splitCardText(const std::string& fullText, std::string& primary, std::string& description) {
    // First try double-newline separator (preserves multi-line QR payloads like vCard/iCal)
    auto dnl = fullText.find("\n\n");
    if (dnl != std::string::npos) {
        primary = fullText.substr(0, dnl);
        description = fullText.substr(dnl + 2);
        while (!primary.empty() && (primary.back() == '\n' || primary.back() == '\r')) primary.pop_back();
        while (!description.empty() && (description.back() == '\n' || description.back() == '\r')) description.pop_back();
        return;
    }
    // No double newline: for multi-line known formats keep the whole text as primary
    if (fullText.size() >= 11 && fullText.compare(0, 11, "BEGIN:VCARD") == 0) {
        primary = fullText;
        description.clear();
        return;
    }
    if (fullText.size() >= 12 && fullText.compare(0, 12, "BEGIN:VEVENT") == 0) {
        primary = fullText;
        description.clear();
        return;
    }
    // Fallback: split at first newline (backward compatible with single-line QR + description)
    auto nl = fullText.find('\n');
    if (nl != std::string::npos) {
        primary = fullText.substr(0, nl);
        description = fullText.substr(nl + 1);
        while (!primary.empty() && primary.back() == '\r') primary.pop_back();
        while (!description.empty() && (description.back() == '\n' || description.back() == '\r')) description.pop_back();
    } else {
        primary = fullText;
        description.clear();
    }
}

void QuickCardsActivity::loadCard(int index) {
    if (index < 0 || index >= static_cast<int>(cards.size())) return;
    selectedIndex = index;
    const auto& card = cards[static_cast<size_t>(index)];
    if (card.type == CardType::QR || card.type == CardType::BARCODE) {
        currentText.clear();
        auto f = Storage.open(card.path.c_str());
        if (f) {
            char buf[1024] = {0};
            size_t read = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
            f.close();
            size_t end = read;
            while (end > 0 && (buf[end-1]=='\n'||buf[end-1]=='\r'||buf[end-1]==' ')) buf[--end]=0;
            currentText = buf;
        }
    }
}

void QuickCardsActivity::deleteCurrentCard() {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(cards.size())) return;
    Storage.remove(cards[static_cast<size_t>(selectedIndex)].path.c_str());
    scanDirectory(); requestUpdate();
}

void QuickCardsActivity::deleteCurrentCardBmpCache() {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(cards.size())) return;
    std::string cp = cards[static_cast<size_t>(selectedIndex)].path + ".cache";
    if (Storage.exists(cp.c_str())) { Storage.remove(cp.c_str()); LOG_INF("[QCRD]","Removed BMP cache"); }
    requestUpdate();
}

void QuickCardsActivity::navigateCard(int delta) {
    int ni = selectedIndex + delta;
    if (ni < 0 || ni >= static_cast<int>(cards.size())) return;
    loadCard(ni); requestUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::loop() {
    Activity::loop();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        if (state==State::FILE_LIST||state==State::EMPTY) { finish(); return; }
        if (state==State::CARD_VIEW) { if(!fullscreenMode){state=State::FILE_LIST;requestUpdate();}return; }
        if (state==State::CREATE_QR||state==State::CREATE_BARCODE) { state=cards.empty()?State::EMPTY:State::FILE_LIST;requestUpdate();return; }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (state==State::FILE_LIST&&!cards.empty()) { loadCard(selectedIndex); state=State::CARD_VIEW;requestUpdate();return; }
        if (state==State::CARD_VIEW) { fullscreenMode=!fullscreenMode;requestUpdate();return; }
    }
    if (state==State::CARD_VIEW&&fullscreenMode) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)||mappedInput.wasReleased(MappedInputManager::Button::Confirm)||
            mappedInput.wasReleased(MappedInputManager::Button::Left)||mappedInput.wasReleased(MappedInputManager::Button::Right)||
            mappedInput.wasReleased(MappedInputManager::Button::Up)||mappedInput.wasReleased(MappedInputManager::Button::Down)) { fullscreenMode=false;requestUpdate();return; }
        return;
    }
    if (state==State::CARD_VIEW) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Left)||mappedInput.wasReleased(MappedInputManager::Button::Up)){navigateCard(-1);return;}
        if (mappedInput.wasReleased(MappedInputManager::Button::Right)||mappedInput.wasReleased(MappedInputManager::Button::Down)){navigateCard(1);return;}
    }
    if (state==State::FILE_LIST) {
        int c=static_cast<int>(cards.size());
        if(c>0){
            if(mappedInput.wasReleased(MappedInputManager::Button::Up)){selectedIndex=(selectedIndex>0)?selectedIndex-1:c-1;requestUpdate();}
            if(mappedInput.wasReleased(MappedInputManager::Button::Down)){selectedIndex=(selectedIndex+1)%c;requestUpdate();}
        }
    }
    if (state==State::FILE_LIST&&mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        if(selectedIndex>=0&&selectedIndex<static_cast<int>(cards.size())) {
            if(cards[static_cast<size_t>(selectedIndex)].type==CardType::IMAGE) deleteCurrentCardBmpCache();
            else deleteCurrentCard();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Dispatcher
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::render(RenderLock&&) {
    switch(state) {
        case State::FILE_LIST: renderFileList(); break;
        case State::CARD_VIEW: {
            if(selectedIndex<0||selectedIndex>=static_cast<int>(cards.size()))return;
            const auto& card=cards[static_cast<size_t>(selectedIndex)];
            std::string primary, desc; splitCardText(currentText, primary, desc);
            switch(card.type) {
                case CardType::IMAGE: renderImageView(card.path,selectedIndex,static_cast<int>(cards.size()));break;
                case CardType::QR:    renderQrCard(primary,desc,selectedIndex,static_cast<int>(cards.size()),card.displayName);break;
                case CardType::BARCODE: renderBarcodeCard(primary,desc,selectedIndex,static_cast<int>(cards.size()),card.displayName);break;
            }
            break;
        }
        case State::EMPTY: renderEmpty(); break;
        case State::CREATE_QR: case State::CREATE_BARCODE: renderer.displayBuffer(); break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Components
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::drawHeaderWithIcon() {
    HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
    const auto& m=UITheme::getInstance().getMetrics();
    const int iconSize=32, iconY=m.topPadding+12;
    renderer.drawIcon(QuickCardsIcon,20,iconY,iconSize,iconSize);
    int lh=renderer.getLineHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID,20+iconSize+10,iconY+(iconSize-lh)/2,tr(STR_QUICK_CARDS),true,EpdFontFamily::BOLD);
}

void QuickCardsActivity::renderFileList() {
    const auto& m=UITheme::getInstance().getMetrics();
    const auto pw=renderer.getScreenWidth(),ph=renderer.getScreenHeight();
    renderer.clearScreen(); drawHeaderWithIcon();
    const int margin=20,iconSize=32,contentTop=m.topPadding+12+iconSize+16;
    const int btnHintsTop=ph-m.buttonHintsHeight-8,availH=btnHintsTop-contentTop-12;
    const int panelW=pw-margin*2,panelH=52,panelGap=10;
    const int visible=std::max(1,availH/(panelH+panelGap));
    int total=static_cast<int>(cards.size());
    int first=std::max(0,std::min(selectedIndex-visible/2,total-visible));

    for(int i=first;i<total&&i<first+visible;++i){
        int idx=i-first,px=margin,py=contentTop+idx*(panelH+panelGap);
        bool sel=(i==selectedIndex);
        renderer.fillRect(px,py,panelW,panelH,sel?1:0);
        PanelDrawHelper::drawCyberpunkPanel(renderer,px,py,panelW,panelH,sel);
        const char* badge="";
        switch(cards[static_cast<size_t>(i)].type){case CardType::IMAGE:badge="[IMG] ";break;case CardType::QR:badge="[QR]  ";break;case CardType::BARCODE:badge="[BAR] ";break;}
        std::string nm=cards[static_cast<size_t>(i)].displayName;
        auto dp=nm.find_last_of('.'); if(dp!=std::string::npos)nm=nm.substr(0,dp);
        std::string label=badge+nm;
        int maxTW=panelW-32,tw=renderer.getTextWidth(UI_12_FONT_ID,label.c_str());
        while(tw>maxTW&&label.length()>4){label=label.substr(0,label.length()-4)+"...";tw=renderer.getTextWidth(UI_12_FONT_ID,label.c_str());}
        int lh=renderer.getLineHeight(UI_12_FONT_ID); bool tb=!sel;
        renderer.drawText(UI_12_FONT_ID,px+16,py+(panelH-lh)/2,label.c_str(),tb,EpdFontFamily::BOLD);
    }
    auto labels=mappedInput.mapLabels(tr(STR_BACK),tr(STR_SELECT),tr(STR_QUICK_CARDS_DELETE),tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    renderer.displayBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Image Card (JPG/BMP Support Verified)
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::renderImageView(const std::string& path, int index, int total) {
    const auto& m=UITheme::getInstance().getMetrics();
    const auto pw=renderer.getScreenWidth(),ph=renderer.getScreenHeight();
    renderer.clearScreen();
    
    std::string bmpPath=path;
    const char* ext=strrchr(path.c_str(),'.');
    
    // Conversione JPG -> BMP (con cache)
    if(ext && (strcasecmp(ext,".jpg")==0 || strcasecmp(ext,".jpeg")==0)){
        bmpPath = convertJpegToBmp(path);
        if(bmpPath.empty()) goto fail_conv;
    }
    // I BMP vengono usati direttamente senza conversione

    {
        auto file=Storage.open(bmpPath.c_str());
        if(!file) goto fail_open;
        
        Bitmap bitmap(file,true);
        if(bitmap.parseHeaders()!=BmpReaderError::Ok){
            file.close();
            goto fail_open;
        }
        
        int headerH=fullscreenMode?0:m.topPadding+12+32+16;
        int footerH=fullscreenMode?0:m.buttonHintsHeight+8;
        int availW=pw,availH=ph-headerH-footerH,startY=fullscreenMode?0:headerH;
        int x,y;
        
        // Scaling proporzionale centrato
        if(bitmap.getWidth()>availW || bitmap.getHeight()>availH){
            float ratio=(float)bitmap.getWidth()/bitmap.getHeight();
            float sr=(float)availW/availH;
            if(ratio>sr){
                x=0;
                y=startY+std::round((availH-availW/ratio)/2.0f);
            } else {
                x=std::round((availW-availH*ratio)/2.0f);
                y=startY;
            }
        } else {
            x=(availW-bitmap.getWidth())/2;
            y=startY+(availH-bitmap.getHeight())/2;
        }
        
        renderer.drawBitmap(bitmap,x,y,availW,availH,0,0);
        file.close();
    }

    // UI Overlay
    if(!fullscreenMode){
        drawHeaderWithIcon();
        char buf[32]; snprintf(buf,sizeof(buf),"%d/%d",index+1,total);
        renderer.drawText(SMALL_FONT_ID,10,ph-m.buttonHintsHeight-24,buf,true);
        auto labels=mappedInput.mapLabels(tr(STR_BACK),tr(STR_FULLSCREEN_LABEL),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    } else {
        // Solo nome file in basso in fullscreen
        std::string nm=cards[static_cast<size_t>(selectedIndex)].displayName;
        auto dp=nm.find_last_of('.');if(dp!=std::string::npos)nm=nm.substr(0,dp);
        int tw=renderer.getTextWidth(UI_12_FONT_ID,nm.c_str());
        renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,ph-30,nm.c_str(),true,EpdFontFamily::BOLD);
    }
    
    renderer.displayBuffer(HalDisplay::FAST_REFRESH); 
    return;

fail_conv: 
    renderer.drawCenteredText(UI_10_FONT_ID,ph/2,"Image conversion failed"); 
    goto draw_btns;
fail_open: 
    renderer.drawCenteredText(UI_10_FONT_ID,ph/2,"Cannot open image");
draw_btns: 
    {
        auto labels=mappedInput.mapLabels(tr(STR_BACK),"","","");
        GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    }
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// ─────────────────────────────────────────────────────────────────────────────
// QR Card - Enhanced Readability
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::renderQrCard(const std::string& primary, const std::string& description, int index, int total, const std::string& title) {
    const auto& m=UITheme::getInstance().getMetrics();
    const auto pw=renderer.getScreenWidth(),ph=renderer.getScreenHeight();
    renderer.clearScreen();

    int headerH=fullscreenMode?0:m.topPadding+12+32+16;
    int footerH=fullscreenMode?0:m.buttonHintsHeight+8;
    int titleH=30;
    int availH=ph-headerH-footerH-titleH-m.verticalSpacing,availW=pw-40;
    int startY=fullscreenMode?titleH:headerH+m.verticalSpacing;
    int qrAreaH=availH*40/100;  // smaller QR to give more room for fields
    int descAreaH=availH-qrAreaH;

    if(!fullscreenMode) drawHeaderWithIcon();

    // Title (Normal mode only)
    if(!fullscreenMode && !title.empty()){
        std::string dt=title; auto dp=dt.find_last_of('.');if(dp!=std::string::npos)dt=dt.substr(0,dp);
        int tw=renderer.getTextWidth(UI_12_FONT_ID,dt.c_str());
        while(tw>pw-40&&dt.length()>4){dt=dt.substr(0,dt.length()-4)+"...";tw=renderer.getTextWidth(UI_12_FONT_ID,dt.c_str());}
        renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,startY,dt.c_str(),true,EpdFontFamily::BOLD);
        startY+=titleH;
    }

    // QR Code
    int qrSize=std::min(availW,qrAreaH);
    Rect qrBounds((pw-qrSize)/2,startY+(qrAreaH-qrSize)/2,qrSize,qrSize);
    QrUtils::drawQrCode(renderer,qrBounds,primary);

    // Formatted Content Display via QrCardParser
    int textY=startY+qrAreaH+8;
    auto parsed = QrCardParser::parse(primary);

    bool isText = (std::strcmp(parsed.format, "TEXT") == 0 || std::strcmp(parsed.format, "EMPTY") == 0);

    if (!isText && !parsed.fields.empty()) {
      // Format tag (bold, small)
      int fmtW = renderer.getTextWidth(SMALL_FONT_ID, parsed.format);
      renderer.drawText(SMALL_FONT_ID, (pw-fmtW)/2, textY, parsed.format, true, EpdFontFamily::BOLD);
      textY += renderer.getLineHeight(SMALL_FONT_ID) + 2;

      // Display title (sanitized)
      if (!parsed.displayTitle.empty()) {
        std::string st = QrCardParser::sanitize(parsed.displayTitle);
        int tw = renderer.getTextWidth(UI_12_FONT_ID, st.c_str());
        renderer.drawText(UI_12_FONT_ID, (pw-tw)/2, textY, st.c_str(), true, EpdFontFamily::BOLD);
        textY += renderer.getLineHeight(UI_12_FONT_ID) + 4;
      }

      // Fields table (label: value) — allow multi-line wrap for long values
      int lh = renderer.getLineHeight(UI_12_FONT_ID);
      for (const auto& f : parsed.fields) {
        if (textY + lh >= ph - footerH - 10) break;
        std::string row = std::string(f.label) + ": " + QrCardParser::sanitize(f.value);
        auto lines = renderer.wrappedText(UI_12_FONT_ID, row.c_str(), pw-40, 3);
        for (size_t i = 0; i < lines.size() && textY + lh < ph - footerH - 10; ++i) {
          int tw = renderer.getTextWidth(UI_12_FONT_ID, lines[i].c_str());
          renderer.drawText(UI_12_FONT_ID, (pw-tw)/2, textY, lines[i].c_str(), true);
          textY += lh + 1;
        }
        textY += 2;  // gap between fields
      }
    } else {
      // Unknown format: show raw text wrapped (sanitized)
      std::string raw = QrCardParser::sanitize(parsed.rawText.empty() ? primary : parsed.rawText);
      if (!raw.empty()) {
        auto lines = renderer.wrappedText(UI_12_FONT_ID, raw.c_str(), pw-40, 6);
        int lh = renderer.getLineHeight(UI_12_FONT_ID);
        for (size_t i=0; i<lines.size() && textY+static_cast<int>(i)*lh < ph-footerH-10; ++i) {
          int tw = renderer.getTextWidth(UI_12_FONT_ID, lines[i].c_str());
          renderer.drawText(UI_12_FONT_ID, (pw-tw)/2, textY+static_cast<int>(i)*lh, lines[i].c_str(), true);
        }
      }
    }

    // Description from file (second line of .qr file) — shown below parsed fields
    if (!description.empty()) {
      std::string cleanDesc = QrCardParser::sanitize(description);
      while (!cleanDesc.empty() && (cleanDesc.back()=='\n' || cleanDesc.back()=='\r')) cleanDesc.pop_back();
      if (!cleanDesc.empty()) {
        textY += 4;
        auto lines = renderer.wrappedText(SMALL_FONT_ID, cleanDesc.c_str(), pw-40, 4);
        int lh = renderer.getLineHeight(SMALL_FONT_ID);
        for (size_t i=0; i<lines.size() && textY+static_cast<int>(i)*lh < ph-footerH-10; ++i) {
          int tw = renderer.getTextWidth(SMALL_FONT_ID, lines[i].c_str());
          renderer.drawText(SMALL_FONT_ID, (pw-tw)/2, textY+static_cast<int>(i)*lh, lines[i].c_str(), true);
        }
      }
    }

    // Footer / Fullscreen Title
    if(!fullscreenMode){
        char buf[32];snprintf(buf,sizeof(buf),"%d/%d",index+1,total);
        renderer.drawText(SMALL_FONT_ID,10,ph-footerH-16,buf,true);
        auto labels=mappedInput.mapLabels(tr(STR_BACK),tr(STR_FULLSCREEN_LABEL),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    } else {
        std::string nm=title; auto dp=nm.find_last_of('.');if(dp!=std::string::npos)nm=nm.substr(0,dp);
        int tw=renderer.getTextWidth(UI_12_FONT_ID,nm.c_str());
        renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,ph-30,nm.c_str(),true,EpdFontFamily::BOLD);
    }
    renderer.displayBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Barcode Card
// ─────────────────────────────────────────────────────────────────────────────

void QuickCardsActivity::renderBarcodeCard(const std::string& primary, const std::string& description, int index, int total, const std::string& title) {
    const auto& m=UITheme::getInstance().getMetrics();
    const auto pw=renderer.getScreenWidth(),ph=renderer.getScreenHeight();
    renderer.clearScreen();

    int headerH=fullscreenMode?0:m.topPadding+12+32+16;
    int footerH=fullscreenMode?0:m.buttonHintsHeight+8;
    int titleH=fullscreenMode?0:30;
    int availW=pw-60;
    int barcodeH=fullscreenMode?ph/3:(ph-headerH-footerH-titleH-m.verticalSpacing)/3;
    int startY=fullscreenMode?(ph-barcodeH)/2:headerH+m.verticalSpacing;

    if(!fullscreenMode) drawHeaderWithIcon();

    if(!fullscreenMode && !title.empty()){
        std::string dt=title; auto dp=dt.find_last_of('.');if(dp!=std::string::npos)dt=dt.substr(0,dp);
        int tw=renderer.getTextWidth(UI_12_FONT_ID,dt.c_str());
        while(tw>pw-40&&dt.length()>4){dt=dt.substr(0,dt.length()-4)+"...";tw=renderer.getTextWidth(UI_12_FONT_ID,dt.c_str());}
        renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,startY,dt.c_str(),true,EpdFontFamily::BOLD);
        startY+=titleH;
    }

    drawBarcode(primary.c_str(),30,startY,availW,barcodeH);

    int textY=startY+barcodeH+16;
    if(!description.empty()){
        auto lines=renderer.wrappedText(UI_12_FONT_ID,description.c_str(),pw-40,8);
        int lh=renderer.getLineHeight(UI_12_FONT_ID);
        for(size_t i=0;i<lines.size()&&textY+static_cast<int>(i)*lh<ph-footerH-10;++i){
            int tw=renderer.getTextWidth(UI_12_FONT_ID,lines[i].c_str());
            renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,textY+static_cast<int>(i)*lh,lines[i].c_str(),true,EpdFontFamily::REGULAR);
        }
    }

    if(!fullscreenMode){
        char buf[32];snprintf(buf,sizeof(buf),"%d/%d",index+1,total);
        renderer.drawText(SMALL_FONT_ID,10,ph-footerH-16,buf,true);
        auto labels=mappedInput.mapLabels(tr(STR_BACK),tr(STR_FULLSCREEN_LABEL),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    } else {
        std::string nm=title; auto dp=nm.find_last_of('.');if(dp!=std::string::npos)nm=nm.substr(0,dp);
        int tw=renderer.getTextWidth(UI_12_FONT_ID,nm.c_str());
        renderer.drawText(UI_12_FONT_ID,(pw-tw)/2,ph-30,nm.c_str(),true,EpdFontFamily::BOLD);
    }
    renderer.displayBuffer();
}

void QuickCardsActivity::renderEmpty() {
    const auto pw=renderer.getScreenWidth(),ph=renderer.getScreenHeight();
    renderer.clearScreen(); drawHeaderWithIcon();
    int mw=renderer.getTextWidth(UI_10_FONT_ID,tr(STR_QUICK_CARDS_NO_FILES));
    renderer.drawText(UI_10_FONT_ID,(pw-mw)/2,ph/2,tr(STR_QUICK_CARDS_NO_FILES),true);
    auto labels=mappedInput.mapLabels(tr(STR_BACK),"","","");
    GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
    renderer.displayBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// JPEG → BMP Converter with Cache
// ─────────────────────────────────────────────────────────────────────────────

std::string QuickCardsActivity::convertJpegToBmp(const std::string& sourcePath) {
    std::string cachePath = sourcePath + ".cache";
    if (Storage.exists(cachePath.c_str())) return cachePath;

    FsFile jpegFile = Storage.open(sourcePath.c_str());
    if (!jpegFile) return {};

    FsFile outFile = Storage.open(cachePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!outFile) {
        jpegFile.close();
        return {};
    }

    int sw = renderer.getScreenWidth();
    int sh = renderer.getScreenHeight();
    bool ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(jpegFile, outFile, sw, sh);

    jpegFile.close();
    outFile.close();

    if (!ok) {
        Storage.remove(cachePath.c_str());
        return {};
    }
    return cachePath;
}