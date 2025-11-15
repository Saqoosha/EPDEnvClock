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

  EPD_FastMode1Init();  // Initialize the fast mode 1 of the EPD.
  EPD_Display_Clear();  // Clear the EPD display content.
  EPD_Update();  // Update the display.

  // Display image using EPD_ShowPicture (792x272 actual display area)
  // This avoids the 4px gap issue in the center
  EPD_ShowPicture(0, 0, 792, 272, DocumentImage, WHITE);
  EPD_Display(ImageBW);  // Display the buffer.
  EPD_Update();  // Update the display.
}

void loop() {
  // put your main code here, to run repeatedly:

}
