with open('src/images/Logo-steroids.png', 'rb') as f:
    import io
    from PIL import Image
    img = Image.open(io.BytesIO(f.read())).convert("RGBA")

    # Create white version: all non-transparent pixels become white
    white_pixels = []
    alpha = img.split()[3]
    for y in range(img.height):
        for x in range(img.width):
            a = alpha.getpixel((x, y))
            if a > 128:  # Non-transparent -> white
                white_pixels.append((255, 255, 255, 255))
            else:  # Transparent
                white_pixels.append((255, 255, 255, 0))

    white_img = Image.new("RGBA", img.size, (255, 255, 255, 0))
    white_img.putdata(white_pixels)

    output = io.BytesIO()
    white_img.save(output, format='PNG')
    data = output.getvalue()

    # Write white version header (PROGMEM)
    with open('src/network/html/LogoPng.generated.h', 'w') as out:
        out.write('#pragma once\n#include <cstddef>\n#include <pgmspace.h>\n\n')
        out.write('// CPR-vCodex Steroids logo PNG white version (served at /logo.png) - GENERATED\n')
        out.write('static const uint8_t LogoPng[] PROGMEM = {\n    ')
        for i, b in enumerate(data):
            out.write(f'0x{b:02x}, ')
            if (i + 1) % 16 == 0:
                out.write('\n    ')
        out.write('\n};\n')
        out.write(f'static const size_t LogoPngSize = {len(data)};\n')

    print(f'White logo: {len(data)} bytes - LogoPng.generated.h updated')
