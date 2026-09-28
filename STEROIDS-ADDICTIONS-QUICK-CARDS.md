# CPR-vCodex Steroids — Quick Cards (QR/Barcode/Image Viewer)

> **SCOPE:** Card management and display system for QR codes, barcodes, and images on SD. Low merge risk.

---

## 1. User-Facing Functionality

| Feature | Description |
|---------|-------------|
| **Card Directory** | `/cards/` on SD root; files: `.qrcard`, `.barcode`, `.bc`, image formats (.jpg, .png, .bmp) |
| **Image Cards** | JPEG/PNG auto-converted to BMP and cached for fast e-ink display |
| **QR Code Rendering** | Text → QR code (auto version 1-20, ECC_LOW, quiet zone 4 modules, min 3px/module) |
| **Code-128 Barcode** | Text → Code-128 barcode rendering (width-adaptive) |
| **Structured Parsing** | Auto-detects and displays fields for: WiFi, vCard, MeCard, Geo, mailto, tel, SMS/sms-to, OTPAuth (2FA), iCal event, URL, plain text |
| **Card Navigation** | Previous/next through directory listing; fullscreen mode toggle |
| **Delete** | Delete current card + its BMP cache |
| **Create** | In-screen text entry for new QR/barcode cards |

---

## 2. Technical Architecture

### 2.1 Card Types (`QuickCardsActivity::CardType`)
```cpp
enum class CardType { IMAGE, QR, BARCODE };

struct CardEntry {
    std::string path;         // File path on SD
    std::string displayName;  // Shown in file list
    CardType type = CardType::IMAGE;
};
```

### 2.2 Storage Layout
```
/cards/
├── my_contact.qrcard      # Text file: line 1 = primary code, rest = description
├── wifi_home.qrcard       # WiFi: SSID=Home;PWD=pass;T=WPA2
├── product_12345.barcode  # Barcode: 12345
├── photo.jpg              # Image (auto-converted to .bmp cache)
└── .cache/
    └── photo.bmp          # Generated BMP for e-ink display
```

### 2.3 QR Code Generation (`QrUtils.cpp`)
```cpp
// Library: QRCode (C library, version 1-20 supported)
// Mode: Byte (conservative capacity table for ECC_LOW)
// Output: Monochrome 1-bit bitmap (black modules on white)
// Sizing: Min 3px per module, quiet zone 4 modules each side
// Version selection: Smallest version that fits payload length

uint32_t bufferSize = qrcode_getBufferSize(version);
QRCode qrcode;
qrcode_initBytes(&qrcode, buf, version, ECC_LOW, payload, len, &status);
// Render: module grid → pixel buffer → display
```

### 2.4 Code-128 Barcode (`QuickCardsActivity`)
```cpp
// Self-contained Code-128 implementation (no external library)
// - barcodeCodeC(val): 11-bit pattern table (standard C encoding)
// - barcodeChecksum(vals, n): mod-103 check digit
// - drawBarcode(digits, x, y, maxW, maxH): width-adaptive rendering
//   - Bars: black (foreground) and spaces (background)
//   - Module width = maxW / total_modules
//   - Min: stops at 1 module if too narrow
```

### 2.5 QR Structured Parser (`QrCardParser.h`)
```cpp
// No heap allocation in hot paths (except display strings)
// Returns: QrCardParser::Result { format, displayTitle, fields[], rawText }

Recognized formats:
  WIFI      → WIFi:T:WPA;S:ssid;P:pass;;
  VCARD     → BEGIN:VCARD ... END:VCARD (FN, N, ORG, TEL, EMAIL, URL, ADR)
  MECARD    → MECARD:N:name;T:phone;E:email;U:url;A:addr
  GEO       → geo:lat,lon[,alt]
  EMAIL     → mailto:addr?subject=X&body=Y
  PHONE     → tel:number
  SMS       → sms:number / smsto:number:message
  OTPAUTH   → otpauth://totp/issuer:account?... (2FA display)
  EVENT     → iCal VEVENT (SUMMARY, DTSTART, DTEND, LOCATION)
  URL       → http(s):// (shows domain + full URL)
  TEXT      → Fallback: raw text truncated to 40 chars
```

### 2.6 Image Conversion Pipeline
```cpp
// convertJpegToBmp(sourcePath):
// 1. Check if .bmp cache exists in .cache/ → use it
// 2. If not: decode JPEG/PNG → 1-bit grayscale → save as BMP
// 3. Cache invalidation: deleteCurrentCardBmpCache() on card delete
```

---

## 3. File Inventory

| File | Role |
|------|------|
| `src/activities/apps/QuickCardsActivity.h/cpp` | Main activity: file list, card view, rendering (QR, barcode, image, structured display) |
| `src/util/QrCardParser.h` | Structured QR/barcode text parser (header-only, no heap in hot path) |
| `src/util/QrUtils.h/cpp` | QR code generation (byte mode, ECC_LOW, version auto-select) |
| `src/components/icons/quickcards.h` | Icon bitmap (PROGMEM) |
| `src/activities/apps/AppsActivity.cpp` | "Quick Cards" shortcut registration |
| `src/activities/home/HomeActivity.cpp` | "Quick Cards" shortcut in Home grid |
| `src/util/ShortcutRegistry.h` | Shortcut definition |

---

## 4. RAM/Flash Impact

| Metric | Value |
|--------|-------|
| Flash | +~40 KB (QR lib + activity + parser + barcode rendering) |
| RAM (idle) | Minimal |
| RAM (image card) | ~48 KB (BMP frame buffer, released after display) |
| RAM (QR/barcode) | ~5 KB (QR module buffer + display) |
| SD Cache | `/.cache/*.bmp` per image card (~48 KB each) |

---

## 5. Upstream Merge Notes

### LOW RISK
- `QuickCardsActivity` — New activity (self-contained)
- `QrCardParser` — New utility (header-only)
- `QrUtils` — New utility (QR generation)
- Shortcut registrations

### Conflicts Unlikely
- No upstream equivalent
- Isolated storage (`/cards/`)

### Safe Cherry-Picks
- `QrCardParser` structured parser (reusable for any QR scanning app)
- `QrUtils` QR generation (independent of activity)
- Code-128 rendering algorithm

---

## 6. Validation Checklist

- [ ] Card directory lists `.qrcard`, `.barcode`, `.bc`, image files
- [ ] Tap QR card → displays QR code + structured fields
- [ ] WiFi QR shows: SSID, Password, Security
- [ ] vCard QR shows: Name, Phone, Email, URL, Address
- [ ] OTP/2FA card shows: Account, Issuer
- [ ] Barcode card → Code-128 rendered correctly (scannable)
- [ ] Image card (JPEG) → converted to BMP, displayed on e-ink
- [ ] Previous/next navigation works
- [ ] Delete card + removes BMP cache
- [ ] Fullscreen mode toggle
- [ ] Create new card via in-screen keyboard
- [ ] Shortcut works from Apps/Home

---

## 7. Related Documents

- `STEROIDS-ADDICTIONS.md` — Main index
- `STEROIDS-ADDICTIONS-SHORTCUTS.md` — Quick Cards shortcut
- `STEROIDS-ADDICTIONS-FAST-RESTART.md` — QuickCards fast restart

---

*Last updated: 2026-09-28 — Corrected: QR/barcode/image card viewer (NOT flashcard/SM-2)*