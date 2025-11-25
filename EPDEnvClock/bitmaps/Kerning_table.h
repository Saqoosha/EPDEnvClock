#ifndef KERNING_TABLE_H
#define KERNING_TABLE_H

#include <Arduino.h>

// Font size enum for unified glyph rendering
enum FontSize {
  FONT_L,  // Large (116px height) - for time display
  FONT_M   // Medium (58px height) - for date/temp/humidity
};

// Font metrics for Baloo Bhai 2 ExtraBold font
// Values are in font units (1000 units = 1 em)
// To convert to pixels: pixels = units * font_size / 1000
//
// Font sizes used:
//   Number M: 90.8px (height 58px)
//   Number L: 181.5px (height 116px)

// Advance widths from font (in font units)
// These include the glyph width plus designed sidebearings
constexpr int16_t AdvanceWidths[] = {
  606,  // 0
  393,  // 1
  522,  // 2
  518,  // 3
  602,  // 4
  522,  // 5
  558,  // 6
  482,  // 7
  560,  // 8
  558,  // 9
  238,  // 10 = period
  258,  // 11 = colon
};

// Spacing adjustment (pixels) - negative = tighter, positive = looser
constexpr int16_t kSpacingAdjustL = -4;  // Adjust for Number L (time display)
constexpr int16_t kSpacingAdjustM = 0;   // Adjust for Number M (date/temp display)

// Get advance width in pixels for Number L (float for precision)
inline float getAdvanceLf(uint8_t glyphIndex) {
  if (glyphIndex > 11) return 0.0f;
  return AdvanceWidths[glyphIndex] * 181.5f / 1000.0f + kSpacingAdjustL;
}

// Get advance width in pixels for Number M (float for precision)
inline float getAdvanceMf(uint8_t glyphIndex) {
  if (glyphIndex > 11) return 0.0f;
  return AdvanceWidths[glyphIndex] * 90.8f / 1000.0f + kSpacingAdjustM;
}

// Integer versions (for backward compatibility)
inline int16_t getAdvanceL(uint8_t glyphIndex) {
  return (int16_t)(getAdvanceLf(glyphIndex) + 0.5f);
}

inline int16_t getAdvanceM(uint8_t glyphIndex) {
  return (int16_t)(getAdvanceMf(glyphIndex) + 0.5f);
}

// Kerning pair structure
struct KerningPair {
  uint8_t left;   // Left glyph index (0-9 for digits, 10=period, 11=colon)
  uint8_t right;  // Right glyph index
  int16_t value;  // Kerning value in font units (positive = more space, negative = less space)
};

// Glyph indices
constexpr uint8_t GLYPH_0 = 0;
constexpr uint8_t GLYPH_1 = 1;
constexpr uint8_t GLYPH_2 = 2;
constexpr uint8_t GLYPH_3 = 3;
constexpr uint8_t GLYPH_4 = 4;
constexpr uint8_t GLYPH_5 = 5;
constexpr uint8_t GLYPH_6 = 6;
constexpr uint8_t GLYPH_7 = 7;
constexpr uint8_t GLYPH_8 = 8;
constexpr uint8_t GLYPH_9 = 9;
constexpr uint8_t GLYPH_PERIOD = 10;
constexpr uint8_t GLYPH_COLON = 11;

