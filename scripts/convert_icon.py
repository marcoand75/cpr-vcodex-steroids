import os
import io
import sys
import argparse

threshold = 128
USAGE = 'Usage: python scripts/convert_icon.py input.png|input.svg output_name width height [--name ArrayName]'

def svg_to_png_bytes(svg_path, width, height):
    import cairosvg

    with open(svg_path, 'rb') as f:
        svg_data = f.read()
    png_bytes = cairosvg.svg2png(bytestring=svg_data, output_width=width, output_height=height)
    return png_bytes

def load_image(path, width, height):
    from PIL import Image

    ext = os.path.splitext(path)[1].lower()
    if ext == '.svg':
        png_bytes = svg_to_png_bytes(path, width, height)
        img = Image.open(io.BytesIO(png_bytes))
    else:
        img = Image.open(path)
        img = img.convert('RGBA')
        img = img.resize((width, height), Image.LANCZOS)
    # Flatten alpha: paste on white background so anti-aliased edges become
    # gray instead of black-on-transparent (which thresholds to all-black).
    background = Image.new('RGBA', img.size, (255, 255, 255, 255))
    background.paste(img, mask=img.split()[3])
    img = background
    # Rotate 90 degrees counterclockwise
    img = img.rotate(90, expand=True)
    return img

def image_to_c_array(img, array_name):
    # Convert to grayscale, then threshold to get white=1, black=0
    # Convert to grayscale
    img = img.convert('L')
    width, height = img.size
    pixels = list(img.getdata())
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    v = pixels[y * width + x + b]
                    # 1 for white, 0 for black
                    bit = 1 if v >= threshold else 0
                    byte |= (bit << (7 - b))
            packed.append(byte)
    # Format as C array
    c = f'#pragma once\n#include <cstdint>\n\n'
    c += f'// size: {width}x{height}\n'
    c += f'static const uint8_t {array_name}[] = {{\n    '
    for i, v in enumerate(packed):
        c += f'0x{v:02X}, '
        if (i + 1) % 16 == 0:
            c += '\n    '
    c = c.rstrip(', \n') + '\n};\n'
    return c

def main():
    parser = argparse.ArgumentParser(description="Convert an image to a 1-bit C array header")
    parser.add_argument("input", help="Source image (.png or .svg)")
    parser.add_argument("output_name", help="Output header name (without .h)")
    parser.add_argument("width", type=int, help="Width in pixels")
    parser.add_argument("height", type=int, help="Height in pixels")
    parser.add_argument("--name", default=None, help="C array name (default: OutputName + 'Icon')")
    parser.add_argument("--threshold", type=int, default=128, help="Grayscale threshold (default 128)")
    args = parser.parse_args()

    array_name = args.name if args.name else args.output_name.capitalize() + 'Icon'
    img = load_image(args.input, args.width, args.height)
    c_array = image_to_c_array(img, array_name)

    # Always save to src/components/icons/[output_name].h relative to project root
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_dir = os.path.join(project_root, 'src', 'components', 'icons')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f'{args.output_name}.h')
    with open(output_path, 'w') as f:
        f.write(c_array)
    print(f'Wrote {output_path}')


if __name__ == "__main__":
    main()
