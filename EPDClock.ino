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

// Global counter variable
uint32_t counter = 0;
unsigned long lastUpdateTime = 0;

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
  EPD_PartUpdate(); // This is super important! It's required for partial update to work properly
  delay(1000);  // Wait for display to stabilize

  lastUpdateTime = millis();
}

void loop() {
  // Check if 1 second has passed
  if (millis() - lastUpdateTime >= 1000) {
    // Calculate number of digits for proper clearing
    uint32_t temp = counter;
    uint8_t digits = 1;
    if (temp == 0) {
      digits = 1;
    } else {
      while (temp >= 10) {
        temp /= 10;
        digits++;
      }
    }

    // Clear area for the number (enough space for up to 10 digits)
    // Each digit is 100 pixels wide
    EPD_ClearWindows(8, 8, 8 + (digits * Number0_WIDTH), 8 + Number0_HEIGHT, WHITE);

    // Draw the counter value (counts up infinitely)
    drawNumber(counter, 8, 8);

    counter++;

    // Update the display using partial update (faster and lower power consumption)
    EPD_Display(ImageBW);
    EPD_PartUpdate();  // Use partial update instead of full update for better performance

    // Update the last update time
    lastUpdateTime = millis();
  }
}
