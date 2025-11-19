#include "display_manager.h"

#include <WiFi.h>

#include "EPD.h"
#include "bitmaps/Number_L_bitmap.h"
#include "bitmaps/Number_S_bitmap.h"
#include "bitmaps/Number_M_bitmap.h"
#include "bitmaps/Icon_bitmap.h"
#include "sensor_manager.h"

#include <pgmspace.h>

namespace
{
constexpr size_t kFrameBufferSize = 27200;
uint8_t ImageBW[kFrameBufferSize];

uint8_t lastDisplayedMinute = 255;
char g_statusMessage[64] = "Init...";

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

const uint8_t *NumberMBitmaps[] = {
    NumberM0, NumberM1, NumberM2, NumberM3, NumberM4,
    NumberM5, NumberM6, NumberM7, NumberM8, NumberM9};

const uint16_t NumberMWidths[] = {
    NumberM0_WIDTH, NumberM1_WIDTH, NumberM2_WIDTH, NumberM3_WIDTH, NumberM4_WIDTH,
    NumberM5_WIDTH, NumberM6_WIDTH, NumberM7_WIDTH, NumberM8_WIDTH, NumberM9_WIDTH};

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

void drawDigitM(uint8_t digit, uint16_t x, uint16_t y)
{
  if (digit > 9)
  {
    return;
  }
  drawBitmapCorrect(x, y, NumberMWidths[digit], NumberM0_HEIGHT, NumberMBitmaps[digit]);
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

void drawPeriodM(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberMPeriod_WIDTH, NumberMPeriod_HEIGHT, NumberMPeriod);
}

uint16_t getDigitMWidth(uint8_t digit)
{
  if (digit > 9)
  {
    return 0;
  }
  return NumberMWidths[digit];
}

uint16_t calculateTemperatureWidth(float temp)
{
  const uint16_t CHAR_SPACING = 5;
  uint16_t width = 0;

  // Format: "23.5" (rounded to 1 decimal place)
  int tempInt = (int)temp;
  int tempDecimal = (int)((temp - tempInt) * 10 + 0.5f); // Round to nearest

  // Handle carry-over when decimal rounds to 10
  if (tempDecimal >= 10)
  {
    tempInt++;
    tempDecimal = 0;
  }

  // First digit (tens)
  uint8_t digit = tempInt / 10;
  width += getDigitMWidth(digit) + CHAR_SPACING;

  // Second digit (ones)
  digit = tempInt % 10;
  width += getDigitMWidth(digit) + CHAR_SPACING;

  // Period
  width += NumberMPeriod_WIDTH + CHAR_SPACING;

  // Decimal digit
  digit = tempDecimal;
  width += getDigitMWidth(digit);

  return width;
}

uint16_t drawTemperature(float temp, uint16_t x, uint16_t y)
{
  const uint16_t CHAR_SPACING = 5;
  uint16_t currentX = x;

  // Format: "23.5" (rounded to 1 decimal place)
  int tempInt = (int)temp;
  int tempDecimal = (int)((temp - tempInt) * 10 + 0.5f); // Round to nearest

  // Handle carry-over when decimal rounds to 10
  if (tempDecimal >= 10)
  {
    tempInt++;
    tempDecimal = 0;
  }

  // First digit (tens)
  uint8_t digit = tempInt / 10;
  drawDigitM(digit, currentX, y);
  currentX += getDigitMWidth(digit) + CHAR_SPACING;

  // Second digit (ones)
  digit = tempInt % 10;
  drawDigitM(digit, currentX, y);
  currentX += getDigitMWidth(digit) + CHAR_SPACING;

  // Period
  drawPeriodM(currentX, y);
  currentX += NumberMPeriod_WIDTH + CHAR_SPACING;

  // Decimal digit
  digit = tempDecimal;
  drawDigitM(digit, currentX, y);
  currentX += getDigitMWidth(digit);

  return currentX; // Return end position
}

uint16_t calculateIntegerWidth(int value)
{
  const uint16_t CHAR_SPACING = 5;
  uint16_t width = 0;

  // Handle negative values
  if (value < 0)
  {
    value = 0; // Clamp to 0 for display
  }

  // Extract digits
  int remaining = value;
  int digits[4] = {0}; // Max 4 digits (e.g., 9999)
  int digitCount = 0;

  // Extract digits from right to left
  if (remaining == 0)
  {
    digits[0] = 0;
    digitCount = 1;
  }
  else
  {
    while (remaining > 0 && digitCount < 4)
    {
      digits[digitCount] = remaining % 10;
      remaining /= 10;
      digitCount++;
    }
  }

  // Calculate width from left to right (reverse order)
  for (int i = digitCount - 1; i >= 0; i--)
  {
    width += getDigitMWidth(digits[i]);
    if (i > 0) // Add spacing except after last digit
    {
      width += CHAR_SPACING;
    }
  }

  return width;
}

uint16_t drawInteger(int value, uint16_t x, uint16_t y)
{
  const uint16_t CHAR_SPACING = 5;
  uint16_t currentX = x;

  // Handle negative values
  if (value < 0)
  {
    value = 0; // Clamp to 0 for display
  }

  // Extract digits
  int remaining = value;
  int digits[4] = {0}; // Max 4 digits (e.g., 9999)
  int digitCount = 0;

  // Extract digits from right to left
  if (remaining == 0)
  {
    digits[0] = 0;
    digitCount = 1;
  }
  else
  {
    while (remaining > 0 && digitCount < 4)
    {
      digits[digitCount] = remaining % 10;
      remaining /= 10;
      digitCount++;
    }
  }

  // Draw digits from left to right (reverse order)
  for (int i = digitCount - 1; i >= 0; i--)
  {
    drawDigitM(digits[i], currentX, y);
    currentX += getDigitMWidth(digits[i]) + CHAR_SPACING;
  }

  // Subtract last spacing since it's after the last digit
  if (digitCount > 0)
  {
    currentX -= CHAR_SPACING;
  }

  return currentX; // Return end position
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
  char statusLine[128];
  char ipStr[16];
  const int yPos = 4; // Adjusted for 12px font (centered in top 20px area?) or just top aligned
  const uint16_t fontSize = 12;

  // Clear the status area first
  EPD_ClearWindows(0, 0, EPD_W, 20, WHITE);

  // Format IP address without using String class
  if (networkState.wifiConnected && WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }
  else
  {
    ipStr[0] = '\0';
  }

  long rssi = WiFi.RSSI();
  uint32_t freeHeap = ESP.getFreeHeap();

  // Format: "WiFi:SSID(RSSI) IP:1.2.3.4 NTP:OK(diff) Heap:12345"
  // Truncate SSID if too long? For now just show basic info

  // Layout:
  // Left: WiFi Status + SSID + RSSI + IP
  // Right: NTP Status + Heap + Uptime

  // Let's try a single line with compact info
  // W:Connected(SSID, -50dBm) 192.168.1.100 | N:OK(123ms) | U:123m | H:12345

  const char *wifiStatus = networkState.wifiConnected ? "OK" : "--";
  const char *ntpStatus = networkState.ntpSynced ? "OK" : "--";

  if (networkState.wifiConnected && ipStr[0] != '\0')
  {
    snprintf(statusLine, sizeof(statusLine), "W:%s(%ld) %s | N:%s | U:%lum | H:%u | Msg:%s",
             wifiStatus, rssi, ipStr, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
  }
  else
  {
    snprintf(statusLine, sizeof(statusLine), "W:%s | N:%s | U:%lum | H:%u | Msg:%s",
             wifiStatus, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
  }

  EPD_ShowString(8, yPos, statusLine, fontSize, BLACK);
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

void DisplayManager_SetStatus(const char *message)
{
  if (message)
  {
    strncpy(g_statusMessage, message, sizeof(g_statusMessage) - 1);
    g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';
  }
}

void DisplayManager_DrawSetupStatus(const char *message)
{
  DisplayManager_SetStatus(message);
  // Clear status area
  EPD_ClearWindows(0, 0, EPD_W, 20, WHITE);
  const uint16_t fontSize = 12;
  const int yPos = 4;
  EPD_ShowString(8, yPos, message, fontSize, BLACK);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

bool DisplayManager_UpdateDisplay(const NetworkState &networkState, bool forceUpdate)
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

  // Minute has changed - read sensor value at the same time
  if (SensorManager_IsInitialized())
  {
    DisplayManager_SetStatus("Reading...");
    SensorManager_Read();
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

  // Draw sensor icons and values
  if (SensorManager_IsInitialized())
  {
    const uint16_t UNIT_Y_OFFSET = 26; // Unit icons are 26px below values
    const uint16_t ICON_VALUE_SPACING = 6; // Spacing between icon and value
    const uint16_t VALUE_UNIT_SPACING = 5; // Spacing between value and unit (same as digit spacing)
    const uint16_t SCREEN_WIDTH = 792;     // Actual display width
    const uint16_t SIDE_MARGIN = 16;       // Margin from screen edges
    const uint16_t SENSOR_Y = 200;         // Y position for sensor data

    // Get sensor values
    float temp = SensorManager_GetTemperature();
    float humidity = SensorManager_GetHumidity();
    uint16_t co2 = SensorManager_GetCO2();

    // Calculate value widths
    uint16_t tempValueWidth = calculateTemperatureWidth(temp);
    uint16_t humidityValueWidth = calculateIntegerWidth((int)(humidity + 0.5f));
    uint16_t co2ValueWidth = calculateIntegerWidth(co2);

    // Calculate total display widths (icon + spacing + value + spacing + unit)
    uint16_t tempDisplayWidth = IconTemp_WIDTH + ICON_VALUE_SPACING + tempValueWidth + VALUE_UNIT_SPACING + UnitC_WIDTH;
    uint16_t humidityDisplayWidth = IconHumidity_WIDTH + ICON_VALUE_SPACING + humidityValueWidth + VALUE_UNIT_SPACING + UnitPercent_WIDTH;
    uint16_t co2DisplayWidth = IconCO2_WIDTH + ICON_VALUE_SPACING + co2ValueWidth + VALUE_UNIT_SPACING + UnitPpm_WIDTH;

    // Calculate X positions
    uint16_t tempX = SIDE_MARGIN;
    uint16_t co2X = SCREEN_WIDTH - SIDE_MARGIN - co2DisplayWidth;
    uint16_t availableSpace = co2X - (tempX + tempDisplayWidth);
    uint16_t humidityX = tempX + tempDisplayWidth + (availableSpace - humidityDisplayWidth) / 2;

    // Draw temperature
    drawBitmapCorrect(tempX, SENSOR_Y, IconTemp_WIDTH, IconTemp_HEIGHT, IconTemp);
    uint16_t tempValueX = tempX + IconTemp_WIDTH + ICON_VALUE_SPACING;
    uint16_t tempEndX = drawTemperature(temp, tempValueX, SENSOR_Y);
    drawBitmapCorrect(tempEndX + VALUE_UNIT_SPACING, SENSOR_Y + UNIT_Y_OFFSET, UnitC_WIDTH, UnitC_HEIGHT, UnitC);

    // Draw humidity
    drawBitmapCorrect(humidityX, SENSOR_Y, IconHumidity_WIDTH, IconHumidity_HEIGHT, IconHumidity);
    uint16_t humidityValueX = humidityX + IconHumidity_WIDTH + ICON_VALUE_SPACING;
    uint16_t humidityEndX = drawInteger((int)(humidity + 0.5f), humidityValueX, SENSOR_Y);
    drawBitmapCorrect(humidityEndX + VALUE_UNIT_SPACING, SENSOR_Y + UNIT_Y_OFFSET, UnitPercent_WIDTH, UnitPercent_HEIGHT, UnitPercent);

    // Draw CO2
    drawBitmapCorrect(co2X, SENSOR_Y, IconCO2_WIDTH, IconCO2_HEIGHT, IconCO2);
    uint16_t co2ValueX = co2X + IconCO2_WIDTH + ICON_VALUE_SPACING;
    uint16_t co2EndX = drawInteger(co2, co2ValueX, SENSOR_Y);
    drawBitmapCorrect(co2EndX + VALUE_UNIT_SPACING, SENSOR_Y + UNIT_Y_OFFSET, UnitPpm_WIDTH, UnitPpm_HEIGHT, UnitPpm);
  }

  const unsigned long drawDuration = micros() - startTime;

  startTime = micros();
  EPD_Display(ImageBW);
  const unsigned long displayDuration = micros() - startTime;

  startTime = micros();
  DisplayManager_SetStatus("Updating...");
  EPD_PartUpdate();
  const unsigned long updateDuration = micros() - startTime;

  lastDisplayedMinute = currentMinute;

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
