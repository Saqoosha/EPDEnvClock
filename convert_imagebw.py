#!/usr/bin/env python3
"""
Convert ImageBW byte array to PNG image
ImageBW format: MSB first, 1 byte = 8 pixels (horizontal)
Screen size: 800x272 pixels
"""

from PIL import Image
import os

def convert_imagebw_to_png(imagebw_data, output_filename=None):
    """
    Convert ImageBW byte array to PNG image

    Args:
        imagebw_data: bytes or bytearray of ImageBW data (27,200 bytes)
        output_filename: Output PNG filename (optional)

    Returns:
        Path to saved PNG file

    Note:
        - Rotation = 180 degrees (as defined in EPD.h)
        - Bit format: bit 0 = black, bit 1 = white (inverted from display logic)
        - Logical size: 800x272 (buffer size)
        - Physical size: 792x272 (actual display area with 4px gap at center)
        - Center gap: 4px gap at x=396-400 (between two controllers)
    """
    if len(imagebw_data) != 27200:
        raise ValueError(f"Invalid ImageBW data length: {len(imagebw_data)}, expected 27200")

    # Screen dimensions (logical buffer size)
    EPD_W = 800  # Logical width (includes 4px gap)
    EPD_H = 272  # Height

    # Actual display dimensions (physical size)
    DISPLAY_W = 792  # Actual display width (without gap)
    DISPLAY_H = 272  # Height

    # Calculate bytes per row
    width_byte = (EPD_W + 7) // 8  # Should be 100 bytes per row

    # Create a new image for the actual display size (792x272)
    # Mode '1' = 1-bit pixels, black and white
    img = Image.new('1', (DISPLAY_W, DISPLAY_H), 1)  # 1 = white (background)

    # Convert ImageBW data to pixels
    pixels = img.load()

    # Process ImageBW buffer (800x272) and map to display (792x272)
    # Buffer structure: [0-395: left 396px] [396-399: 4px gap] [400-799: right 400px]
    # Display: [0-791: 792px] (gap removed)
    #
    # Important: Skip pixels at x = 396, 397, 398, 399 (4px gap)
    # Then apply 180-degree rotation

    for y_src in range(EPD_H):
        for x_byte in range(width_byte):
            byte_val = imagebw_data[y_src * width_byte + x_byte]

            # Process 8 bits (MSB first)
            for bit in range(8):
                x_src = x_byte * 8 + bit

                # Check if we've reached the end of the row
                if x_src >= EPD_W:
                    break

                # CRITICAL: Skip the gap at center
                # EPD.cpp Paint_SetPixel adds 8px offset for x >= 396, suggesting 8px gap
                # But AGENTS.md says 4px gap. Let's try 8px first (396-403)
                if x_src >= 396 and x_src < 404:  # 8px gap
                    continue  # Skip these pixels completely

                # Map logical x to display x (accounting for gap)
                # Left side (0-395): use as is -> becomes 0-395
                # Right side (404-799): shift left by 8px -> becomes 396-791
                if x_src < 396:
                    x_display = x_src
                else:  # x_src >= 404
                    x_display = x_src - 8  # Remove gap offset (404->396, 405->397, etc.)

                # Apply 180-degree rotation
                # Rotation 180: X' = widthMemory - X - 1, Y' = heightMemory - Y - 1
                x_rotated = DISPLAY_W - 1 - x_display
                y_rotated = DISPLAY_H - 1 - y_src

                # Check bit (MSB first)
                # Paint_SetPixel(BLACK) sets bit to 0, Paint_SetPixel(WHITE) sets bit to 1
                # So in ImageBW buffer: bit 0 = black, bit 1 = white
                # In PIL '1' mode: 0 = black, 1 = white
                if byte_val & (0x80 >> bit):
                    # Bit is 1 in buffer = white
                    pixels[x_rotated, y_rotated] = 1  # White
                else:
                    # Bit is 0 in buffer = black
                    pixels[x_rotated, y_rotated] = 0  # Black

    # Save image
    if output_filename is None:
        from datetime import datetime
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_filename = f"imagebw_{timestamp}.png"

    # Ensure output directory exists
    output_dir = "output"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    output_path = os.path.join(output_dir, output_filename)
    img.save(output_path, 'PNG')

    print(f"Converted ImageBW to PNG: {output_path} ({DISPLAY_W}x{DISPLAY_H})")

    return output_path

if __name__ == '__main__':
    import sys

    if len(sys.argv) < 2:
        print("Usage: convert_imagebw.py <input_file> [output_file]")
        print("  input_file: Binary ImageBW file (27,200 bytes)")
        print("  output_file: Output PNG filename (optional)")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    # Read ImageBW data
    with open(input_file, 'rb') as f:
        imagebw_data = f.read()

    # Convert to PNG
    convert_imagebw_to_png(imagebw_data, output_file)
