#!/usr/bin/env python3
"""
Convert icon PNG image to EPD bitmap array for Arduino
Format: 1 bit per pixel, 1 byte = 8 pixels horizontally
"""

from PIL import Image
import sys
import os

def convert_icon_to_bitmap(image_path):
    """
    Convert PNG icon image to EPD bitmap array

    Args:
        image_path: Path to input PNG image

    Returns:
        bitmap_data, width, height
    """
    # Open and convert image
    img = Image.open(image_path)

    # Get original size
    width, height = img.size

    # Convert to grayscale
    if img.mode != 'L':
        img = img.convert('L')

    # Convert to 1-bit (black and white)
    # Threshold at 128: pixels darker than 128 become black (0), lighter become white (1)
    img_bw = img.point(lambda x: 0 if x < 128 else 255, mode='1')

    # Calculate bytes per row (8 pixels per byte)
    width_byte = (width + 7) // 8

    # Convert to byte array
    pixels = img_bw.load()
    bitmap_data = []

    for y in range(height):
        for x_byte in range(width_byte):
            byte_val = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                if x < width:
                    # bit 1 = black, bit 0 = white
                    pixel = pixels[x, y]
                    if pixel == 0:  # Black pixel in source image
                        byte_val |= (0x80 >> bit)  # Set bit to 1 for black
                    # White pixel (pixel == 1) leaves bit as 0 (white)
            bitmap_data.append(byte_val)

    return bitmap_data, width, height

def generate_c_array(bitmap_data, array_name, width, height):
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
    lines.append(f"#define {array_name}_WIDTH {width}")
    lines.append(f"#define {array_name}_HEIGHT {height}")

    return "\n".join(lines)

def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_icon.py <icon_png_path> [output_header_path]")
        print("Example: python convert_icon.py assets/icon_temp.png")
        print("         python convert_icon.py assets/icon_temp.png EPDClock/bitmaps/Icon_temp_bitmap.h")
        sys.exit(1)

    image_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.exists(image_path):
        print(f"Error: Image file not found: {image_path}")
        sys.exit(1)

    print(f"Converting {image_path} to bitmap...")

    try:
        bitmap_data, width, height = convert_icon_to_bitmap(image_path)

        # Determine array name from filename
        basename = os.path.basename(image_path)
        if basename.startswith("icon_"):
            icon_name = basename[5:-4]  # Remove "icon_" prefix and ".png" suffix
            # Handle special case for CO2 (should be IconCO2, not IconCo2)
            if icon_name.lower() == "co2":
                array_name = "IconCO2"
            else:
                array_name = f"Icon{icon_name.capitalize()}"
        elif basename.startswith("unit_"):
            unit_name = basename[5:-4]  # Remove "unit_" prefix and ".png" suffix
            # Handle special cases
            if unit_name.lower() == "c":
                array_name = "UnitC"
            elif unit_name.lower() == "percent":
                array_name = "UnitPercent"
            elif unit_name.lower() == "ppm":
                array_name = "UnitPpm"
            else:
                array_name = f"Unit{unit_name.capitalize()}"
        else:
            array_name = "Icon"

        c_array = generate_c_array(bitmap_data, array_name, width, height)

        # Generate header guard name
        if output_path:
            guard_name = os.path.basename(output_path).upper().replace(".", "_").replace("/", "_")
        else:
            guard_name = f"{array_name}_BITMAP_H"

        # Generate full header file content
        header_content = f"""#ifndef {guard_name}
#define {guard_name}

#include <Arduino.h>

{c_array}

#endif
"""

        # Write to file or stdout
        if output_path:
            with open(output_path, 'w') as f:
                f.write(header_content)
            print(f"\nHeader file written to: {output_path}")
        else:
            print("\n" + header_content)

        print(f"Image size: {width}x{height} pixels")
        print(f"Data size: {len(bitmap_data)} bytes")

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
