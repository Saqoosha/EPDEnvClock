#include "display_manager.h"

#include <WiFi.h>

#include "EPD.h"
#include "EPD_Init.h"
#include "font_renderer.h"
#include "bitmaps/Icon_bitmap.h"
#include "bitmaps/Kerning_table.h"
#include "sensor_manager.h"
#include "deep_sleep_manager.h"
#include "logger.h"
#include "network_manager.h"

namespace
{
constexpr size_t kFrameBufferSize = 27200;
constexpr uint16_t kScreenWidth = 792;
constexpr uint16_t kScreenHeight = 272;

// Layout constants
constexpr uint16_t kTimeX = 16;
constexpr uint16_t kTimeY = 123;
constexpr uint16_t kDateX = 16;
constexpr uint16_t kDateY = 45;
constexpr uint16_t kTempValueX = 546;
constexpr uint16_t kTempValueY = 33;
constexpr uint16_t kHumidityValueX = 546;
constexpr uint16_t kHumidityValueY = 114;
constexpr uint16_t kCO2ValueX = 546;
constexpr uint16_t kCO2ValueY = 193;
constexpr uint16_t kSideMargin = 16;
constexpr uint16_t kUnitYOffset = 26;
constexpr uint16_t kIconValueSpacing = 6;
constexpr uint16_t kValueUnitSpacing = 5;
// Spacing is now handled by font advance widths + kerning from Kerning_table.h

uint8_t ImageBW[kFrameBufferSize];

char g_statusMessage[64] = "Init...";

// ============================================================
// Glyph Sequence Builders
// ============================================================

// Build temperature glyph sequence: "23.5" -> [2, 3, PERIOD, 5]
uint8_t buildTemperatureGlyphs(float temp, uint8_t *glyphs)
{
  if (temp < 0.0f)
    temp = 0.0f;

  int tempInt = (int)temp;
  int tempDecimal = (int)((temp - tempInt) * 10 + 0.5f);
  if (tempDecimal >= 10)
  {
    tempInt++;
    tempDecimal = 0;
  }

  glyphs[0] = tempInt / 10;
  glyphs[1] = tempInt % 10;
  glyphs[2] = GLYPH_PERIOD;
  glyphs[3] = tempDecimal;
  return 4;
}

// Build integer glyph sequence: 1234 -> [1, 2, 3, 4]
uint8_t buildIntegerGlyphs(int value, uint8_t *glyphs)
{
  if (value < 0)
    value = 0;
  if (value == 0)
  {
    glyphs[0] = 0;
    return 1;
  }

  uint8_t temp[4];
  uint8_t count = 0;
  while (value > 0 && count < 4)
  {
    temp[count++] = value % 10;
    value /= 10;
  }
  // Reverse
  for (uint8_t i = 0; i < count; i++)
    glyphs[i] = temp[count - 1 - i];
  return count;
}

// Build date glyph sequence: 2024.11.25 -> [2,0,2,4, PERIOD, 1,1, PERIOD, 2,5]
uint8_t buildDateGlyphs(uint16_t year, uint8_t month, uint8_t day, uint8_t *glyphs)
{
  glyphs[0] = (year / 1000) % 10;
  glyphs[1] = (year / 100) % 10;
  glyphs[2] = (year / 10) % 10;
  glyphs[3] = year % 10;
  glyphs[4] = GLYPH_PERIOD;
  glyphs[5] = month / 10;
  glyphs[6] = month % 10;
  glyphs[7] = GLYPH_PERIOD;
  glyphs[8] = day / 10;
  glyphs[9] = day % 10;
  return 10;
}

// Build time glyph sequence: 12:34 -> [1,2, COLON, 3,4] or 9:34 -> [9, COLON, 3,4]
uint8_t buildTimeGlyphs(uint8_t hour, uint8_t minute, uint8_t *glyphs)
{
  uint8_t count = 0;
  if (hour >= 10)
    glyphs[count++] = hour / 10;
  glyphs[count++] = hour % 10;
  glyphs[count++] = GLYPH_COLON;
  glyphs[count++] = minute / 10;
  glyphs[count++] = minute % 10;
  return count;
}

// ============================================================
// Drawing Functions (using glyph sequences)
// ============================================================

uint16_t calculateTemperatureWidth(float temp)
{
  uint8_t glyphs[4];
  uint8_t count = buildTemperatureGlyphs(temp, glyphs);
  return calcGlyphSequenceWidth(glyphs, count, FONT_M);
}

uint16_t drawTemperature(float temp, uint16_t x, uint16_t y)
{
  uint8_t glyphs[4];
  uint8_t count = buildTemperatureGlyphs(temp, glyphs);
  LOGD(LogTag::DISPLAY_MGR, "drawTemp: %.1f at x=%d", temp, x);
  return drawGlyphSequence(glyphs, count, x, y, FONT_M);
}

uint16_t calculateIntegerWidth(int value)
{
  uint8_t glyphs[4];
  uint8_t count = buildIntegerGlyphs(value, glyphs);
  return calcGlyphSequenceWidth(glyphs, count, FONT_M);
}

uint16_t drawInteger(int value, uint16_t x, uint16_t y)
{
  uint8_t glyphs[4];
  uint8_t count = buildIntegerGlyphs(value, glyphs);
  LOGD(LogTag::DISPLAY_MGR, "drawInt: %d at x=%d", value, x);
  return drawGlyphSequence(glyphs, count, x, y, FONT_M);
}

uint16_t calculateDateWidth(uint16_t year, uint8_t month, uint8_t day)
{
  uint8_t glyphs[10];
  uint8_t count = buildDateGlyphs(year, month, day, glyphs);
  return calcGlyphSequenceWidth(glyphs, count, FONT_M);
}

void drawDateM(uint16_t year, uint8_t month, uint8_t day, uint16_t x, uint16_t y)
{
  uint8_t glyphs[10];
  uint8_t count = buildDateGlyphs(year, month, day, glyphs);
  LOGD(LogTag::DISPLAY_MGR, "drawDateM: %04d.%02d.%02d at x=%d", year, month, day, x);
  drawGlyphSequence(glyphs, count, x, y, FONT_M);
}

uint16_t calculateTimeWidth(uint8_t hour, uint8_t minute)
{
  uint8_t glyphs[5];
  uint8_t count = buildTimeGlyphs(hour, minute, glyphs);
  return calcGlyphSequenceWidth(glyphs, count, FONT_L);
}

void drawTime(uint8_t hour, uint8_t minute, uint16_t x, uint16_t y)
{
  uint8_t glyphs[5];
  uint8_t count = buildTimeGlyphs(hour, minute, glyphs);
  LOGD(LogTag::DISPLAY_MGR, "drawTime: %02d:%02d at x=%d", hour, minute, x);
  drawGlyphSequence(glyphs, count, x, y, FONT_L);
}

void drawStatus(const NetworkState &networkState, float batteryVoltage)
{
  // Increased buffer size to prevent overflow with long status messages
  // Format can be: "B:3.845V | W:OK(-50) 192.168.1.100 | N:OK | U:123m | H:12345 | Msg:...")
  // Max length: ~110 chars + 64 char message = ~174 chars, using 256 for safety
  char statusLine[256];
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

  // Format: "B:3.845V | WiFi:SSID(RSSI) IP:1.2.3.4 NTP:OK(diff) Heap:12345"
  // Truncate SSID if too long? For now just show basic info

  // Layout:
  // Left: Battery Voltage | WiFi Status + SSID + RSSI + IP
  // Right: NTP Status + Heap + Uptime

  // Let's try a single line with compact info
  // B:3.845V | W:Connected(SSID, -50dBm) 192.168.1.100 | N:OK(123ms) | U:123m | H:12345

  const char *wifiStatus = networkState.wifiConnected ? "OK" : "--";
  const char *ntpStatus = networkState.ntpSynced ? "OK" : "--";

  // Check if there's a status message
  bool hasMessage = (g_statusMessage[0] != '\0');

  if (networkState.wifiConnected && ipStr[0] != '\0')
  {
    if (hasMessage)
    {
      snprintf(statusLine, sizeof(statusLine), "B:%.3fV | W:%s(%ld) %s | N:%s | U:%lum | H:%u | Msg:%s",
               batteryVoltage, wifiStatus, rssi, ipStr, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
    }
    else
    {
      snprintf(statusLine, sizeof(statusLine), "B:%.3fV | W:%s(%ld) %s | N:%s | U:%lum | H:%u",
               batteryVoltage, wifiStatus, rssi, ipStr, ntpStatus, millis() / 60000, freeHeap);
    }
  }
  else
  {
    if (hasMessage)
    {
      snprintf(statusLine, sizeof(statusLine), "B:%.3fV | W:%s | N:%s | U:%lum | H:%u | Msg:%s",
               batteryVoltage, wifiStatus, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
    }
    else
    {
      snprintf(statusLine, sizeof(statusLine), "B:%.3fV | W:%s | N:%s | U:%lum | H:%u",
               batteryVoltage, wifiStatus, ntpStatus, millis() / 60000, freeHeap);
    }
  }

  EPD_ShowString(8, yPos, statusLine, fontSize, BLACK);
}

void drawStatus(const NetworkState &networkState, float batteryVoltage); // Forward declaration

bool performUpdate(const NetworkState &networkState, bool forceUpdate, bool fullUpdate)
{
  struct tm timeinfo;
  bool timeAvailable = false;

  if (!getLocalTime(&timeinfo))
  {
    LOGE(LogTag::DISPLAY_MGR, "Failed to get local time, trying RTC fallback");
    // Try to restore time from RTC as fallback
    if (NetworkManager_SetupTimeFromRTC())
    {
      // Try again after RTC restore
      if (getLocalTime(&timeinfo))
      {
        LOGI(LogTag::DISPLAY_MGR, "Time restored from RTC fallback");
        timeAvailable = true;
      }
      else
      {
        LOGE(LogTag::DISPLAY_MGR, "Failed to get local time even after RTC restore");
      }
    }
    else
    {
      LOGE(LogTag::DISPLAY_MGR, "Failed to get local time and RTC fallback also failed");
    }
  }
  else
  {
    timeAvailable = true;
  }

  // Check if we should skip update (only if time is available and not forced)
  // Note: For full update, we usually want to force update regardless of minute change
  // But the caller can control this via forceUpdate parameter.
  // DisplayManager_FullUpdate calls this with forceUpdate=true.
  if (timeAvailable)
  {
    const uint8_t currentMinute = timeinfo.tm_min;
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    uint8_t &lastDisplayedMinute = rtcState.lastDisplayedMinute;

    if (!forceUpdate && currentMinute == lastDisplayedMinute)
    {
      return false;
    }
  }

  unsigned long startTime = micros();
  Paint_Clear(WHITE);

  // Use battery voltage measured early in setup() (before WiFi/sensor operations)
  // This ensures we measure voltage when battery is in near-idle state (no load)
  float batteryVoltage = g_batteryVoltage;

  // Draw time and date only if time is available
  if (timeAvailable)
  {
    const uint8_t hour = timeinfo.tm_hour;
    const uint8_t currentMinute = timeinfo.tm_min;
    const uint16_t year = timeinfo.tm_year + 1900;
    const uint8_t month = timeinfo.tm_mon + 1;
    const uint8_t day = timeinfo.tm_mday;

    // Calculate centered X for time (Range: 15 to 468, Center: 241)
    uint16_t timeWidth = calculateTimeWidth(hour, currentMinute);
    uint16_t timeX = 241 - (timeWidth / 2);

    drawTime(hour, currentMinute, timeX, kTimeY);

    // Calculate centered X for date (Range: 15 to 468, Center: 241)
    uint16_t dateWidth = calculateDateWidth(year, month, day);
    uint16_t dateX = 241 - (dateWidth / 2);

    drawDateM(year, month, day, dateX, kDateY);

    // Update lastDisplayedMinute for next check
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    rtcState.lastDisplayedMinute = currentMinute;
  }
  else
  {
    // Draw error message instead of time
    const uint16_t fontSize = 12;
    EPD_ShowString(kTimeX, kTimeY, "No Time", fontSize, BLACK);
    EPD_ShowString(kDateX, kDateY, "WiFi Failed", fontSize, BLACK);
  }

  drawStatus(networkState, batteryVoltage);

  // Draw sensor icons and values
  if (SensorManager_IsInitialized())
  {
    // Get sensor values
    float temp = SensorManager_GetTemperature();
    float humidity = SensorManager_GetHumidity();
    uint16_t co2 = SensorManager_GetCO2();

    // Icon positions (fixed)
    constexpr uint16_t kTempIconX = 482;
    constexpr uint16_t kTempIconY = 33;
    constexpr uint16_t kHumidityIconX = 482;
    constexpr uint16_t kHumidityIconY = 114;
    constexpr uint16_t kCO2IconX = 482;
    constexpr uint16_t kCO2IconY = 193;

    // Draw temperature
    drawBitmapCorrect(kTempIconX, kTempIconY, IconTemp_WIDTH, IconTemp_HEIGHT, IconTemp);
    uint16_t tempEndX = drawTemperature(temp, kTempValueX, kTempValueY);
    drawBitmapCorrect(tempEndX + kValueUnitSpacing, kTempValueY + kUnitYOffset, UnitC_WIDTH, UnitC_HEIGHT, UnitC);

    // Draw humidity
    drawBitmapCorrect(kHumidityIconX, kHumidityIconY, IconHumidity_WIDTH, IconHumidity_HEIGHT, IconHumidity);
    uint16_t humidityEndX = drawInteger((int)(humidity + 0.5f), kHumidityValueX, kHumidityValueY);
    drawBitmapCorrect(humidityEndX + kValueUnitSpacing, kHumidityValueY + kUnitYOffset, UnitPercent_WIDTH, UnitPercent_HEIGHT, UnitPercent);

    // Draw CO2
    drawBitmapCorrect(kCO2IconX, kCO2IconY, IconCO2_WIDTH, IconCO2_HEIGHT, IconCO2);
    uint16_t co2EndX = drawInteger(co2, kCO2ValueX, kCO2ValueY);
    drawBitmapCorrect(co2EndX + kValueUnitSpacing, kCO2ValueY + kUnitYOffset, UnitPpm_WIDTH, UnitPpm_HEIGHT, UnitPpm);
  }

  const unsigned long drawDuration = micros() - startTime;

  startTime = micros();
  EPD_Display(ImageBW);
  const unsigned long displayDuration = micros() - startTime;

  startTime = micros();
  if (fullUpdate)
  {
    DisplayManager_SetStatus("Full Updating...");
    EPD_Update();
  }
  else
  {
    DisplayManager_SetStatus("Updating...");
    EPD_PartUpdate();
  }
  const unsigned long updateDuration = micros() - startTime;

  if (timeAvailable)
  {
    LOGI(LogTag::DISPLAY_MGR, "%s: %d:%02d, Battery: %.3fV", fullUpdate ? "Full update" : "Updated", timeinfo.tm_hour, timeinfo.tm_min, batteryVoltage);
  }
  else
  {
    LOGI(LogTag::DISPLAY_MGR, "%s (no time available), Battery: %.3fV", fullUpdate ? "Full update" : "Updated", batteryVoltage);
  }
  LOGD(LogTag::DISPLAY_MGR, "Draw: %lu us, EPD_Display: %lu us, Update: %lu us, Total: %lu us",
       drawDuration, displayDuration, updateDuration, drawDuration + displayDuration + updateDuration);

  // Save frame buffer to RTC memory for next wake up
  DeepSleepManager_SaveFrameBuffer(ImageBW, kFrameBufferSize);

  // Put EPD into deep sleep after update
  EPD_DeepSleep();
  LOGI(LogTag::DISPLAY_MGR, "EPD entered deep sleep");

  return true;
}

} // namespace

// Battery voltage measurement
// Voltage divider is connected to GPIO8
// ESP32-S3 ADC is non-linear, so we use a linear calibration equation instead of ideal divider ratio
// Calibration data (measured):
//   ADC 2166 = 3.712V
//   ADC 2237 = 3.846V
//   ADC 2293 = 4.011V
// Linear fit equation: Vbat = 0.002334 * adc_raw - 1.353
// This compensates for ADC non-linearity and provides ±0.6% accuracy in 3.7-4.1V range
constexpr int BATTERY_ADC_PIN = 8;
constexpr float BATTERY_VOLTAGE_SLOPE = 0.002334f; // Linear calibration slope
constexpr float BATTERY_VOLTAGE_OFFSET = -1.353f;  // Linear calibration offset

// Global variable to store battery voltage (measured early in setup, before WiFi/sensor operations)
// Defined outside namespace so it can be accessed from EPDClock.ino
float g_batteryVoltage = 0.0f;

void DisplayManager_Init(bool wakeFromSleep)
{
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);

  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);

