from PIL import Image
import sys
import os

def process_image_to_header(image_path, output_header, array_name):
    # Open image
    img_gray = Image.open(image_path).convert('L')   # 'L' = 8-bit gray
    # Save gray with adequate name for visual comparison
    base = os.path.splitext(os.path.basename(image_path))[0]
    img_gray.save(f'{base}Gray.bmp')

    img = Image.open(image_path)
    width, height = img.size
    
    # convert to rgb
    img = img.convert("RGB")
    
    # Generate h file
    with open(output_header, "w") as f:
        f.write(f"#ifndef {array_name.upper()}_H\n")
        f.write(f"#define {array_name.upper()}_H\n\n")
        
        f.write(f"#define WIDTH {width}\n")
        f.write(f"#define HEIGHT {height}\n\n")
        
        f.write('unsigned char gray[%d] __attribute__((section("seg_sdram1")));\n\n' % (width * height))

        # RGB PIXELS
        f.write(f"const unsigned char {array_name}[{width * height * 3}] __attribute__((section(\"seg_sdram1\"))) = {{\n")
        
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y)) 
                f.write(f"    {r}, {g}, {b},\n")
        
        f.write("};\n\n")
        f.write("#endif\n")

# -------- MAIN --------
if len(sys.argv) != 2:
    print("Usage: python GenerateHeader.py image.bmp|jpg|png")
    sys.exit(1)

image_name = sys.argv[1]
# output_header derived
base = os.path.splitext(os.path.basename(image_name))[0]
output_header = f"{base}.bmp.h"
array_name = "rgb"

process_image_to_header(image_name, output_header, array_name)
