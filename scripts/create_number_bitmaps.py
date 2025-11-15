#!/usr/bin/env python3
"""
Create number bitmap PNGs from font
- Renders numbers 0-9 with specified font and size
- Binarizes the bitmap
- Measures max height from black area (common height for all numbers)
- Splits each number as single PNG with same height but variable width
"""

from PIL import Image, ImageDraw, ImageFont
import os
import sys
import subprocess
import platform

def find_font_macos(font_name, style=None):
    """
    Find font on macOS using mdfind

    Args:
        font_name: Font name to search for
        style: Font style (e.g., "Extra Bold")

    Returns:
        Font file path or None
    """
    try:
        # Search for font files
        search_term = font_name
        if style:
            search_term = f"{font_name} {style}"

        # Use mdfind to search for font files
        result = subprocess.run(
            ['mdfind', f'kMDItemDisplayName == "*{search_term}*" && kMDItemContentType == "com.apple.font"'],
            capture_output=True, text=True, timeout=5
        )

        if result.returncode == 0 and result.stdout:
            paths = [p.strip() for p in result.stdout.split('\n') if p.strip()]
            # Prefer .ttf or .otf files
            for path in paths:
                if path.endswith(('.ttf', '.otf', '.ttc')):
                    return path

        # Also try searching in common font directories
        font_dirs = [
            "/System/Library/Fonts",
            "/Library/Fonts",
            os.path.expanduser("~/Library/Fonts"),
        ]

        # Normalize search terms (remove spaces, hyphens, case insensitive)
        def normalize_text(text):
            return text.lower().replace(' ', '').replace('-', '').replace('_', '')

        name_normalized = normalize_text(font_name)
        style_normalized = normalize_text(style) if style else ""

        for font_dir in font_dirs:
            if not os.path.exists(font_dir):
                continue

            for root, dirs, files in os.walk(font_dir):
                for file in files:
                    if file.endswith(('.ttf', '.otf', '.ttc')):
                        file_normalized = normalize_text(file)

                        # Check if font name matches (normalized)
                        if name_normalized in file_normalized:
                            # If style is specified, check if it matches (normalized)
                            if style and style_normalized:
                                # Check for style keywords (extrabold, bold, etc.)
                                style_keywords = style_normalized.split()
                                if not any(keyword in file_normalized for keyword in style_keywords):
                                    continue
                            return os.path.join(root, file)
    except Exception as e:
        pass

    return None

def pt_to_px(pt, dpi=96):
    """
    Convert points to pixels

    Args:
        pt: Font size in points
        dpi: Dots per inch (default: 96 for standard screen)

    Returns:
        Font size in pixels
    """
    # 1 point = 1/72 inch
    # pixels = (points / 72) * dpi
    return int((pt / 72.0) * dpi)

def find_font(font_name, size_px, style=None, font_path=None):
    """
    Try to find and load the specified font

    Args:
        font_name: Font name to search for
        size_px: Font size in pixels
        style: Font style (e.g., "Extra Bold")
        font_path: Direct path to font file (if provided, use this)

    Returns:
        Font object

    Raises:
        FileNotFoundError: If font is not found
        OSError: If font file cannot be loaded
    """

    # If font path is provided, use it directly
    if font_path:
        if not os.path.exists(font_path):
            raise FileNotFoundError(f"Font file not found: {font_path}")
        try:
            return ImageFont.truetype(font_path, size_px)
        except Exception as e:
            raise OSError(f"Error loading font from {font_path}: {e}")

    # Try to find font on macOS
    if platform.system() == "Darwin":
        found_path = find_font_macos(font_name, style)
        if found_path:
            try:
                print(f"✓ Found font: {found_path}")
                return ImageFont.truetype(found_path, size_px)
            except Exception as e:
                raise OSError(f"Error loading font from {found_path}: {e}")

    # Try common font paths
    font_paths = [
        # macOS
        f"/System/Library/Fonts/Supplemental/{font_name}.ttf",
        f"/Library/Fonts/{font_name}.ttf",
        f"~/Library/Fonts/{font_name}.ttf",
        f"/System/Library/Fonts/Supplemental/{font_name}.otf",
        f"/Library/Fonts/{font_name}.otf",
        f"~/Library/Fonts/{font_name}.otf",
    ]

    for path in font_paths:
        expanded_path = os.path.expanduser(path)
        if os.path.exists(expanded_path):
            try:
                return ImageFont.truetype(expanded_path, size_px)
            except:
                continue

    # Font not found - raise error
    style_str = f" {style}" if style else ""
    raise FileNotFoundError(
        f"Font '{font_name}{style_str}' not found. "
        f"Please install the font or specify --font-path with the full path to the font file."
    )

def get_text_bbox(draw, text, font):
    """Get bounding box of text"""
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox

def render_number(number, font, padding=20):
    """
    Render a single number on a white canvas

    Args:
        number: Number to render (0-9 or ':')
        font: Font object
        padding: Padding around the text

    Returns:
        PIL Image object (grayscale)
    """
    # Create a temporary image to measure text size
    temp_img = Image.new('RGB', (1000, 1000), 'white')
    temp_draw = ImageDraw.Draw(temp_img)

    # Get text bounding box
    bbox = get_text_bbox(temp_draw, str(number), font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]

    # Create image with padding
    img_width = text_width + padding * 2
    img_height = text_height + padding * 2

    img = Image.new('L', (img_width, img_height), 255)  # White background
    draw = ImageDraw.Draw(img)

    # Draw text in black (centered)
    x = padding - bbox[0]
    y = padding - bbox[1]
    draw.text((x, y), str(number), font=font, fill=0)  # 0 = black

    return img

def binarize_image(img, threshold=128):
    """
    Convert grayscale image to binary (black and white)

    Args:
        img: PIL Image (grayscale)
        threshold: Threshold value (0-255)

    Returns:
        PIL Image (mode '1')
    """
    return img.point(lambda x: 0 if x < threshold else 255, mode='1')

def get_black_bounds(img):
    """
    Get bounding box of black pixels in binary image

    Args:
        img: PIL Image (mode '1')

    Returns:
        (left, top, right, bottom) or None if no black pixels
    """
    pixels = img.load()
    width, height = img.size

    left = width
    top = height
    right = 0
    bottom = 0

    found_black = False

    for y in range(height):
        for x in range(width):
            if pixels[x, y] == 0:  # Black pixel
                found_black = True
                left = min(left, x)
                top = min(top, y)
                right = max(right, x)
                bottom = max(bottom, y)

    if not found_black:
        return None

    return (left, top, right + 1, bottom + 1)

def crop_to_black_area(img, bounds):
    """
    Crop image to black area bounds

    Args:
        img: PIL Image
        bounds: (left, top, right, bottom)

    Returns:
        Cropped PIL Image
    """
    return img.crop(bounds)