  if (wakeFromSleep)
  {
    // Wake from deep sleep: Use minimal initialization
    LOGI(LogTag::DISPLAY_MGR, "Waking EPD from deep sleep (minimal init)");
    EPD_HW_RESET();
    EPD_FastMode1Init();

    // Try to load previous frame buffer from RTC memory
    if (DeepSleepManager_LoadFrameBuffer(ImageBW, kFrameBufferSize))
    {
      // Success! Send previous data to EPD controller RAM
      // This restores the RAM state so PartUpdate works correctly
      EPD_Display(ImageBW);
      EPD_PartUpdate(); // Optional: ensure display is in sync
      LOGI(LogTag::DISPLAY_MGR, "EPD restored with previous image data");
    }
    else
    {
      // Failed to load (first boot or corruption)
      // Clear screen to be safe
      LOGW(LogTag::DISPLAY_MGR, "Failed to load previous image, clearing screen");
      Paint_Clear(WHITE);
      EPD_Display_Clear();
      EPD_Update();
      EPD_PartUpdate();
    }
  }
  else
  {
    // Cold boot: Full initialization with screen clear
    LOGI(LogTag::DISPLAY_MGR, "Cold boot - full initialization");
    Paint_Clear(WHITE);
    EPD_FastMode1Init();
    EPD_Display_Clear();
    EPD_Update();
    EPD_PartUpdate();
  }

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
  return performUpdate(networkState, forceUpdate, false);
}

