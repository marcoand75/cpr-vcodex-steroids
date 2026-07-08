#!/usr/bin/env python3
"""Convert a PNG/SVG image into a C bitmap header for the CPR-vCodex boot/sleep
logo, suitable for GfxRenderer::drawIcon().

Why the transform is the way it is
----------------------------------
`GfxRenderer::drawIcon(bitmap, x, y, width, height)` forwards the call to the
display as `drawImageTransparent(bitmap, ..., w = height, h = width)`. The
EInkDisplay driver computes the per-row stride as `imageWidthBytes = w / 8`
(integer division). Therefore the on-screen *height* (which becomes `w`) MUST be
a multiple of 8, otherwise the last few columns of every row are dropped.

The bitmap is stored "sideways" relative to the screen: a logo that should
appear on screen as `width` x `height` is stored as `height` x `width` (after a
90 deg CCW rotation). This script does exactly that, and automatically rounds
the requested on-screen height up to the next multiple of 8.

Usage
-----
    python scripts/convert_logo.py input.png [output.h]
        [--width 175] [--height 46] [--rotate 0]
        [--name Logo] [--threshold 128] [--invert]
        [--bg FFFFFF] [--no-preview]

    --rotate N   Pre-rotate the SOURCE by N degrees (0/90/180/270, CCW) so it is
                 oriented the way you want to SEE it on screen. The mandatory
                 90 deg CCW display transform is always applied afterwards.
    --invert     Swap black/white (use for white-on-transparent artwork).
    --bg HEX     Background to flatten transparency onto (default FFFFFF).
    --no-preview Skip the ASCII on-screen preview.
"""

import argparse
import io
import os
import sys

# GfxRenderer::drawIcon forwards (width, height) as (w = height, h = width) to the
# display, which strides rows with integer w/8. So the on-screen height must be a
# multiple of 8.
DISPLAY_ROW_MULTIPLE = 8


def _load_image(path, width, height, rotate, bg_hex, *, from_svg=False):
    """Load an image, pre-rotate the source, and resize it to the on-screen slot
    (width x height). The image is returned in the orientation the user wants to
    see on screen."""
    from PIL import Image

    if from_svg:
        import cairosvg
        with open(path, "rb") as f:
            png_bytes = cairosvg.svg2png(
                bytestring=f.read(),
                output_width=width,
                output_height=height,
            )
        img = Image.open(io.BytesIO(png_bytes))
    else:
        img = Image.open(path)

    img = img.convert("RGBA")

    if rotate:
        # PIL rotates counter-clockwise for positive angles.
        img = img.rotate(rotate, expand=True)

    # Resize the (possibly rotated) source to fill the exact on-screen slot.
    img = img.resize((width, height), Image.LANCZOS)
    return img


def _flatten(img, bg_hex):
    """Composite the RGBA image onto an opaque background of the given color."""
    from PIL import Image

    bg = Image.new("RGBA", img.size, tuple(int(bg_hex[i:i + 2], 16) for i in (0, 2, 4)))
    bg.paste(img, mask=img.split()[3])
    return bg.convert("RGB")


def image_to_c_array(img, threshold, invert):
    """Pack a grayscale image as 1bpp, MSB-first, row-major.

    white (>= threshold) -> bit 1 (transparent on the e-ink display)
    black (<  threshold) -> bit 0 (drawn pixel)
    """
    img = img.convert("L")
    width, height = img.size
    pixels = list(img.getdata())
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    v = pixels[y * width + x + b]
                    bit = 1 if v >= threshold else 0
                    if invert:
                        bit = 1 - bit
                    byte |= (bit << (7 - b))
            packed.append(byte)
    return packed