def main():
    import argparse

    parser = argparse.ArgumentParser(description='Create number bitmap PNGs from font')
    parser.add_argument('--font-name', default='Balco Bhai 2', help='Font name')
    parser.add_argument('--font-style', default='Extra Bold', help='Font style')
    parser.add_argument('--font-size', type=float, default=51.8, help='Font size in points')
    parser.add_argument('--font-size-px', type=int, default=None, help='Font size in pixels (overrides --font-size and --dpi)')
    parser.add_argument('--font-path', default=None, help='Direct path to font file')
    parser.add_argument('--dpi', type=int, default=96, help='DPI for pt to px conversion (default: 96)')
    parser.add_argument('--output-dir', default='assets/Number S', help='Output directory')

    args = parser.parse_args()

    font_name = args.font_name
    font_style = args.font_style
    font_size = args.font_size
    font_size_px = args.font_size_px
    font_path = args.font_path
    dpi = args.dpi

    # Resolve output directory path relative to project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    if os.path.isabs(args.output_dir):
        output_dir = args.output_dir
    else:
        output_dir = os.path.join(project_root, args.output_dir)

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Debug output (set to False to disable)
    debug_enabled = False
    if debug_enabled:
        debug_dir = os.path.join(output_dir, "debug")
        os.makedirs(debug_dir, exist_ok=True)
    else:
        debug_dir = None

    # Convert pt to px if px not directly specified
    if font_size_px is None:
        font_size_px = pt_to_px(font_size, dpi)
    else:
        # If px is directly specified, use it and update font_size for display
        font_size = font_size_px  # For display purposes

    # Try to load font
    if font_path:
        print(f"Loading font from: {font_path}")
    else:
        print(f"Searching for font: {font_name} {font_style}")

    try:
        font = find_font(font_name, font_size_px, style=font_style, font_path=font_path)
    except FileNotFoundError as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except OSError as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Using font: {font}")
    if args.font_size_px is not None:
        print(f"Font size: {font_size_px}px (direct pixel specification)")
    else:
        print(f"Font size: {font_size}pt ({font_size_px}px at {dpi} DPI)")
    print(f"Output directory: {output_dir}\n")

    # Render all numbers at once with spacing
    numbers = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.']
    char_spacing = 10  # Space between characters in pixels
    left_margin = 0  # Left margin before first character in pixels (set to 0 to avoid black border)

    # Create text with spacing
    text_to_render = ' '.join(numbers)  # Add space between characters

    print(f"Rendering all characters together with spacing: '{text_to_render}'...")

    # Create a large canvas to render all characters
    padding = 30
    temp_img = Image.new('RGB', (2000, 1000), 'white')
    temp_draw = ImageDraw.Draw(temp_img)

    # Get bounding box for all text
    all_text_bbox = get_text_bbox(temp_draw, text_to_render, font=font)
    text_width = all_text_bbox[2] - all_text_bbox[0]
    text_height = all_text_bbox[3] - all_text_bbox[1]

    # Create image with padding
    img_width = text_width + padding * 2
    img_height = text_height + padding * 2

    img = Image.new('L', (img_width, img_height), 255)  # White background
    draw = ImageDraw.Draw(img)

    # Draw all text at once with spacing
    # Left margin will be handled separately when cropping
    x = padding - all_text_bbox[0]
    y = padding - all_text_bbox[1]
    draw.text((x, y), text_to_render, font=font, fill=0)  # 0 = black

    # Save original image for debugging
    if debug_dir:
        img.save(os.path.join(debug_dir, "01_original.png"), 'PNG')
        print(f"  Debug: Saved original image to {debug_dir}/01_original.png")

    # Binarize
    img_binary = binarize_image(img)

    # Save binarized image for debugging
    if debug_dir:
        img_binary.save(os.path.join(debug_dir, "02_binarized.png"), 'PNG')
        print(f"  Debug: Saved binarized image to {debug_dir}/02_binarized.png")

    # Find black area bounds for all characters together
    all_bounds = get_black_bounds(img_binary)
    if not all_bounds:
        print("Error: No black pixels found in rendered text")
        sys.exit(1)

    all_left, all_top, all_right, all_bottom = all_bounds
    common_height = all_bottom - all_top

    print(f"✓ Common black area: {all_right - all_left}x{common_height}px")
    print(f"  Top: {all_top}px, Bottom: {all_bottom}px")
    print(f"  Left: {all_left}px, Right: {all_right}px")

    # Crop to common height (top and bottom) only, keep full width to preserve side margins
    img_width, img_height = img_binary.size
    cropped_all = img_binary.crop((0, all_top, img_width, all_bottom))

    # Save cropped image for debugging
    if debug_dir:
        cropped_all.save(os.path.join(debug_dir, "03_cropped_height.png"), 'PNG')
        print(f"  Debug: Saved cropped image to {debug_dir}/03_cropped_height.png")

    # Now split into individual characters
    print("\nSplitting into individual characters...")

    # Calculate positions of each character by measuring each one individually
    # Account for spacing between characters and left margin
    char_positions = []

    # Text starts at: padding - all_text_bbox[0]
    text_start_x = padding - all_text_bbox[0]

    # Start with left margin offset
    current_x = left_margin

    for i, num in enumerate(numbers):
        # Get bounding box for this character
        char_bbox = get_text_bbox(temp_draw, str(num), font=font)
        char_width = char_bbox[2] - char_bbox[0]

        # Calculate position in the cropped image
        # Position in original image: text_start_x + current_x
        # Position in cropped image: (text_start_x + current_x) - 0 (since we kept full width)
        # But we need to account for the fact that cropped image starts at x=0, not at all_left
        char_start_x = text_start_x + current_x
        char_end_x = text_start_x + current_x + char_width

        char_positions.append((num, char_start_x, char_end_x))

        # Move to next character position (add spacing)
        # Get space width
        space_bbox = get_text_bbox(temp_draw, ' ', font=font)
        space_width = space_bbox[2] - space_bbox[0] if space_bbox[2] > space_bbox[0] else char_spacing
        current_x += char_width + space_width

    # Split and crop each character
    for num, start_x, end_x in char_positions:
        # Extract character region
        char_img = cropped_all.crop((start_x, 0, end_x, common_height))

        # Save character region before cropping left/right for debugging
        if debug_dir:
            debug_char_path = os.path.join(debug_dir, f"04_{num}_before_crop.png")
            char_img.save(debug_char_path, 'PNG')

        # Get black bounds for this character to crop left/right
        char_bounds = get_black_bounds(char_img)
        if char_bounds:
            left, top, right, bottom = char_bounds
            # Crop left and right only (height is already correct)
            final_cropped = char_img.crop((left, 0, right, common_height))
        else:
            # If no black pixels found, use the whole width
            final_cropped = char_img

        # Save final PNG
        if num == '.':
            output_path = os.path.join(output_dir, "period.png")
        else:
            output_path = os.path.join(output_dir, f"{num}.png")
        final_cropped.save(output_path, 'PNG')

        # Save final cropped image for debugging
        if debug_dir:
            debug_final_path = os.path.join(debug_dir, f"05_{num}_final.png")
            final_cropped.save(debug_final_path, 'PNG')

        final_width, final_height = final_cropped.size
        print(f"  '{num}': {final_width}x{final_height}px -> {output_path}")

    print(f"\n✓ All numbers saved to {output_dir}/")
    print(f"✓ Common height: {common_height}px")
    print(f"\nNext step: Run convert_numbers.py to generate Number_bitmap.h")

if __name__ == "__main__":
    main()
