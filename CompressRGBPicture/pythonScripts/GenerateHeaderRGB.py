"""
generateHeaderRGB.py

Usage:
    python generateHeaderRGB.py image.bmp|jpg|png
    
Generates the Image.bmp.h header for the purpose of DSP use
Generates a ImageRGB.bmp grayscaled image of our original image
Saves the ImageRGV.raw rgb pixels for reference as the original rgb pixels.
"""

from PIL import Image
import sys
import os

def process_image_to_header(image_path, output_header, array_name):

   
    base = os.path.splitext(os.path.basename(image_path))[0]
    img = Image.open(image_path)
    width, height = img.size
    
    # Conversion to rgb, save the rgb.bmp and rgb.raw bytes
    img = img.convert("RGB")
    img.save(f'{base}RGB.bmp')
    rgb_bytes = img.tobytes()
    
    with open(f'{base}RGB.raw', 'wb') as f:
        f.write(rgb_bytes)

    # Generate the .h file
    with open(output_header, "w") as f:
        f.write(f"#ifndef {array_name.upper()}_H\n")
        f.write(f"#define {array_name.upper()}_H\n\n")
        
        f.write(f"#define WIDTH {width}\n")
        f.write(f"#define HEIGHT {height}\n\n")
        # Rch[] Gch[] Bch[] array declaration
        f.write('unsigned char Rch[%d] __attribute__((section("seg_sdram1")));\n\n' % (width * height))
        f.write('unsigned char Gch[%d] __attribute__((section("seg_sdram1")));\n\n' % (width * height))
        f.write('unsigned char Bch[%d] __attribute__((section("seg_sdram1")));\n\n' % (width * height))
        # rgb[] array declaration and initialization
        f.write(f"const unsigned char {array_name}[{width * height * 3}] __attribute__((section(\"seg_sdram1\"))) = {{\n")
        
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y)) 
                f.write(f"    {r}, {g}, {b},\n")
        
        f.write("};\n\n")
        f.write("#endif\n")

# -------- MAIN --------
if len(sys.argv) != 2:
    print("Usage: python generateHeaderRGB.py image.bmp|jpg|png")
    sys.exit(1)

image_name = sys.argv[1]
# output_header name basename + ".bmp.h"
base = os.path.splitext(os.path.basename(image_name))[0]
output_header = f"{base}.bmp.h"
array_name = "rgb"

process_image_to_header(image_name, output_header, array_name)