// Kerning table for Number L (time display with colon)
// Only includes pairs with non-zero kerning values
constexpr KerningPair KerningTableL[] PROGMEM = {
  // Colon pairs
  {GLYPH_COLON, GLYPH_1, 10},
  {GLYPH_COLON, GLYPH_2, 5},
  {GLYPH_COLON, GLYPH_3, 10},
  {GLYPH_COLON, GLYPH_8, 5},
  {GLYPH_1, GLYPH_COLON, 8},
  {GLYPH_2, GLYPH_COLON, 5},
  {GLYPH_3, GLYPH_COLON, 10},
  {GLYPH_5, GLYPH_COLON, 5},
  {GLYPH_8, GLYPH_COLON, 5},

  // Digit pairs
  {GLYPH_0, GLYPH_1, -10},
  {GLYPH_0, GLYPH_2, -15},
  {GLYPH_0, GLYPH_3, -8},
  {GLYPH_0, GLYPH_4, 5},
  {GLYPH_0, GLYPH_7, -12},
  {GLYPH_0, GLYPH_9, -5},
  {GLYPH_1, GLYPH_2, 10},
  {GLYPH_1, GLYPH_4, 5},
  {GLYPH_1, GLYPH_7, 8},
  {GLYPH_2, GLYPH_4, -5},
  {GLYPH_2, GLYPH_7, -5},
  {GLYPH_3, GLYPH_7, -5},
  {GLYPH_3, GLYPH_8, 8},
  {GLYPH_3, GLYPH_9, 4},
  {GLYPH_4, GLYPH_0, 10},
  {GLYPH_4, GLYPH_1, -20},
  {GLYPH_4, GLYPH_4, 20},
  {GLYPH_4, GLYPH_5, -15},
  {GLYPH_4, GLYPH_6, 10},
  {GLYPH_4, GLYPH_7, -25},
  {GLYPH_4, GLYPH_8, 15},
  {GLYPH_4, GLYPH_9, -15},
  {GLYPH_5, GLYPH_0, 5},
  {GLYPH_5, GLYPH_3, 10},
  {GLYPH_5, GLYPH_4, 5},
  {GLYPH_5, GLYPH_6, 4},
  {GLYPH_5, GLYPH_8, 10},
  {GLYPH_5, GLYPH_9, -10},
  {GLYPH_6, GLYPH_1, -8},
  {GLYPH_6, GLYPH_2, -10},
  {GLYPH_6, GLYPH_3, -7},
  {GLYPH_6, GLYPH_4, 5},
  {GLYPH_6, GLYPH_5, -5},
  {GLYPH_6, GLYPH_7, -8},
  {GLYPH_6, GLYPH_9, -10},
  {GLYPH_7, GLYPH_0, 3},
  {GLYPH_7, GLYPH_1, 26},
  {GLYPH_7, GLYPH_3, 5},
  {GLYPH_7, GLYPH_4, -25},
  {GLYPH_7, GLYPH_6, 3},
  {GLYPH_7, GLYPH_7, 15},
  {GLYPH_7, GLYPH_8, 4},
  {GLYPH_7, GLYPH_9, 5},
  {GLYPH_8, GLYPH_2, -5},
  {GLYPH_8, GLYPH_5, -10},
  {GLYPH_8, GLYPH_7, -15},
  {GLYPH_9, GLYPH_2, -15},
  {GLYPH_9, GLYPH_3, -8},
  {GLYPH_9, GLYPH_7, -12},
};
constexpr size_t KerningTableL_SIZE = sizeof(KerningTableL) / sizeof(KerningTableL[0]);

// Kerning table for Number M and S (date/values with period)
// Only includes pairs with non-zero kerning values
constexpr KerningPair KerningTableMS[] PROGMEM = {
  // Period pairs
  {GLYPH_PERIOD, GLYPH_0, -12},
  {GLYPH_PERIOD, GLYPH_1, -30},
  {GLYPH_PERIOD, GLYPH_4, 5},
  {GLYPH_PERIOD, GLYPH_6, -15},
  {GLYPH_PERIOD, GLYPH_9, -20},
  {GLYPH_0, GLYPH_PERIOD, -12},
  {GLYPH_2, GLYPH_PERIOD, 5},
  {GLYPH_3, GLYPH_PERIOD, 2},
  {GLYPH_7, GLYPH_PERIOD, -30},
  {GLYPH_8, GLYPH_PERIOD, 2},
  {GLYPH_9, GLYPH_PERIOD, -25},

  // Digit pairs (same as L)
  {GLYPH_0, GLYPH_1, -10},
  {GLYPH_0, GLYPH_2, -15},
  {GLYPH_0, GLYPH_3, -8},
  {GLYPH_0, GLYPH_4, 5},
  {GLYPH_0, GLYPH_7, -12},
  {GLYPH_0, GLYPH_9, -5},
  {GLYPH_1, GLYPH_2, 10},
  {GLYPH_1, GLYPH_4, 5},
  {GLYPH_1, GLYPH_7, 8},
  {GLYPH_2, GLYPH_4, -5},
  {GLYPH_2, GLYPH_7, -5},
  {GLYPH_3, GLYPH_7, -5},
  {GLYPH_3, GLYPH_8, 8},
  {GLYPH_3, GLYPH_9, 4},
  {GLYPH_4, GLYPH_0, 10},
  {GLYPH_4, GLYPH_1, -20},
  {GLYPH_4, GLYPH_4, 20},
  {GLYPH_4, GLYPH_5, -15},
  {GLYPH_4, GLYPH_6, 10},
  {GLYPH_4, GLYPH_7, -25},
  {GLYPH_4, GLYPH_8, 15},
  {GLYPH_4, GLYPH_9, -15},
  {GLYPH_5, GLYPH_0, 5},
  {GLYPH_5, GLYPH_3, 10},
  {GLYPH_5, GLYPH_4, 5},
  {GLYPH_5, GLYPH_6, 4},
  {GLYPH_5, GLYPH_8, 10},
  {GLYPH_5, GLYPH_9, -10},
  {GLYPH_6, GLYPH_1, -8},
  {GLYPH_6, GLYPH_2, -10},
  {GLYPH_6, GLYPH_3, -7},
  {GLYPH_6, GLYPH_4, 5},
  {GLYPH_6, GLYPH_5, -5},
  {GLYPH_6, GLYPH_7, -8},
  {GLYPH_6, GLYPH_9, -10},
  {GLYPH_7, GLYPH_0, 3},
  {GLYPH_7, GLYPH_1, 26},
  {GLYPH_7, GLYPH_3, 5},
  {GLYPH_7, GLYPH_4, -25},
  {GLYPH_7, GLYPH_6, 3},
  {GLYPH_7, GLYPH_7, 15},
  {GLYPH_7, GLYPH_8, 4},
  {GLYPH_7, GLYPH_9, 5},
  {GLYPH_8, GLYPH_2, -5},
  {GLYPH_8, GLYPH_5, -10},
  {GLYPH_8, GLYPH_7, -15},
  {GLYPH_9, GLYPH_2, -15},
  {GLYPH_9, GLYPH_3, -8},
  {GLYPH_9, GLYPH_7, -12},
};
constexpr size_t KerningTableMS_SIZE = sizeof(KerningTableMS) / sizeof(KerningTableMS[0]);

