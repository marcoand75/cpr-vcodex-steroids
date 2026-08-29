# Quick Cards — Reference Cards on Your E-Reader

This folder contains reference cards for the **Quick Cards** app.
Drop image files, QR code text files, and barcode text files here and browse
them with the physical buttons on your Xteink device.

---

## File formats

### Images

| Extension | Status | Notes |
|-----------|--------|-------|
| `.jpg` / `.jpeg` | **Recommended** | Auto-converted to 1-bit BMP on first open and cached as `.cache` for instant reload |
| `.bmp` | Supported | Rendered directly without conversion |
| `.png` | Supported via converter | Converted and cached the same way as JPEG |

**Recommended:** use `.jpg` / `.jpeg` for images. They produce smaller files on
the SD card and the on-device converter handles them reliably.

### QR codes (`.qr` files)

A `.qr` file is plain text.

- The **QR payload** is the first part of the file.
- An optional **description** can follow after a blank line.

Use a **single blank line** (`\n\n`) to separate the QR payload from the
description. This keeps multi-line QR formats (vCard, calendar events) intact.

**Example — single-line QR payload with description:**

```
WIFI:T:WPA;S:MyNetwork;P:MyPassword;;

Hotel Wi-Fi
```

**Example — multi-line vCard QR payload (no description):**

```
BEGIN:VCARD
VERSION:3.0
N:Andreacchio;Marco;;;
FN:Marco Andreacchio
END:VCARD
```

**Example — multi-line vCard QR payload with description:**

```
BEGIN:VCARD
VERSION:3.0
N:Andreacchio;Marco;;;
FN:Marco Andreacchio
END:VCARD

Work contact
```

### Supported QR formats

| Format | Prefix / marker | Parsed fields shown on screen |
|--------|----------------|-------------------------------|
| Wi-Fi | `WIFI:...` | SSID, Password, Security, Hidden |
| vCard | `BEGIN:VCARD` | Name, Full Name, Organization, Phone, Email, URL, Address |
| MeCard | `MECARD:` | Name, Phone, Email, Note, URL, Address |
| Geo | `geo:` | Latitude, Longitude, Altitude |
| Email | `mailto:` | Email, Subject, Body |
| Phone | `tel:` | Phone number |
| SMS | `sms:` / `smsto:` | Number, Message |
| OTP / 2FA | `otpauth://` | Account, Issuer, Type |
| Calendar event | `BEGIN:VEVENT` | Summary, Start, End, Location, Description |
| URL | `http://` or `https://` | URL, Domain |
| Plain text | anything else | raw text |

### Barcodes (`.barcode` / `.bc` files)

A barcode file is plain text.

- **First line:** numeric digits only (Code-128C, max 40 characters).
- The digit count **must be even** (Code-128C encodes pairs of digits).
- **Remaining lines:** optional free-text description.

**Example:**

```
123456789012

TESSERA CONAD
```

If the barcode text contains non-digit characters or an odd number of digits,
the app shows an error instead of rendering bars.

---

## How to use

1. Put your files in this `/cards/` folder on the SD card.
2. Open **Quick Cards** from the Home screen or the Apps hub.
3. Browse the list with **Up / Down**.
4. Press **Confirm** to open a card.
5. Press **Confirm** again to toggle fullscreen mode.
6. Press **Back** to exit fullscreen or return to the list.
7. Press **Left** on an image card to delete its cached BMP (not the original file).
   Press **Left** on a QR or barcode card to delete the file.

---

## Tips

- Long filenames are truncated in the list view.
- Image cards are scaled to fit the screen while preserving aspect ratio.
- QR codes are rendered in the upper portion of the screen, with parsed fields
  shown below in clean centered text.
- Barcodes are rendered at one-third of the screen height, with human-readable
  digits printed below the bars.
- The folder is created automatically on first launch if it does not exist.