def to_on_screen_preview(packed, stored_w, stored_h, threshold):
    """The stored bitmap is the on-screen image rotated 90 deg CCW. Rotate it
    90 deg CW to recover what the user will actually see, for an ASCII preview."""
    from PIL import Image

    stored = Image.new("L", (stored_w, stored_h), 255)
    px = stored.load()
    row_bytes = stored_w // 8
    for y in range(stored_h):
        for x in range(stored_w):
            byte = packed[y * row_bytes + x // 8]
            bit = (byte >> (7 - (x % 8))) & 1
            # white (bit 1) -> 255, black (bit 0) -> 0, matching the packed data.
            px[x, y] = 255 if bit else 0
    return stored.rotate(-90, expand=True)


def render_preview(on_screen, threshold, max_cols=88, max_rows=24):
    w, h = on_screen.size
    preview = on_screen.resize((min(max_cols, w), min(max_rows, h)))
    px = preview.load()
    lines = []
    for y in range(preview.size[1]):
        lines.append("".join("#" if px[x, y] < threshold else " " for x in range(preview.size[0])))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Convert a PNG/SVG into a C bitmap header for the boot/sleep logo."
    )
    parser.add_argument("input", help="Source image (.png or .svg)")
    parser.add_argument(
        "output",
        nargs="?",
        default=os.path.join("src", "images", "Logo.h"),
        help="Output header (default: src/images/Logo.h)",
    )
    parser.add_argument("--width", type=int, default=175, help="On-screen width in px (default 175)")
    parser.add_argument("--height", type=int, default=46, help="On-screen height in px (default 46)")
    parser.add_argument(
        "--rotate",
        type=int,
        choices=[0, 90, 180, 270],
        default=0,
        help="Pre-rotate the source CCW so it is oriented as seen on screen (default 0)",
    )
    parser.add_argument("--name", default="Logo", help="C array name (default Logo)")
    parser.add_argument("--threshold", type=int, default=128, help="Grayscale threshold (default 128)")
    parser.add_argument("--invert", action="store_true", help="Swap black/white")
    parser.add_argument("--bg", default="FFFFFF", help="Background hex for transparency (default FFFFFF)")
    parser.add_argument("--no-preview", action="store_true", help="Skip ASCII on-screen preview")
    args = parser.parse_args()

    if len(args.bg) != 6 or any(c not in "0123456789abcdefABCDEF" for c in args.bg):
        parser.error("--bg must be a 6-digit hex color, e.g. FFFFFF")

    on_screen_w = args.width
    # Height fed to drawIcon must be a multiple of 8 (display strides w/8).
    on_screen_h = args.height
    stored_w = ((on_screen_h + DISPLAY_ROW_MULTIPLE - 1) // DISPLAY_ROW_MULTIPLE) * DISPLAY_ROW_MULTIPLE
    padded = stored_w != on_screen_h
    stored_h = on_screen_w

    ext = os.path.splitext(args.input)[1].lower()
    from_svg = ext == ".svg"

    # Load + pre-rotate + resize to the on-screen artwork slot (true art size).
    img = _load_image(args.input, on_screen_w, on_screen_h, args.rotate, args.bg, from_svg=from_svg)
    img = _flatten(img, args.bg)

    # Place the artwork centered on a white on-screen canvas whose HEIGHT is the
    # multiple-of-8 value the display requires. This preserves the true art
    # height (e.g. 46) with 1px top/bottom padding inside the 48px slot.
    from PIL import Image

    canvas = Image.new("RGB", (on_screen_w, stored_w), (255, 255, 255))
    off_y = (stored_w - on_screen_h) // 2
    canvas.paste(img, (0, off_y))
    img = canvas

    # Mandatory display transform: rotate 90 deg CCW so the stored bitmap maps
    # correctly through drawIcon(). Stored size becomes (stored_w x stored_h).
    stored = img.rotate(90, expand=True).convert("L")
    sw, sh = stored.size
    assert (sw, sh) == (stored_w, stored_h), f"internal size mismatch {sw}x{sh} vs {stored_w}x{stored_h}"

    packed = image_to_c_array(stored, args.threshold, args.invert)
    assert len(packed) == sw // 8 * sh, f"packing length {len(packed)} != {sw // 8 * sh}"

    # Emit the header, matching the existing src/images/Logo.h style.
    c = "#pragma once\n#include <cstdint>\n\n"
    c += (
        f"// Logo bitmap: stored {sw}x{sh} "
        f"(on-screen ~{on_screen_w}x{on_screen_h}), "
        f"pre-rotated 90 CCW for drawIcon()\n"
    )
    c += f"static const uint8_t {args.name}[] = {{\n    "
    for i, v in enumerate(packed):
        c += f"0x{v:02X}, "
        if (i + 1) % 16 == 0:
            c += "\n    "
    c = c.rstrip(", \n") + "\n};\n"

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "w") as f:
        f.write(c)

    print(f"Wrote {args.output}")
    print(f"  on-screen : ~{on_screen_w}x{on_screen_h}")
    print(f"  stored    : {sw}x{sh} ({sw // 8} bytes/row, {len(packed)} bytes total)")
    if padded:
        print(
            f"  note      : height rounded {on_screen_h} -> {stored_w} "
            f"(multiple of {DISPLAY_ROW_MULTIPLE}); artwork is {on_screen_h}px tall, "
            f"centered with 1px top/bottom padding."
        )
    print(f"  Update BOOT_LOGO_WIDTH/HEIGHT (and sleep screen) to {on_screen_w}/{stored_w}.")

    if not args.no_preview:
        on_screen = to_on_screen_preview(packed, sw, sh, args.threshold)
        print("\nOn-screen preview (should read upright, landscape):")
        print(render_preview(on_screen, args.threshold))


if __name__ == "__main__":
    main()