void DisplayManager_FullUpdate(const NetworkState &networkState)
{
  performUpdate(networkState, true, true);
}

uint8_t *DisplayManager_GetFrameBuffer()
{
  return ImageBW;
}

float DisplayManager_ReadBatteryVoltage()
{
  // Read battery voltage from ADC
  // Should be called early in setup() before WiFi/sensor operations
  // For 470kΩ voltage divider: first do dummy read, then read 16 times with 50µs delay
  // Use linear calibration equation to compensate for ESP32-S3 ADC non-linearity
  // Vbat = 0.002334 * adc_raw - 1.353
  //
  // Note: After deep sleep, ADC needs to stabilize. We do a dummy read first,
  // then read 16 times with small delays to get accurate average.
  // This ensures we measure voltage when battery is in near-idle state (no load).

  constexpr int NUM_SAMPLES = 16; // 8-16 times recommended for 470kΩ divider

  // Small delay after wake from deep sleep to let ADC stabilize
  delay(10); // 10ms delay for ADC to stabilize after deep sleep

  analogRead(BATTERY_ADC_PIN); // Dummy read to stabilize ADC
  long adcSum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++)
  {
    adcSum += analogRead(BATTERY_ADC_PIN);
    delayMicroseconds(50); // 50µs delay between readings for 470kΩ divider
  }
  int rawAdc = adcSum / NUM_SAMPLES;
  float batteryVoltage = BATTERY_VOLTAGE_SLOPE * rawAdc + BATTERY_VOLTAGE_OFFSET;

  LOGI(LogTag::DISPLAY_MGR, "Battery voltage measured: %.3fV (ADC: %d)", batteryVoltage, rawAdc);

  return batteryVoltage;
}
