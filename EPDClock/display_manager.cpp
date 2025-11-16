#include "display_manager.h"

#include "EPD.h"
#include "bitmaps/Number_L_bitmap.h"
#include "bitmaps/Number_S_bitmap.h"

#include <pgmspace.h>

namespace
{
constexpr size_t kFrameBufferSize = 27200;
uint8_t ImageBW[kFrameBufferSize];

uint8_t lastDisplayedMinute = 255;
uint8_t lastDisplayedDay = 255;
unsigned long lastUpdateTime = 0;

void drawBitmapCorrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *bitmap)
{
  const uint16_t widthByte = (width + 7) / 8;

  for (uint16_t row = 0; row < height; row++)
  {
    uint16_t pixel_x = x;

    for (uint16_t col_byte = 0; col_byte < widthByte; col_byte++)
    {
      uint8_t bitmap_byte = pgm_read_byte(&bitmap[row * widthByte + col_byte]);

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

const uint8_t *NumberBitmaps[] = {
    Number0, Number1, Number2, Number3, Number4,
    Number5, Number6, Number7, Number8, Number9};

const uint16_t NumberWidths[] = {
    Number0_WIDTH, Number1_WIDTH, Number2_WIDTH, Number3_WIDTH, Number4_WIDTH,
    Number5_WIDTH, Number6_WIDTH, Number7_WIDTH, Number8_WIDTH, Number9_WIDTH};

const uint8_t *NumberLBitmaps[] = {
    NumberL0, NumberL1, NumberL2, NumberL3, NumberL4,
    NumberL5, NumberL6, NumberL7, NumberL8, NumberL9};

void drawDigit(uint8_t digit, uint16_t x, uint16_t y)
{
  if (digit > 9)
  {
    return;
  }
  drawBitmapCorrect(x, y, NumberWidths[digit], Number0_HEIGHT, NumberBitmaps[digit]);
}

void drawDigitL(uint8_t digit, uint16_t x, uint16_t y)
{
  if (digit > 9)
  {
    return;
  }
  drawBitmapCorrect(x, y, NumberL0_WIDTH, NumberL0_HEIGHT, NumberLBitmaps[digit]);
}

uint16_t getDigitWidth(uint8_t digit)
{
  if (digit > 9)
  {
    return 0;
  }
  return NumberWidths[digit];
}

void drawPeriod(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberPeriod_WIDTH, NumberPeriod_HEIGHT, NumberPeriod);
}

void drawColon(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberColon_WIDTH, NumberColon_HEIGHT, NumberColon);
}

void drawDate(uint16_t year, uint8_t month, uint8_t day, uint16_t x, uint16_t y)
{
  const uint16_t CHAR_SPACING = 6;
  uint16_t currentX = x;

  uint8_t digit = (year / 1000) % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = (year / 100) % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = (year / 10) % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = year % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  drawPeriod(currentX, y);
  currentX += NumberPeriod_WIDTH + CHAR_SPACING;

  digit = month / 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = month % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  drawPeriod(currentX, y);
  currentX += NumberPeriod_WIDTH + CHAR_SPACING;

  digit = day / 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = day % 10;
  drawDigit(digit, currentX, y);
}

void drawTime(uint8_t hour, uint8_t minute, uint16_t x, uint16_t y)
{
  uint16_t currentX = x;

  uint8_t digit = hour / 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  digit = hour % 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  currentX += 6;
  drawColon(currentX, y);
  currentX += NumberColon_WIDTH + 6;

  digit = minute / 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  digit = minute % 10;
  drawDigitL(digit, currentX, y);
}

void drawStatus(const NetworkState &networkState)
{
  char statusLine[80];
  const int yPos = 2;
  const uint16_t fontSize = 16;

  snprintf(statusLine, sizeof(statusLine), networkState.wifiConnected ? "WiFi:OK" : "WiFi:--");
  EPD_ShowString(8, yPos, statusLine, fontSize, BLACK);

  snprintf(statusLine, sizeof(statusLine), networkState.ntpSynced ? "NTP:OK" : "NTP:--");
  EPD_ShowString(120, yPos, statusLine, fontSize, BLACK);

  if (networkState.wifiConnectTime > 0)
  {
    snprintf(statusLine, sizeof(statusLine), "W:%lums", networkState.wifiConnectTime);
    EPD_ShowString(200, yPos, statusLine, fontSize, BLACK);
  }

  if (networkState.ntpSyncTime > 0)
  {
    snprintf(statusLine, sizeof(statusLine), "N:%lums", networkState.ntpSyncTime);
    EPD_ShowString(300, yPos, statusLine, fontSize, BLACK);
  }

  const unsigned long uptimeMinutes = millis() / 60000;
  snprintf(statusLine, sizeof(statusLine), "U:%lum", uptimeMinutes);
  EPD_ShowString(400, yPos, statusLine, fontSize, BLACK);
}

} // namespace

void DisplayManager_Init()
{
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);

  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  EPD_PartUpdate();

  delay(500);
}

void DisplayManager_DrawSetupStatus(const char *message)
{
  EPD_ClearWindows(0, EPD_H - 20, EPD_W, EPD_H, WHITE);
  const uint16_t fontSize = 16;
  const int yPos = EPD_H - 18;
  EPD_ShowString(8, yPos, message, fontSize, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

bool DisplayManager_UpdateClock(const NetworkState &networkState, bool forceUpdate)
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("[Display] Failed to get local time");
    return false;
  }

  const uint8_t currentMinute = timeinfo.tm_min;

  if (!forceUpdate && currentMinute == lastDisplayedMinute)
  {
    return false;
  }

  const uint8_t hour = timeinfo.tm_hour;
  const uint16_t year = timeinfo.tm_year + 1900;
  const uint8_t month = timeinfo.tm_mon + 1;
  const uint8_t day = timeinfo.tm_mday;

  unsigned long startTime = micros();
  Paint_Clear(WHITE);
  drawTime(hour, currentMinute, 330, 53);
  drawDate(year, month, day, 15, 125);
  drawStatus(networkState);
  const unsigned long drawDuration = micros() - startTime;

  startTime = micros();
  EPD_Display(ImageBW);
  const unsigned long displayDuration = micros() - startTime;

  startTime = micros();
  EPD_PartUpdate();
  const unsigned long updateDuration = micros() - startTime;

  lastDisplayedMinute = currentMinute;
  lastDisplayedDay = day;
  lastUpdateTime = millis();

  Serial.print("[Display] Updated: ");
  Serial.print(hour);
  Serial.print(":");
  Serial.println(currentMinute);
  Serial.print("[Display] Draw: ");
  Serial.print(drawDuration);
  Serial.print(" us, EPD_Display: ");
  Serial.print(displayDuration);
  Serial.print(" us, EPD_PartUpdate: ");
  Serial.print(updateDuration);
  Serial.print(" us, Total: ");
  Serial.print(drawDuration + displayDuration + updateDuration);
  Serial.println(" us");

  return true;
}

uint8_t *DisplayManager_GetFrameBuffer()
{
  return ImageBW;
}
