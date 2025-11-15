#include <Arduino.h>     // Includes Arduino core library files
#include "EPD.h"         // Includes library files for electronic paper screens
#include "Document_bitmap.h"  // Include the converted image data

// Define an array to store image data, with a size of 27200 bytes
uint8_t ImageBW[27200];

void setup() {
  // Set the screen power control pin.
  pinMode(7, OUTPUT);  // Set pin 7 as output mode.
  digitalWrite(7, HIGH);  // Set pin 7 to high level to turn on the screen power.

  // Initialize the EPD (electronic paper display) screen.
  EPD_GPIOInit();  // Initialize the GPIO pins of the EPD.
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);  // Create a new canvas with size EPD_W x EPD_H and background color white.
  Paint_Clear(WHITE);  // Clear the canvas with background color white.

  // Copy image data from PROGMEM to buffer (fast method)
  // DocumentImage is 800x272 = 27200 bytes, same as ImageBW buffer
  for (uint32_t i = 0; i < DocumentImage_SIZE; i++) {
    ImageBW[i] = pgm_read_byte(&DocumentImage[i]);
  }

  EPD_FastMode1Init();  // Initialize the fast mode 1 of the EPD.
  EPD_Display_Clear();  // Clear the EPD display content.
  EPD_Display(ImageBW);  // Display the image.
  EPD_Update();  // Update the display.
}

void loop() {
  // put your main code here, to run repeatedly:

}
