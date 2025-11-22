#!/usr/bin/env python3
"""
Convert all PNG number images to EPD bitmap arrays for Arduino
Each number image is converted to its original size (no resizing)
"""

from PIL import Image
import os
import sys

def convert_image_to_bitmap(image_path, keep_original_size=True):
    """
    Convert PNG image to EPD bitmap array

    Args:
        image_path: Path to input PNG image
        keep_original_size: If True, use original image size; if False, resize to 800x272

    Returns:
        bitmap_data, width, height, width_byte
    """
    # Open and convert image
    img = Image.open(image_path)

    # Get original dimensions
    original_width, original_height = img.size

    # Convert to grayscale
    if img.mode != 'L':
        img = img.convert('L')

    # Use original size or resize
    if keep_original_size:
        output_width = original_width
        output_height = original_height
    else:
        output_width = 800
        output_height = 272
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
            bitmap_data.append(byte_val)

    return bitmap_data, output_width, output_height, width_byte

def generate_c_array(bitmap_data, array_name="ImageData"):
    """Generate C array string"""
    lines = [f"const uint8_t {array_name}[] = {{"]
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
    lines.append(f"#define {array_name}_WIDTH {bitmap_data[0] if len(bitmap_data) > 0 else 0}")
    lines.append(f"#define {array_name}_HEIGHT {bitmap_data[1] if len(bitmap_data) > 1 else 0}")

    return "\n".join(lines)

def main():
    import argparse

    parser = argparse.ArgumentParser(description='Convert PNG number images to EPD bitmap arrays')
    parser.add_argument('--dir', default='assets/Number L', help='Directory containing PNG files (default: assets/Number L)')
    args = parser.parse_args()

    # Resolve path relative to project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    if os.path.isabs(args.dir):
        number_dir = args.dir
    else:
        number_dir = os.path.join(project_root, args.dir)

    if not os.path.exists(number_dir):
        print(f"Error: Directory not found: {number_dir}")
        sys.exit(1)

    # Get all PNG files
    png_files = sorted([f for f in os.listdir(number_dir) if f.endswith('.png')])

    if not png_files:
        print(f"Error: No PNG files found in {number_dir}")
        sys.exit(1)

    print(f"Found {len(png_files)} PNG files in {number_dir}")

    # Generate header file
    header_content = []
    header_content.append("#ifndef NUMBER_BITMAP_H")
    header_content.append("#define NUMBER_BITMAP_H")
    header_content.append("")
    header_content.append("#include <Arduino.h>")
    header_content.append("")

    # Store image info for easy access
    image_info = []

    for png_file in png_files:
        image_path = os.path.join(number_dir, png_file)
        print(f"\nConverting {png_file}...")

        try:
            bitmap_data, width, height, width_byte = convert_image_to_bitmap(image_path, keep_original_size=True)

            # Generate array name from filename (e.g., "0.png" -> "Number0")
            base_name = os.path.splitext(png_file)[0]
            if base_name.isdigit():
                array_name = f"Number{base_name}"
            elif base_name == "colon":
                array_name = "NumberColon"
            elif base_name == "period":
                array_name = "NumberPeriod"
            else:
                array_name = f"Number{base_name.capitalize()}"

            # Generate C array
            c_array = generate_c_array(bitmap_data, array_name)

            # Fix the width/height defines (they were incorrectly using bitmap_data indices)
            lines = c_array.split('\n')
            # Remove incorrect width/height lines
            lines = [l for l in lines if not l.startswith(f"#define {array_name}_WIDTH") and
                     not l.startswith(f"#define {array_name}_HEIGHT")]
            # Add correct width/height defines
            lines.append(f"#define {array_name}_WIDTH {width}")
            lines.append(f"#define {array_name}_HEIGHT {height}")

            header_content.extend(lines)
            header_content.append("")

            image_info.append({
                'name': array_name,
                'width': width,
                'height': height,
                'file': png_file
            })

            print(f"  ✓ Converted: {width}x{height} pixels, {len(bitmap_data)} bytes")

        except Exception as e:
            print(f"  ✗ Error converting {png_file}: {e}")
            import traceback
            traceback.print_exc()

    header_content.append("#endif")

    # Write header file (to firmware/bitmaps directory)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_file = os.path.join(project_root, "firmware", "bitmaps", "Number_bitmap.h")
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    with open(output_file, 'w') as f:
        f.write("\n".join(header_content))

    print(f"\n✓ Conversion complete!")
    print(f"✓ Output file: {output_file}")
    print(f"\nTo use in Arduino sketch:")
    print(f"  #include \"{output_file}\"")
    print(f"\nExample usage:")
    for info in image_info[:4]:  # Show first 4
        print(f"  EPD_ShowPicture(x, y, {info['name']}_WIDTH, {info['name']}_HEIGHT, {info['name']}, WHITE);")

if __name__ == "__main__":
    main()
