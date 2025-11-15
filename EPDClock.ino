#include "EPD.h"         // Includes library files for electronic paper screens
#include "Number_bitmap.h"  // Include the converted number image data
#include <pgmspace.h>

// Define an array to store image data, with a size of 27200 bytes
uint8_t ImageBW[27200];

// Custom function to draw bitmap correctly handling row boundaries
// This fixes EPD_ShowPicture's issue with row boundaries
void drawBitmapCorrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap, uint16_t color) {
  uint16_t width_byte = (width + 7) / 8;  // Bytes per row

  for (uint16_t row = 0; row < height; row++) {
    uint16_t pixel_x = x;

    for (uint16_t col_byte = 0; col_byte < width_byte; col_byte++) {
      uint8_t bitmap_byte = pgm_read_byte(&bitmap[row * width_byte + col_byte]);

      for (uint8_t bit = 0; bit < 8; bit++) {
        // Check if we've reached the end of the row
        if (pixel_x >= x + width) {
          break;
        }

        // Check bit (MSB first)
        if (bitmap_byte & (0x80 >> bit)) {
          Paint_SetPixel(pixel_x, y + row, BLACK);
        } else {
          Paint_SetPixel(pixel_x, y + row, WHITE);
        }

        pixel_x++;
      }
    }
  }
}

// Array of number bitmap pointers (all numbers are same size: 100x116)
const uint8_t* NumberBitmaps[] = {
  Number0, Number1, Number2, Number3, Number4,
  Number5, Number6, Number7, Number8, Number9
};

// Draw a single digit using bitmap images
void drawDigit(uint8_t digit, uint16_t x, uint16_t y) {
  if (digit > 9) return;  // Invalid digit
  drawBitmapCorrect(x, y, Number0_WIDTH, Number0_HEIGHT, NumberBitmaps[digit], WHITE);
}

// Draw a multi-digit number using bitmap images
void drawNumber(uint32_t num, uint16_t x, uint16_t y) {
  if (num == 0) {
    // Special case for 0
    drawDigit(0, x, y);
    return;
  }

  // Calculate number of digits
  uint32_t temp = num;
  uint8_t digits = 0;
  while (temp > 0) {
    temp /= 10;
    digits++;
  }

  // Draw digits from left to right
  temp = num;
  uint32_t divisor = 1;
  for (uint8_t i = 1; i < digits; i++) {
    divisor *= 10;
  }

  for (uint8_t i = 0; i < digits; i++) {
    uint8_t digit = (temp / divisor) % 10;
    drawDigit(digit, x + i * Number0_WIDTH, y);
    divisor /= 10;
  }
}

// Draw colon using bitmap image
void drawColon(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberColon_WIDTH, NumberColon_HEIGHT, NumberColon, WHITE);
}

// Draw time in format "HH:MM" using bitmap images
void drawTime(uint8_t hour, uint8_t minute, uint16_t x, uint16_t y)
{
  // Draw hour (2 digits)
  drawDigit(hour / 10, x, y);
  drawDigit(hour % 10, x + Number0_WIDTH, y);

  // Draw colon
  drawColon(x + 2 * Number0_WIDTH, y);

  // Draw minute (2 digits)
  drawDigit(minute / 10, x + 2 * Number0_WIDTH + NumberColon_WIDTH, y);
  drawDigit(minute % 10, x + 3 * Number0_WIDTH + NumberColon_WIDTH, y);
}

void setup() {
  // Set the screen power control pin.
  pinMode(7, OUTPUT);  // Set pin 7 as output mode.
  digitalWrite(7, HIGH);  // Set pin 7 to high level to turn on the screen power.

  // Initialize random seed (use analog pin noise or timer)
  randomSeed(analogRead(0));

  // Initialize the EPD (electronic paper display) screen.
  EPD_GPIOInit();  // Initialize the GPIO pins of the EPD.
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);  // Create a new canvas with size EPD_W x EPD_H and background color white.
  Paint_Clear(WHITE);  // Clear the canvas with background color white.

  EPD_FastMode1Init();  // Initialize the fast mode 1 of the EPD.
  EPD_Display_Clear();  // Clear the EPD display content.
  EPD_Update();         // Complete update
  EPD_PartUpdate(); // This is super important! It's required for partial update to work properly
  delay(1000);  // Wait for display to stabilize

  // Draw "12:34" using custom bitmaps
  drawTime(12, 34, 8, 8);

  // Update the display using partial update (faster and lower power consumption)
  EPD_Display(ImageBW);
  EPD_PartUpdate(); // Use partial update instead of full update for better performance
}

void loop()
{
  // Do nothing - display is shown once at startup
}
