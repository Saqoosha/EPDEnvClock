#!/usr/bin/env python3
"""
Convert PNG image to EPD bitmap array for Arduino
EPD actual display: 792x272 pixels
EPD buffer size: 800x272 pixels (for address offset)
Format: 1 bit per pixel, 1 byte = 8 pixels horizontally

Note: The EPD display uses master/slave dual controllers (396px each).
There is a 4px gap at the center where the controllers meet.
EPD_Display() function requires 800x272 bitmap data.
EPD_ShowPicture() function requires 792x272 bitmap data.
"""

from PIL import Image
import sys
import os

def convert_image_to_bitmap(image_path, output_width=800, output_height=272):
    """
    Convert PNG image to EPD bitmap array

    Args:
        image_path: Path to input PNG image
        output_width: Target width (default 800 for EPD_Display, use 792 for EPD_ShowPicture)
        output_height: Target height (default 272)

    Returns:
        C array string and byte array

    Note:
        - Use 800x272 for EPD_Display() function (full screen with address offset)
        - Use 792x272 for EPD_ShowPicture() function (actual display area)
    """
    # Open and convert image
    img = Image.open(image_path)

    # Convert to grayscale
    if img.mode != 'L':
        img = img.convert('L')

    # Resize to target resolution
    img = img.resize((output_width, output_height), Image.Resampling.LANCZOS)

    # Convert to 1-bit (black and white)
    # Threshold at 128: pixels darker than 128 become black (0), lighter become white (1)
    img_bw = img.point(lambda x: 0 if x < 128 else 255, mode='1')

    # Calculate bytes per row (8 pixels per byte)
    width_byte = (output_width + 7) // 8

    # Convert to byte array
    pixels = img_bw.load()
    bitmap_data = []

    for y in range(output_height):
        for x_byte in range(width_byte):
            byte_val = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                if x < output_width:
                    # EPD_ShowPicture format: bit 1 = black, bit 0 = white
                    # PIL '1' mode: 0 = black, 1 = white
                    pixel = pixels[x, y]
                    if pixel == 0:  # Black pixel in source image
                        byte_val |= (0x80 >> bit)  # Set bit to 1 for black
                    # White pixel (pixel == 1) leaves bit as 0 (white)
            # No inversion needed: bit 1 = black, bit 0 = white matches EPD_ShowPicture
            bitmap_data.append(byte_val)

    return bitmap_data, width_byte, output_height

def generate_c_array(bitmap_data, array_name="ImageData"):
    """Generate C array string"""
    lines = [f"const uint8_t {array_name}[] PROGMEM = {{"]
    current_line = "  "

    for i, byte_val in enumerate(bitmap_data):
        current_line += f"0x{byte_val:02X}"

        # Add comma if not the last element
        if i < len(bitmap_data) - 1:
            current_line += ","

        # Every 16 bytes, start a new line
        if (i + 1) % 16 == 0:
            lines.append(current_line)
            current_line = "  "

    # Add remaining bytes if any
    if current_line.strip() != "":
        lines.append(current_line)

    lines.append(f"}}; // {len(bitmap_data)} bytes")
    lines.append(f"#define {array_name}_SIZE {len(bitmap_data)}")

    return "\n".join(lines)

def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_image.py <image_path> [output_width] [output_height]")
        print("Example: python convert_image.py /path/to/Desktop/Document.png 800 272")
        sys.exit(1)

    image_path = sys.argv[1]
    output_width = int(sys.argv[2]) if len(sys.argv) > 2 else 800
    output_height = int(sys.argv[3]) if len(sys.argv) > 3 else 272

    if not os.path.exists(image_path):
        print(f"Error: Image file not found: {image_path}")
        sys.exit(1)

    print(f"Converting {image_path} to {output_width}x{output_height} bitmap...")

    try:
        bitmap_data, width_byte, height = convert_image_to_bitmap(
            image_path, output_width, output_height
        )

        c_array = generate_c_array(bitmap_data, "DocumentImage")

        # Save to file
        output_file = os.path.splitext(image_path)[0] + "_bitmap.h"
        with open(output_file, 'w') as f:
            f.write("#ifndef DOCUMENT_IMAGE_H\n")
            f.write("#define DOCUMENT_IMAGE_H\n\n")
            f.write("#include <Arduino.h>\n\n")
            f.write(c_array)
            f.write("\n\n#endif\n")

        print(f"\nConversion complete!")
        print(f"Output file: {output_file}")
        print(f"Image size: {output_width}x{height} pixels")
        print(f"Data size: {len(bitmap_data)} bytes ({width_byte} bytes per row)")
        print(f"\nTo use in Arduino sketch:")
        print(f"  #include \"{os.path.basename(output_file)}\"")
        print(f"  EPD_ShowPicture(0, 0, {output_width}, {height}, DocumentImage, WHITE);")

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