// Font size constants (in pixels, for kerning calculation)
constexpr float FONT_SIZE_M = 90.8f;   // Number M (height 58px)
constexpr float FONT_SIZE_L = 181.5f;  // Number L (height 116px)
constexpr float FONT_UNITS_PER_EM = 1000.0f;

// Helper function to get kerning value for a pair (float for precision)
// Returns kerning adjustment in pixels (positive = add space, negative = reduce space)
inline float getKerningLf(uint8_t left, uint8_t right) {
  for (size_t i = 0; i < KerningTableL_SIZE; i++) {
    KerningPair pair;
    memcpy_P(&pair, &KerningTableL[i], sizeof(KerningPair));
    if (pair.left == left && pair.right == right) {
      return pair.value * FONT_SIZE_L / FONT_UNITS_PER_EM;
    }
  }
  return 0.0f;
}

inline float getKerningMf(uint8_t left, uint8_t right) {
  for (size_t i = 0; i < KerningTableMS_SIZE; i++) {
    KerningPair pair;
    memcpy_P(&pair, &KerningTableMS[i], sizeof(KerningPair));
    if (pair.left == left && pair.right == right) {
      return pair.value * FONT_SIZE_M / FONT_UNITS_PER_EM;
    }
  }
  return 0.0f;
}

// Integer versions (for backward compatibility)
inline int16_t getKerningL(uint8_t left, uint8_t right) {
  return (int16_t)(getKerningLf(left, right) + 0.5f);
}

inline int16_t getKerningM(uint8_t left, uint8_t right) {
  return (int16_t)(getKerningMf(left, right) + 0.5f);
}

// Convert digit (0-9) to glyph index
inline uint8_t digitToGlyphIndex(uint8_t digit) {
  return digit; // 0-9 maps directly
}

// ============================================================
// Unified API - Use these functions for consistent glyph handling
// ============================================================

// Get advance width for any font size (float for precision)
inline float getAdvancef(uint8_t glyphIndex, FontSize size) {
  return (size == FONT_L) ? getAdvanceLf(glyphIndex) : getAdvanceMf(glyphIndex);
}

// Get kerning for any font size (float for precision)
inline float getKerningf(uint8_t left, uint8_t right, FontSize size) {
  return (size == FONT_L) ? getKerningLf(left, right) : getKerningMf(left, right);
}

// Integer versions (for backward compatibility)
inline int16_t getAdvance(uint8_t glyphIndex, FontSize size) {
  return (int16_t)(getAdvancef(glyphIndex, size) + 0.5f);
}

inline int16_t getKerning(uint8_t left, uint8_t right, FontSize size) {
  return (int16_t)(getKerningf(left, right, size) + 0.5f);
}

// Calculate width of a glyph sequence
inline uint16_t calculateGlyphsWidth(const uint8_t* glyphs, uint8_t count, FontSize size,
                                     uint16_t (*getBitmapWidth)(uint8_t)) {
  if (count == 0) return 0;

  uint16_t width = 0;
  for (uint8_t i = 0; i < count - 1; i++) {
    int16_t kern = getKerning(glyphs[i], glyphs[i + 1], size);
    width += getAdvance(glyphs[i], size) + kern;
  }
  // Last glyph uses bitmap width (no trailing space)
  width += getBitmapWidth(glyphs[count - 1]);
  return width;
}

#endif // KERNING_TABLE_H
