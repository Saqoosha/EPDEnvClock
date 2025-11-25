#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include <Arduino.h>
#include "bitmaps/Kerning_table.h"

// ============================================================
// Font Renderer - Unified glyph drawing with kerning support
// ============================================================

// Initialize font renderer (call once at startup)
void fontRendererInit();

// Low-level glyph drawing
void drawGlyph(uint8_t glyphIndex, uint16_t x, uint16_t y, FontSize size);
uint16_t getGlyphBitmapWidth(uint8_t glyphIndex, FontSize size);

// Glyph sequence drawing
uint16_t drawGlyphSequence(const uint8_t* glyphs, uint8_t count, uint16_t x, uint16_t y, FontSize size);
uint16_t calcGlyphSequenceWidth(const uint8_t* glyphs, uint8_t count, FontSize size);

// Convenience functions for specific glyphs
void drawDigitL(uint8_t digit, uint16_t x, uint16_t y);
void drawDigitM(uint8_t digit, uint16_t x, uint16_t y);
void drawColon(uint16_t x, uint16_t y);
void drawPeriodM(uint16_t x, uint16_t y);

// Get bitmap widths
uint16_t getDigitLWidth(uint8_t digit);
uint16_t getDigitMWidth(uint8_t digit);

// Low-level bitmap drawing (used internally and by display_manager for icons)
void drawBitmapCorrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap);

#endif // FONT_RENDERER_H
