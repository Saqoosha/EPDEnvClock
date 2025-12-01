#include "display_manager.h"

#include <WiFi.h>
#include <Wire.h>

#include "EPD.h"
#include "EPD_Init.h"
#include "font_renderer.h"
#include "bitmaps/Icon_bitmap.h"
#include "bitmaps/Kerning_table.h"
#include "sensor_manager.h"
#include "deep_sleep_manager.h"
#include "logger.h"
#include "network_manager.h"
#include "fuel_gauge_manager.h"

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
// No zero padding for month/day: 2025.12.1 or 2026.1.1
uint8_t buildDateGlyphs(uint16_t year, uint8_t month, uint8_t day, uint8_t *glyphs)
{
  uint8_t count = 0;
  glyphs[count++] = (year / 1000) % 10;
  glyphs[count++] = (year / 100) % 10;
  glyphs[count++] = (year / 10) % 10;
  glyphs[count++] = year % 10;
  glyphs[count++] = GLYPH_PERIOD;
  if (month >= 10)
    glyphs[count++] = month / 10;
  glyphs[count++] = month % 10;
  glyphs[count++] = GLYPH_PERIOD;
  if (day >= 10)
    glyphs[count++] = day / 10;
  glyphs[count++] = day % 10;
  return count;
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

void drawStatus(const NetworkState &networkState, float batteryVoltage, float batteryPercent)
{
  // Increased buffer size to prevent overflow with long status messages
  // Format can be: "B:85%(3.85V) | W:OK(-50) 192.168.1.100 | N:OK | U:123m | H:12345 | Msg:...")
  // Max length: ~120 chars + 64 char message = ~184 chars, using 256 for safety
  char statusLine[256];
  char ipStr[16];
  char batteryStr[24];
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

  // Format battery string: "85%(3.85V)[CHG]" if fuel gauge available, else "3.845V"
  // Add [CHG] indicator if charging
  const char* chrgIndicator = g_batteryCharging ? "[CHG]" : "";
  if (FuelGauge_IsAvailable())
  {
    snprintf(batteryStr, sizeof(batteryStr), "%.0f%%(%.2fV)%s", batteryPercent, batteryVoltage, chrgIndicator);
  }
  else
  {
    snprintf(batteryStr, sizeof(batteryStr), "%.3fV%s", batteryVoltage, chrgIndicator);
  }

  const char *wifiStatus = networkState.wifiConnected ? "OK" : "--";
  const char *ntpStatus = networkState.ntpSynced ? "OK" : "--";

  // Check if there's a status message
  bool hasMessage = (g_statusMessage[0] != '\0');

  if (networkState.wifiConnected && ipStr[0] != '\0')
  {
    if (hasMessage)
    {
      snprintf(statusLine, sizeof(statusLine), "B:%s | W:%s(%ld) %s | N:%s | U:%lum | H:%u | Msg:%s",
               batteryStr, wifiStatus, rssi, ipStr, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
    }
    else
    {
      snprintf(statusLine, sizeof(statusLine), "B:%s | W:%s(%ld) %s | N:%s | U:%lum | H:%u",
               batteryStr, wifiStatus, rssi, ipStr, ntpStatus, millis() / 60000, freeHeap);
    }
  }
  else
  {
    if (hasMessage)
    {
      snprintf(statusLine, sizeof(statusLine), "B:%s | W:%s | N:%s | U:%lum | H:%u | Msg:%s",
               batteryStr, wifiStatus, ntpStatus, millis() / 60000, freeHeap, g_statusMessage);
    }
    else
    {
      snprintf(statusLine, sizeof(statusLine), "B:%s | W:%s | N:%s | U:%lum | H:%u",
               batteryStr, wifiStatus, ntpStatus, millis() / 60000, freeHeap);
    }
  }

  EPD_ShowString(8, yPos, statusLine, fontSize, BLACK);
}

void drawStatus(const NetworkState &networkState, float batteryVoltage, float batteryPercent); // Forward declaration

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

  drawStatus(networkState, batteryVoltage, g_batteryPercent);

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

  // Put EPD into deep sleep FIRST - secure the display before any other operations
  // EPD is sensitive and should not be disturbed by SD card or other I/O operations
  EPD_DeepSleep();
  LOGI(LogTag::DISPLAY_MGR, "EPD entered deep sleep");

  // Save frame buffer to SD card AFTER EPD is safely in deep sleep
  DeepSleepManager_SaveFrameBuffer(ImageBW, kFrameBufferSize);

  return true;
}

} // namespace

// Battery measurement using MAX17048 fuel gauge (with ADC fallback)
// MAX17048 provides accurate state-of-charge estimation

// ADC fallback constants (voltage divider on GPIO8)
// Linear fit equation: Vbat = 0.002334 * adc_raw - 1.353
constexpr int BATTERY_ADC_PIN = 8;
constexpr float BATTERY_VOLTAGE_SLOPE = 0.002334f;
constexpr float BATTERY_VOLTAGE_OFFSET = -1.353f;

