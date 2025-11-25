#include "font_renderer.h"

#include "EPD.h"
#include "bitmaps/Number_L_bitmap.h"
#include "bitmaps/Number_M_bitmap.h"
#include "logger.h"

namespace
{

// Bitmap lookup tables
const uint8_t *NumberLBitmaps[] = {
    NumberL0, NumberL1, NumberL2, NumberL3, NumberL4,
    NumberL5, NumberL6, NumberL7, NumberL8, NumberL9};

const uint16_t NumberLWidths[] = {
    NumberL0_WIDTH, NumberL1_WIDTH, NumberL2_WIDTH, NumberL3_WIDTH, NumberL4_WIDTH,
    NumberL5_WIDTH, NumberL6_WIDTH, NumberL7_WIDTH, NumberL8_WIDTH, NumberL9_WIDTH};

const uint8_t *NumberMBitmaps[] = {
    NumberM0, NumberM1, NumberM2, NumberM3, NumberM4,
    NumberM5, NumberM6, NumberM7, NumberM8, NumberM9};

const uint16_t NumberMWidths[] = {
    NumberM0_WIDTH, NumberM1_WIDTH, NumberM2_WIDTH, NumberM3_WIDTH, NumberM4_WIDTH,
    NumberM5_WIDTH, NumberM6_WIDTH, NumberM7_WIDTH, NumberM8_WIDTH, NumberM9_WIDTH};

// Generic digit drawing helper
void drawDigitGeneric(uint8_t digit, uint16_t x, uint16_t y,
                      const uint8_t **bitmaps, const uint16_t *widths, uint16_t height)
{
  if (digit > 9) return;
  drawBitmapCorrect(x, y, widths[digit], height, bitmaps[digit]);
}

} // namespace

// ============================================================
// Low-level bitmap drawing
// ============================================================

void drawBitmapCorrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *bitmap)
{
  const uint16_t widthByte = (width + 7) / 8;

  for (uint16_t row = 0; row < height; row++)
  {
    uint16_t pixel_x = x;

    for (uint16_t col_byte = 0; col_byte < widthByte; col_byte++)
    {
      uint8_t bitmap_byte = bitmap[row * widthByte + col_byte];

      for (uint8_t bit = 0; bit < 8; bit++)
      {
        if (pixel_x >= x + width)
        {
          break;
        }

        if (bitmap_byte & (0x80 >> bit))
        {
          Paint_SetPixel(pixel_x, y + row, BLACK);
        }
        else
        {
          Paint_SetPixel(pixel_x, y + row, WHITE);
        }

        pixel_x++;
      }
    }
  }
}

// ============================================================
// Digit drawing functions
// ============================================================

void drawDigitL(uint8_t digit, uint16_t x, uint16_t y)
{
  drawDigitGeneric(digit, x, y, NumberLBitmaps, NumberLWidths, NumberL0_HEIGHT);
}

void drawDigitM(uint8_t digit, uint16_t x, uint16_t y)
{
  drawDigitGeneric(digit, x, y, NumberMBitmaps, NumberMWidths, NumberM0_HEIGHT);
}

void drawColon(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberLColon_WIDTH, NumberLColon_HEIGHT, NumberLColon);
}

void drawPeriodM(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberMPeriod_WIDTH, NumberMPeriod_HEIGHT, NumberMPeriod);
}

// ============================================================
// Width getters
// ============================================================

uint16_t getDigitLWidth(uint8_t digit)
{
  if (digit > 9) return 0;
  return NumberLWidths[digit];
}

uint16_t getDigitMWidth(uint8_t digit)
{
  if (digit > 9) return 0;
  return NumberMWidths[digit];
}

// ============================================================
// Unified glyph API
// ============================================================

void drawGlyph(uint8_t glyphIndex, uint16_t x, uint16_t y, FontSize size)
{
  if (glyphIndex <= 9)
  {
    // Digit 0-9
    if (size == FONT_L)
      drawDigitL(glyphIndex, x, y);
    else
      drawDigitM(glyphIndex, x, y);
  }
  else if (glyphIndex == GLYPH_PERIOD)
  {
    drawPeriodM(x, y); // Period only exists in M size
  }
  else if (glyphIndex == GLYPH_COLON)
  {
    drawColon(x, y); // Colon only exists in L size
  }
}

uint16_t getGlyphBitmapWidth(uint8_t glyphIndex, FontSize size)
{
  if (glyphIndex <= 9)
  {
    return (size == FONT_L) ? getDigitLWidth(glyphIndex) : getDigitMWidth(glyphIndex);
  }
  else if (glyphIndex == GLYPH_PERIOD)
  {
    return NumberMPeriod_WIDTH;
  }
  else if (glyphIndex == GLYPH_COLON)
  {
    return NumberLColon_WIDTH;
  }
  return 0;
}

// ============================================================
// Glyph sequence rendering
// ============================================================

uint16_t drawGlyphSequence(const uint8_t* glyphs, uint8_t count, uint16_t x, uint16_t y, FontSize size)
{
  if (count == 0) return x;

  uint16_t currentX = x;

  for (uint8_t i = 0; i < count; i++)
  {
    drawGlyph(glyphs[i], currentX, y, size);

    if (i < count - 1) // Not the last glyph
    {
      int16_t advance = getAdvance(glyphs[i], size);
      int16_t kern = getKerning(glyphs[i], glyphs[i + 1], size);
      LOGD(LogTag::FONT, "  [%d] glyph=%d, adv=%d, kern=%d", i, glyphs[i], advance, kern);
      currentX += advance + kern;
    }
    else // Last glyph
    {
      uint16_t bitmapWidth = getGlyphBitmapWidth(glyphs[i], size);
      LOGD(LogTag::FONT, "  [%d] glyph=%d, width=%d (last)", i, glyphs[i], bitmapWidth);
      currentX += bitmapWidth;
    }
  }

  return currentX;
}

uint16_t calcGlyphSequenceWidth(const uint8_t* glyphs, uint8_t count, FontSize size)
{
  if (count == 0) return 0;

  uint16_t width = 0;

  for (uint8_t i = 0; i < count - 1; i++)
  {
    int16_t kern = getKerning(glyphs[i], glyphs[i + 1], size);
    width += getAdvance(glyphs[i], size) + kern;
  }
  // Last glyph uses bitmap width
  width += getGlyphBitmapWidth(glyphs[count - 1], size);

  return width;
}

void fontRendererInit()
{
  // Nothing to initialize currently, but reserved for future use
}
