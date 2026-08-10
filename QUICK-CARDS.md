# 🧬 Multi‑Device & Multi‑E‑Ink Display Support

> ⚠️ **Major infrastructure change — tested on physical X4 hardware.**

CPR‑vCodex Steroids now runs on **both Xteink X3 and X4 e‑readers** with
automatic board detection at boot time. This is the result of a full SDK
migration aligning with [CrossInk v1.5.0](https://github.com/uxjulia/CrossInk)
and the [freeink‑sdk](https://github.com/Free-Ink/freeink-sdk) ecosystem.

---

## What changed

| Before | After |
|--------|-------|
| `open-x4-sdk` (single device) | **`freeink‑sdk`** (multi‑device, same SDK as CrossInk) |
| Hardcoded X4 pinout | **Runtime board detection** — I2C fingerprint on BQ27220, DS3231, QMI8658 |
| No panel variant detection | **UC8279d probe** for newer X3 production runs |
| No SPI protection | **HalSpiBus** recursive mutex — safe for async refresh and future touch |
| Clock always shown | **X4 clock hidden** in status bar settings (no DS3231 RTC on X4) |

---

## Supported devices

| Device | Panel Controller | Resolution | Status |
|--------|-----------------|------------|--------|
| **Xteink X4** | SSD1677 | 800 × 480 | ✅ Tested on hardware |
| **Xteink X4 (repaired)** | UC8179 / UC8279 replacement | 800 × 480 | ✅ Detected at boot |
| **Xteink X3** | UC8253 | 792 × 528 | ✅ Full driver support |
| **Xteink X3 (new batch)** | UC8279d | 792 × 528 | ✅ Detected, compatible LUTs |

> 🔍 **UC8279 panel detection** — newer X3 units ship with a UC8279d controller
> instead of the original UC8253. The firmware bit‑bangs the panel registers
> before SPI initialization, identifies the controller, and stores the result.
> The existing UC8253 LUTs are compatible and work out of the box; a dedicated
> UC8279 LUT set can be added in the future for optimized refresh quality.

---

## Under the hood

| Component | What it does |
|-----------|-------------|
| 🔌 **freeink‑sdk** | Replaces `open-x4-sdk`. Board profiles for Xteink X3 and X4, panel driver abstraction (SSD1677 / UC8253 / UC8279), and runtime device selection. |
| 🧬 **BoardConfig** | Detects X3 vs X4 at boot via I2C fingerprinting. Sets the correct display pins, power‑rail latch, and controller profile before SPI initialization. |
| 🔍 **XteinkDetectExt** | Bit‑bangs UC81xx VER/FLG registers on the EPD bus to distinguish UC8253 from UC8279d panels on X3 hardware. Results cached in NVS with override support. |
| 🔒 **HalSpiBus** | Recursive FreeRTOS mutex protecting the shared SPI bus (display + SD card + future touch). RAII lock pattern — acquires automatically, releases on scope exit. |
| 🌐 **CrossPoint Compatible** | All upstream CrossPoint features (reading stats, achievements, KOReader sync, dictionary, flashcards) are fully preserved. The SDK migration is transparent to the reader experience. |

---

# 🃏 Quick Cards — Image & QR Code Viewer

> *Your wallet, boarding pass, and student ID — always ready on your e-reader.*

**Quick Cards** is a brand-new app that turns your Xteink into a lightweight
image and QR code viewer. Drop any image or QR code file onto the SD card and
browse it with physical buttons — no touchscreen, no phone, no fuss.

---

## 🚀 How to use it

1. **Put files on your SD card** inside the `/cards/` folder (auto‑created on first launch):
   - `.bmp`, `.jpg`, `.jpeg`, `.png` — any image (auto‑converted & cached for e‑ink)
   - `.qr` — a QR code stored as plain text inside the file
   - `.barcode` / `.bc` — a barcode (digits only, Code‑128)

2. **Open Quick Cards** from the Home screen or the Apps hub.

3. **Browse your cards** with the side buttons (Up/Down), open with Confirm,
   toggle fullscreen mode, navigate between cards.

---

## 📸 Image Viewer

| Feature | Detail |
|---------|--------|
| **BMP** | Direct rendering, scaled to fit the screen perfectly |
| **JPEG** | Auto‑converted to 1‑bit BMP on first open; cached as `.cache` for instant reuse |
| **PNG** | Converted via the same proven PNG decoder used by the Sleep Screen; cached on disk |
| **Fullscreen** | Press Confirm — only the image is visible, no UI chrome. Press any key to exit |
| **Cache management** | Left button on an image card deletes only the cached BMP, not your original file |

> 💡 *JPEG and PNG are converted to BMP the first time you open them and stored
> as a hidden `.cache` file — subsequent opens are **instant**!*

---

## 📱 QR Code Reader — 10 formats supported

Quick Cards includes **QrCardParser**, a lightweight decoder that understands
10 formats out of the box:

| Format | Example content | What you see |
|--------|----------------|--------------|
| 🔐 **Wi‑Fi** | `WIFI:T:WPA;S:MyNetwork;P:pass123;;` | SSID, Password, Security type |
| 👤 **vCard** | `BEGIN:VCARD … END:VCARD` | Name, Phone, Email, URL, Address |
| 📇 **MeCard** | `MECARD:N:Smith,John;TEL:…;;` | Name, Phone, Email, Note, URL, Address |
| 📍 **Geo** | `geo:45.4642,9.1900` | Latitude, Longitude |
| ✉️ **Email** | `mailto:name@domain.com?subject=…` | Email address, Subject |
| 📞 **Phone** | `tel:+390212345678` | Phone number |
| 💬 **SMS** | `sms:+390212345678:Hello there` | Number, Message |
| 🔑 **2FA / OTP** | `otpauth://totp/GitHub:user?…` | Account name, Issuer |
| 📅 **Calendar** | `BEGIN:VEVENT … END:VEVENT` | Summary, Start/End dates, Location, Description |
| 🌐 **URL** | `https://github.com/marcoand75/…` | Full URL, Domain |

The QR code is rendered in the upper portion of the screen, with structured
fields shown below — clean, centered, and easy to read on e‑ink.

---

## 🔢 Barcode Cards

Numeric barcodes (Code‑128, up to 40 digits) are rendered at **⅓ of the screen
height**, centered vertically. Human‑readable digits appear below the bars.
Perfect for loyalty cards, student IDs, and ticket barcodes.

---

## 🎨 Cyberpunk‑style Interface

The card list uses the same **cyberpunk panel design** as the Wikipedia app and
the Lyra home screen — dark selected panels with crisp white borders and bold
text, fast and futuristic on e‑ink.

---

## 🖥️ Fullscreen Mode

Press **Confirm** on any card: only the image, QR code, or barcode is visible.
No header, no footer, no button hints. Press **any physical button** to exit
back to normal view. The filename is shown centered at the bottom.

---

## 📂 What's on your SD card

```
/cards/
├── boarding-pass.jpg        → shows your flight ticket
├── student-id.png           → shows your ID badge
├── wifi-hotel.qr            → scanned: SSID, password, security
├── tessera-sanitaria.bc     → scanned: Code‑128 barcode
├── boarding-pass.jpg.cache  → hidden BMP cache (auto‑generated)
└── student-id.png.cache     → hidden BMP cache (auto‑generated)
```

---

## 🎯 Why Quick Cards?

- **No phone needed** — keep your digital cards on the device you already carry
- **Always ready** — e‑ink screen stays on forever, no battery drain
- **Physical buttons** — navigate with side buttons, no touchscreen required
- **Offline** — everything is stored locally on your SD card
- **Open** — plain text files for QR/barcode, standard image formats, no proprietary format
- **Fast** — images cached after first open, instant reload

---

*Quick Cards is part of CPR‑vCodex Steroids. For the complete feature list,
see the [README](https://github.com/marcoand75/cpr-vcodex-steroids#readme)
and the [full documentation](https://github.com/marcoand75/cpr-vcodex-steroids/blob/master/STEROIDS-ADDICTIONS.md).*