// Global variables for battery state
float g_batteryVoltage = 0.0f;
float g_batteryPercent = 0.0f;
float g_batteryChargeRate = 0.0f;
bool g_batteryCharging = false;
static bool s_fuelGaugeInitialized = false;

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

bool DisplayManager_UpdateTimeOnly(const NetworkState &networkState, bool forceUpdate)
{
  struct tm timeinfo;
  bool timeAvailable = false;

  if (!getLocalTime(&timeinfo))
  {
    LOGE(LogTag::DISPLAY_MGR, "Failed to get local time, trying RTC fallback");
    if (NetworkManager_SetupTimeFromRTC())
    {
      if (getLocalTime(&timeinfo))
      {
        LOGI(LogTag::DISPLAY_MGR, "Time restored from RTC fallback");
        timeAvailable = true;
      }
    }
  }
  else
  {
    timeAvailable = true;
  }

  // Check if we should skip update
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

  // Don't clear entire buffer - keep previous sensor values from restored frame buffer
  // Only clear the time/date/status areas (left side of screen)
  // Status bar: y=0-20, full width
  // Time/Date area: x=0-480, y=20-272 (left side only, sensor icons start at x=482)
  // EPD_ClearWindows(0, 0, kScreenWidth, 20, WHITE);  // Status bar
  EPD_ClearWindows(0, 20, 480, kScreenHeight, WHITE);  // Time/date area only

  float batteryVoltage = g_batteryVoltage;

  // Draw time and date only (sensor values remain from previous frame buffer)
  if (timeAvailable)
  {
    const uint8_t hour = timeinfo.tm_hour;
    const uint8_t currentMinute = timeinfo.tm_min;
    const uint16_t year = timeinfo.tm_year + 1900;
    const uint8_t month = timeinfo.tm_mon + 1;
    const uint8_t day = timeinfo.tm_mday;

    uint16_t timeWidth = calculateTimeWidth(hour, currentMinute);
    uint16_t timeX = 241 - (timeWidth / 2);
    drawTime(hour, currentMinute, timeX, kTimeY);

    uint16_t dateWidth = calculateDateWidth(year, month, day);
    uint16_t dateX = 241 - (dateWidth / 2);
    drawDateM(year, month, day, dateX, kDateY);

    RTCState &rtcState = DeepSleepManager_GetRTCState();
    rtcState.lastDisplayedMinute = currentMinute;
  }
  else
  {
    const uint16_t fontSize = 12;
    EPD_ShowString(kTimeX, kTimeY, "No Time", fontSize, BLACK);
    EPD_ShowString(kDateX, kDateY, "WiFi Failed", fontSize, BLACK);
  }

  // Status line will be updated shortly after by DrawSetupStatus("Initializing Sensor...")
  // or by UpdateSensorOnly(), so we skip it here to avoid unnecessary flash.
  // drawStatus(networkState, batteryVoltage);

  const unsigned long drawDuration = micros() - startTime;

  startTime = micros();
  EPD_Display(ImageBW);
  const unsigned long displayDuration = micros() - startTime;

  startTime = micros();
  DisplayManager_SetStatus("Time Update...");
  EPD_PartUpdate();
  const unsigned long updateDuration = micros() - startTime;

  LOGI(LogTag::DISPLAY_MGR, "Time only update done");
  LOGD(LogTag::DISPLAY_MGR, "Draw: %lu us, EPD_Display: %lu us, Update: %lu us",
       drawDuration, displayDuration, updateDuration);

  // Note: Do NOT call EPD_DeepSleep() or save frame buffer here
  // We will add sensor values in the next phase

  return true;
}

