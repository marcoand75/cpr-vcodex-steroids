# CPR-vCodex Steroids — Quick Cards App

> **SCOPE:** Complete technical reference for the image/QR/barcode viewer app.

---

## 1. Architecture Overview

```
QuickCardsActivity
    │
    ├─ Browses /cards/ on SD
    ├─ Renders BMP/JPEG/PNG with auto-scaling
    ├─ QR Code parser (10 formats)
    ├─ Code-128 barcode parser
    ├─ Cyberpunk panel file list
    └─ Fullscreen mode
```

---

## 2. Supported Formats

| Type | Formats | Notes |
|------|---------|-------|
| Images | BMP, JPEG, PNG | Auto-scaled to screen |
| QR Codes | Structured field parsing | Wi-Fi, vCard, MeCard, Geo, Email, Phone, SMS, OTP, Calendar, URL |
| Barcodes | Code-128 | Standard parsing |

---

## 3. QR Field Parser (`QrCardParser`)

### 3.1 Supported Formats (10)

| Format | Prefix | Extracted Fields |
|--------|--------|------------------|
| Wi-Fi | `WIFI:` | SSID, Password, Security (WPA/WEP/nopass) |
| vCard | `BEGIN:VCARD` | Name, Phone, Email, URL, Address |
| MeCard | `MECARD:` | Name, Phone, Email, URL, Address |
| Geo | `geo:` | Latitude, Longitude |
| Email | `mailto:` | To, Subject, Body |
| Phone | `tel:` | Number |
| SMS | `smsto:` | Number, Body |
| OTP | `otpauth://` | Issuer, Account, Secret, Algorithm, Digits, Period |
| Calendar | `BEGIN:VEVENT` | Summary, Description, Location, Start/End |
| URL | `http(s)://` | Full URL |

### 3.2 Output
- Sanitized to printable ASCII for e-ink fonts
- Structured display in QuickCardsActivity

---

## 3. Image Rendering

### 3.1 Pipeline
```
File → Decode (BMP/JPEG/PNG) → Scale (contain) → Dither (Atkinson, 4-level) → Display
```

### 3.2 Dithering
- Shared `DitheringConfig.h` (Atkinson, gamma 1.5, levels 0/85/170/255)
- Same pipeline as library covers / screensaver

---

## 4. UI

- **Cyberpunk panel file list** (left sidebar)
- **Fullscreen mode** (hides panel)
- **Navigation:** Up/Down = file list, Left/Right = image navigate
- **Zoom/pan:** Not implemented (fixed contain fit)

---

## 5. Key Files

| File | Role |
|------|------|
| `src/activities/apps/QuickCardsActivity.h/cpp` | Main activity |
| `src/util/QrCardParser.h` | QR structured parsing (10 formats) |
| `lib/JpegToBmpConverter/` | JPEG → BMP + dither |
| `lib/PngToBmpConverter/` | PNG → BMP + dither |
| `src/components/icons/quickcards.h` | App icon (32px) |
| `src/components/icons/quickcards24.h` | App icon (24px) |

---

## 6. Related Documents

- **App Registration:** `STEROIDS-ADDICTIONS.md` §3
- **Upstream Merge:** `STEROIDS-ALIGN-TO-UPSTREAM.md` (QuickCardsActivity protected)
- **Optimizations:** `STEROIDS-OPTIMIZATION.md` §8A (grayscale pipeline)
- **Grayscale Config:** `lib/GfxRenderer/DitheringConfig.h`

---

*Last updated: 2026-09-14*