void DisplayManager_UpdateSensorOnly(const NetworkState &networkState)
{
  unsigned long startTime = micros();

  // Clear sensor area before drawing new values (right side of screen)
  // This prevents old values from showing through when digits change
  EPD_ClearWindows(480, 20, kScreenWidth, kScreenHeight, WHITE);

  // Draw sensor icons and values (frame buffer already has time/date from previous phase)
  if (SensorManager_IsInitialized())
  {
    float temp = SensorManager_GetTemperature();
    float humidity = SensorManager_GetHumidity();
    uint16_t co2 = SensorManager_GetCO2();

    constexpr uint16_t kTempIconX = 482;
    constexpr uint16_t kTempIconY = 33;
    constexpr uint16_t kHumidityIconX = 482;
    constexpr uint16_t kHumidityIconY = 114;
    constexpr uint16_t kCO2IconX = 482;
    constexpr uint16_t kCO2IconY = 193;

    drawBitmapCorrect(kTempIconX, kTempIconY, IconTemp_WIDTH, IconTemp_HEIGHT, IconTemp);
    uint16_t tempEndX = drawTemperature(temp, kTempValueX, kTempValueY);
    drawBitmapCorrect(tempEndX + kValueUnitSpacing, kTempValueY + kUnitYOffset, UnitC_WIDTH, UnitC_HEIGHT, UnitC);

    drawBitmapCorrect(kHumidityIconX, kHumidityIconY, IconHumidity_WIDTH, IconHumidity_HEIGHT, IconHumidity);
    uint16_t humidityEndX = drawInteger((int)(humidity + 0.5f), kHumidityValueX, kHumidityValueY);
    drawBitmapCorrect(humidityEndX + kValueUnitSpacing, kHumidityValueY + kUnitYOffset, UnitPercent_WIDTH, UnitPercent_HEIGHT, UnitPercent);

    drawBitmapCorrect(kCO2IconX, kCO2IconY, IconCO2_WIDTH, IconCO2_HEIGHT, IconCO2);
    uint16_t co2EndX = drawInteger(co2, kCO2ValueX, kCO2ValueY);
    drawBitmapCorrect(co2EndX + kValueUnitSpacing, kCO2ValueY + kUnitYOffset, UnitPpm_WIDTH, UnitPpm_HEIGHT, UnitPpm);
  }

  // Redraw status line to clear "Initializing Sensor..." message
  drawStatus(networkState, g_batteryVoltage, g_batteryPercent);

  const unsigned long drawDuration = micros() - startTime;

  startTime = micros();
  EPD_Display(ImageBW);
  const unsigned long displayDuration = micros() - startTime;

  startTime = micros();
  DisplayManager_SetStatus("Sensor Update...");
  EPD_PartUpdate();
  const unsigned long updateDuration = micros() - startTime;

  LOGI(LogTag::DISPLAY_MGR, "Sensor values update done");
  LOGD(LogTag::DISPLAY_MGR, "Draw: %lu us, EPD_Display: %lu us, Update: %lu us",
       drawDuration, displayDuration, updateDuration);

  // Now put EPD into deep sleep and save frame buffer
  EPD_DeepSleep();
  LOGI(LogTag::DISPLAY_MGR, "EPD entered deep sleep");

  DeepSleepManager_SaveFrameBuffer(ImageBW, kFrameBufferSize);
}

uint8_t *DisplayManager_GetFrameBuffer()
{
  return ImageBW;
}

float DisplayManager_ReadBatteryVoltage()
{
  // === CRITICAL: Read CHRG pin FIRST, before any I2C operations ===
  // I2C communication can cause noise on GPIO pins, so we read CHRG
  // before initializing fuel gauge (which uses Wire1 I2C)
  Charging_Init();
  g_batteryCharging = Charging_IsCharging();

  // Try to use MAX17048 fuel gauge first (uses Wire1 on GPIO 14/16)
  if (!s_fuelGaugeInitialized)
  {
    if (FuelGauge_Init())
    {
      s_fuelGaugeInitialized = true;
      LOGI(LogTag::DISPLAY_MGR, "MAX17048 fuel gauge initialized on Wire1");
    }
    else
    {
      LOGW(LogTag::DISPLAY_MGR, "MAX17048 not found, falling back to ADC");
    }
  }

  if (FuelGauge_IsAvailable())
  {
    // Read from MAX17048
    float voltage = FuelGauge_GetVoltage();
    float percent = FuelGauge_GetPercent();
    float chargeRate = FuelGauge_GetChargeRate();

    g_batteryVoltage = voltage;
    g_batteryPercent = percent;
    g_batteryChargeRate = chargeRate;

    LOGI(LogTag::DISPLAY_MGR, "Battery: %.3fV, %.1f%%, Rate: %.2f%%/hr",
         voltage, percent, chargeRate);

    return voltage;
  }

  // Fallback to ADC measurement
  constexpr int NUM_SAMPLES = 16;

  delay(10);
  analogRead(BATTERY_ADC_PIN);
  long adcSum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++)
  {
    adcSum += analogRead(BATTERY_ADC_PIN);
    delayMicroseconds(50);
  }
  int rawAdc = adcSum / NUM_SAMPLES;
  float batteryVoltage = BATTERY_VOLTAGE_SLOPE * rawAdc + BATTERY_VOLTAGE_OFFSET;

  g_batteryVoltage = batteryVoltage;
  g_batteryPercent = 0.0f; // Unknown without fuel gauge
  g_batteryChargeRate = 0.0f; // Unknown without fuel gauge

  LOGI(LogTag::DISPLAY_MGR, "Battery (ADC fallback): %.3fV (raw: %d)", batteryVoltage, rawAdc);

  return batteryVoltage;
}

float DisplayManager_GetBatteryPercent()
{
  return g_batteryPercent;
}

float DisplayManager_GetBatteryChargeRate()
{
  return g_batteryChargeRate;
}

bool DisplayManager_IsFuelGaugeAvailable()
{
  return FuelGauge_IsAvailable();
}
