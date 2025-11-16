#include "EPD.h"         // Includes library files for electronic paper screens
#include "bitmaps/Number_S_bitmap.h" // Number S font (for date display)
#include "bitmaps/Number_L_bitmap.h" // Number L font (for clock display)
#include <pgmspace.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "wifi_config.h" // Wi-Fi認証情報（gitignoreに含まれています）
#include "server_config.h" // Python server configuration

// SDC41 Sensor includes
#include <Wire.h>
#include <SensirionI2cScd4x.h>

// Define an array to store image data, with a size of 27200 bytes
uint8_t ImageBW[27200];

// SDC41 Sensor configuration
#define I2C_SDA_PIN 38
#define I2C_SCL_PIN 21
#define SCD4X_I2C_ADDRESS 0x62    // Default I2C address for SCD4x
#define SENSOR_READ_INTERVAL 5000 // Read sensor every 5 seconds

// SDC41 Sensor object
SensirionI2cScd4x scd4x;

// Sensor data variables
uint16_t co2 = 0;
float temperature = 0.0;
float humidity = 0.0;
unsigned long lastSensorRead = 0;
bool sensorInitialized = false;

// Variables for time tracking
uint8_t lastDisplayedMinute = 255; // Initialize to invalid value
uint8_t lastDisplayedDay = 255;    // Initialize to invalid value
unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000; // Re-sync every hour (in milliseconds)

// State variables for display
bool wifiConnected = false;
bool ntpSynced = false;
unsigned long wifiConnectTime = 0;
unsigned long ntpSyncTime = 0;
unsigned long lastUpdateTime = 0;

// ImageBW export settings
#if ENABLE_IMAGEBW_EXPORT
bool enableImageBWExport = true;
#else
bool enableImageBWExport = false;
#endif
unsigned long lastImageBWExport = 0;
const unsigned long IMAGEBW_EXPORT_INTERVAL = 60000; // Export every 60 seconds (0 = disable auto export)

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

// Array of number bitmap pointers (Number S font - for date display)
const uint8_t* NumberBitmaps[] = {
  Number0, Number1, Number2, Number3, Number4,
  Number5, Number6, Number7, Number8, Number9
};

// Array of number widths (Number S font)
const uint16_t NumberWidths[] = {
    Number0_WIDTH, Number1_WIDTH, Number2_WIDTH, Number3_WIDTH, Number4_WIDTH,
    Number5_WIDTH, Number6_WIDTH, Number7_WIDTH, Number8_WIDTH, Number9_WIDTH};

// Array of number bitmap pointers (Number L font - for clock display)
const uint8_t *NumberLBitmaps[] = {
    NumberL0, NumberL1, NumberL2, NumberL3, NumberL4,
    NumberL5, NumberL6, NumberL7, NumberL8, NumberL9};

// Draw a single digit using Number S font (for date display)
void drawDigit(uint8_t digit, uint16_t x, uint16_t y) {
  if (digit > 9) return;  // Invalid digit
  drawBitmapCorrect(x, y, NumberWidths[digit], Number0_HEIGHT, NumberBitmaps[digit], WHITE);
}

// Draw a single digit using Number L font (for clock display)
void drawDigitL(uint8_t digit, uint16_t x, uint16_t y)
{
  if (digit > 9)
    return; // Invalid digit
  drawBitmapCorrect(x, y, NumberL0_WIDTH, NumberL0_HEIGHT, NumberLBitmaps[digit], WHITE);
}

// Get width of a digit (Number S font)
uint16_t getDigitWidth(uint8_t digit)
{
  if (digit > 9)
    return 0;
  return NumberWidths[digit];
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

// Draw colon using bitmap image (for Number L font)
void drawColon(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberColon_WIDTH, NumberColon_HEIGHT, NumberColon, WHITE);
}

// Draw period using bitmap image (for Number S font)
void drawPeriod(uint16_t x, uint16_t y)
{
  drawBitmapCorrect(x, y, NumberPeriod_WIDTH, NumberPeriod_HEIGHT, NumberPeriod, WHITE);
}

// Draw date in format "YYYY.MM.DD" using Number S font bitmaps (6px spacing)
void drawDate(uint16_t year, uint8_t month, uint8_t day, uint16_t x, uint16_t y)
{
  const uint16_t CHAR_SPACING = 6; // 6px spacing between characters
  uint16_t currentX = x;

  // Draw year (4 digits)
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

  // Draw period
  drawPeriod(currentX, y);
  currentX += NumberPeriod_WIDTH + CHAR_SPACING;

  // Draw month (2 digits)
  digit = month / 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = month % 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  // Draw period
  drawPeriod(currentX, y);
  currentX += NumberPeriod_WIDTH + CHAR_SPACING;

  // Draw day (2 digits)
  digit = day / 10;
  drawDigit(digit, currentX, y);
  currentX += getDigitWidth(digit) + CHAR_SPACING;

  digit = day % 10;
  drawDigit(digit, currentX, y);
}

// Draw time in format "HH:MM" using Number L font bitmaps (no spacing)
void drawTime(uint8_t hour, uint8_t minute, uint16_t x, uint16_t y)
{
  uint16_t currentX = x;

  // Draw hour (2 digits)
  uint8_t digit = hour / 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  digit = hour % 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  // Draw colon with 6px spacing before and after
  currentX += 6; // 6px spacing before colon
  drawColon(currentX, y);
  currentX += NumberColon_WIDTH + 6; // 6px spacing after colon

  // Draw minute (2 digits)
  digit = minute / 10;
  drawDigitL(digit, currentX, y);
  currentX += NumberL0_WIDTH;

  digit = minute % 10;
  drawDigitL(digit, currentX, y);
}

// Draw setup status message
void drawSetupStatus(const char *message)
{
  // Clear status area (bottom 60 pixels)
  EPD_ClearWindows(0, EPD_H - 20, EPD_W, EPD_H, WHITE);

  uint16_t fontSize = 16;
  int yPos = EPD_H - 18; // Position from bottom

  EPD_ShowString(8, yPos, message, fontSize, BLACK);

  // Update display immediately
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Draw internal state at the top of the screen
void drawStatus()
{
  char statusLine[80];
  int yPos = 2; // Position from top
  uint16_t fontSize = 16;

  // Wi-Fi status
  if (wifiConnected)
  {
    snprintf(statusLine, sizeof(statusLine), "WiFi:OK");
  }
  else
  {
    snprintf(statusLine, sizeof(statusLine), "WiFi:--");
  }
  EPD_ShowString(8, yPos, statusLine, fontSize, BLACK);

  // NTP status
  if (ntpSynced)
  {
    snprintf(statusLine, sizeof(statusLine), "NTP:OK");
  }
  else
  {
    snprintf(statusLine, sizeof(statusLine), "NTP:--");
  }
  EPD_ShowString(120, yPos, statusLine, fontSize, BLACK);

  // Show timing info (if available)
  if (wifiConnectTime > 0)
  {
    snprintf(statusLine, sizeof(statusLine), "W:%lums", wifiConnectTime);
    EPD_ShowString(200, yPos, statusLine, fontSize, BLACK);
  }

  if (ntpSyncTime > 0)
  {
    snprintf(statusLine, sizeof(statusLine), "N:%lums", ntpSyncTime);
    EPD_ShowString(300, yPos, statusLine, fontSize, BLACK);
  }

  // Show uptime in minutes
  unsigned long uptimeMinutes = millis() / 60000;
  snprintf(statusLine, sizeof(statusLine), "U:%lum", uptimeMinutes);
  EPD_ShowString(400, yPos, statusLine, fontSize, BLACK);
}

// Connect to Wi-Fi
bool connectWiFi()
{
  Serial.begin(115200);
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  drawSetupStatus("Connecting WiFi...");

  unsigned long startTime = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;

    // Update status every 2 seconds
    if (attempts % 4 == 0)
    {
      char statusMsg[40];
      snprintf(statusMsg, sizeof(statusMsg), "WiFi connecting... %d", attempts);
      drawSetupStatus(statusMsg);
    }
  }
  Serial.println();

  unsigned long connectionTime = millis() - startTime;

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    wifiConnectTime = connectionTime;
    Serial.print("Wi-Fi connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[TIMING] Wi-Fi connection time: ");
    Serial.print(connectionTime);
    Serial.println(" ms");

    char statusMsg[40];
    snprintf(statusMsg, sizeof(statusMsg), "WiFi OK! (%lums)", connectionTime);
    drawSetupStatus(statusMsg);
    delay(500);
    return true;
  }
  else
  {
    wifiConnected = false;
    wifiConnectTime = 0;
    Serial.println("Wi-Fi connection failed!");
    Serial.print("[TIMING] Wi-Fi connection attempt time: ");
    Serial.print(connectionTime);
    Serial.println(" ms");

    drawSetupStatus("WiFi FAILED!");
    delay(1000);
    return false;
  }
}

// Sync time with NTP server
bool syncNTP()
{
  const char *ntpServer = "ntp.nict.jp";
  const long gmtOffset_sec = 9 * 3600; // JST (UTC+9)
  const int daylightOffset_sec = 0;    // No daylight saving time in Japan

  drawSetupStatus("Syncing NTP...");

  unsigned long startTime = millis();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.print("Waiting for NTP time sync");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10)
  {
    Serial.print(".");
    delay(1000);
    attempts++;

    // Update status every second
    char statusMsg[40];
    snprintf(statusMsg, sizeof(statusMsg), "NTP syncing... %d", attempts);
    drawSetupStatus(statusMsg);
  }
  Serial.println();

  unsigned long syncTime = millis() - startTime;

  if (attempts < 10)
  {
    ntpSynced = true;
    ntpSyncTime = syncTime;
    Serial.println("Time synchronized!");
    Serial.print("Current time: ");
    Serial.print(timeinfo.tm_hour);
    Serial.print(":");
    Serial.println(timeinfo.tm_min);
    Serial.print("[TIMING] NTP sync time: ");
    Serial.print(syncTime);
    Serial.println(" ms");

    char statusMsg[40];
    snprintf(statusMsg, sizeof(statusMsg), "NTP OK! (%lums)", syncTime);
    drawSetupStatus(statusMsg);
    delay(500);
    return true;
  }
  else
  {
    ntpSynced = false;
    ntpSyncTime = 0;
    Serial.println("NTP time sync failed!");
    Serial.print("[TIMING] NTP sync attempt time: ");
    Serial.print(syncTime);
    Serial.println(" ms");

    drawSetupStatus("NTP FAILED!");
    delay(1000);
    return false;
  }
}

// Update display with current time
void updateDisplay()
{
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    uint8_t currentMinute = timeinfo.tm_min;

    // Only update if minute has changed
    if (currentMinute != lastDisplayedMinute)
    {
      uint8_t hour = timeinfo.tm_hour;

      unsigned long startTime = micros();

      // Clear the display
      Paint_Clear(WHITE);

      // Draw current time using custom bitmaps at (330, 53)
      // Clock width: ~433px (4 digits × 100px + colon 33px, no spacing)
      // Screen width: 800px, so max start position: 800 - 433 = 367px
      // Using 330px (moved 30px right from 300px)
      drawTime(hour, currentMinute, 330, 53);

      // Draw current date using Number S font at (15, 125)
      uint16_t year = timeinfo.tm_year + 1900;
      uint8_t month = timeinfo.tm_mon + 1;
      uint8_t day = timeinfo.tm_mday;

      // Update date (always redraw when time updates)
      drawDate(year, month, day, 15, 125);
      lastDisplayedDay = day;

      // Draw status at the top
      drawStatus();

      // Update the display using partial update (faster and lower power consumption)
      unsigned long drawTime = micros() - startTime;

      startTime = micros();
      EPD_Display(ImageBW);
      unsigned long displayTime = micros() - startTime;

      startTime = micros();
      EPD_PartUpdate();
      unsigned long updateTime = micros() - startTime;

      lastDisplayedMinute = currentMinute;
      lastUpdateTime = millis();

      Serial.print("Display updated: ");
      Serial.print(hour);
      Serial.print(":");
      Serial.println(currentMinute);
      Serial.print("[TIMING] Draw time: ");
      Serial.print(drawTime);
      Serial.print(" us, EPD_Display: ");
      Serial.print(displayTime);
      Serial.print(" us, EPD_PartUpdate: ");
      Serial.print(updateTime);
      Serial.print(" us, Total: ");
      Serial.print(drawTime + displayTime + updateTime);
      Serial.println(" us");

      // Send ImageBW to server after display update
      if (enableImageBWExport && WiFi.status() == WL_CONNECTED)
      {
        sendImageBWToServer();
        lastImageBWExport = millis();
      }
    }
  }
}

// Send ImageBW to Python server
bool sendImageBWToServer()
{
  if (!wifiConnected || WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[ImageBW] WiFi not connected, skipping export");
    return false;
  }

  if (!enableImageBWExport)
  {
    return false;
  }

  HTTPClient http;
  String url = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + "/imagebw";

  Serial.print("[ImageBW] Sending to server: ");
  Serial.println(url);

  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Content-Length", String(27200));

  unsigned long startTime = millis();
  int httpResponseCode = http.POST((uint8_t *)ImageBW, 27200);
  unsigned long sendTime = millis() - startTime;

  bool success = false;

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.print("[ImageBW] Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("[ImageBW] Response: ");
    Serial.println(response);
    Serial.print("[ImageBW] Send time: ");
    Serial.print(sendTime);
    Serial.println(" ms");

    if (httpResponseCode == 200)
    {
      success = true;
    }
  }
  else
  {
    Serial.print("[ImageBW] Error: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return success;
}

// Re-sync with NTP periodically to maintain accuracy
void checkNtpResync()
{
  unsigned long currentTime = millis();

  // Update Wi-Fi connection status
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // Check if it's time to re-sync (every hour)
  if (currentTime - lastNtpSync >= NTP_SYNC_INTERVAL)
  {
    Serial.println("Re-syncing with NTP server...");

    // Reconnect Wi-Fi if disconnected
    if (WiFi.status() != WL_CONNECTED)
    {
      connectWiFi();
    }

    // Sync with NTP
    if (WiFi.status() == WL_CONNECTED)
    {
      if (syncNTP())
      {
        lastNtpSync = currentTime;
        Serial.println("NTP re-sync successful!");
        // Update display to show new status
        updateDisplay();
      }
    }
  }
}

// SDC41 Sensor functions
bool initSDC41()
{
  Serial.println("Initializing SDC41 sensor...");

  uint16_t error;
  char errorMessage[256];

  // Initialize I2C with custom pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // Initialize sensor with I2C address
  scd4x.begin(Wire, SCD4X_I2C_ADDRESS);

  // Stop any previous measurement
  error = scd4x.stopPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return false;
  }

  delay(1000);

  // Start periodic measurement
  error = scd4x.startPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return false;
  }

  Serial.println("SDC41 sensor initialized successfully!");
  Serial.println("Waiting for first measurement (5 seconds)...");
  delay(5000);

  return true;
}

void readSDC41()
{
  if (!sensorInitialized)
  {
    return;
  }

  uint16_t error;
  char errorMessage[256];
  bool isDataReady = false;

  // Check if data is ready with timeout protection
  unsigned long startTime = millis();
  error = scd4x.getDataReadyStatus(isDataReady);
  if (error)
  {
    Serial.print("[Sensor] Error getDataReadyStatus: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    // Don't disable sensor on error, just skip this reading
    return;
  }

  if (millis() - startTime > 100)
  {
    Serial.println("[Sensor] getDataReadyStatus timeout");
    return;
  }

  if (!isDataReady)
  {
    // Data not ready is normal, don't log it
    return;
  }

  // Read measurement with timeout protection
  startTime = millis();
  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("[Sensor] Error readMeasurement: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    // Don't disable sensor on error, just skip this reading
    return;
  }

  if (millis() - startTime > 200)
  {
    Serial.println("[Sensor] readMeasurement timeout");
    return;
  }

  // Print sensor values to serial (optional, for debugging)
  Serial.print("[Sensor] CO2: ");
  Serial.print(co2);
  Serial.print(" ppm, T: ");
  Serial.print(temperature);
  Serial.print(" °C, H: ");
  Serial.print(humidity);
  Serial.println(" %RH");
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== EPD Clock with SDC41 Sensor ===");

  // Set the screen power control pin.
  pinMode(7, OUTPUT);  // Set pin 7 as output mode.
  digitalWrite(7, HIGH);  // Set pin 7 to high level to turn on the screen power.

  // Initialize random seed (use analog pin noise or timer)
  randomSeed(analogRead(0));

  // Initialize the EPD (electronic paper display) screen.
  EPD_GPIOInit();  // Initialize the GPIO pins of the EPD.
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);  // Create a new canvas with size EPD_W x EPD_H and background color white.
  Paint_Clear(WHITE);  // Clear the canvas with background color white.

  // Initialize EPD hardware (must be done before any display operations)
  EPD_FastMode1Init();  // Initialize the fast mode 1 of the EPD.
  EPD_Display_Clear();  // Clear the EPD display content.
  EPD_Update();         // Complete update
  EPD_PartUpdate(); // This is super important! It's required for partial update to work properly

  delay(500); // Wait for display to stabilize

  // Now EPD is initialized, we can display status
  drawSetupStatus("EPD Ready!");

  // Initialize SDC41 sensor
  if (initSDC41())
  {
    sensorInitialized = true;
    Serial.println("SDC41 sensor initialized successfully!");
  }
  else
  {
    sensorInitialized = false;
    Serial.println("Warning: SDC41 sensor initialization failed!");
    Serial.println("Please check connections:");
    Serial.println("  SDA -> GPIO 38");
    Serial.println("  SCL -> GPIO 21");
    Serial.println("  VDD -> 3.3V");
    Serial.println("  GND -> GND");
    // Continue without sensor - don't halt
  }

  // Connect to Wi-Fi and sync with NTP
  drawSetupStatus("Connecting WiFi...");
  if (connectWiFi())
  {
    syncNTP();
    lastNtpSync = millis();
    // Wi-Fi can be disconnected after NTP sync to save power
    // WiFi.disconnect(true); // Uncomment to disconnect Wi-Fi after sync
  }
  else
  {
    wifiConnected = false;
    ntpSynced = false;
  }

  // Final display update
  drawSetupStatus("Starting...");
  updateDisplay();
}

void loop()
{
  // Check if we need to re-sync with NTP
  checkNtpResync();

  // Read sensor when minute changes (same timing as clock update)
  // Temporarily disabled to debug reboot issue
  /*
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    uint8_t currentMinute = timeinfo.tm_min;
    static uint8_t lastSensorMinute = 255;

    if (currentMinute != lastSensorMinute)
    {
      if (sensorInitialized)
      {
        readSDC41();
      }
      lastSensorMinute = currentMinute;
    }
  }
  */

  // Update display if minute has changed
  updateDisplay();

  // Periodically send ImageBW to server (if auto export is enabled)
  unsigned long currentTime = millis();
  if (enableImageBWExport && IMAGEBW_EXPORT_INTERVAL > 0 && WiFi.status() == WL_CONNECTED)
  {
    if (currentTime - lastImageBWExport >= IMAGEBW_EXPORT_INTERVAL)
    {
      sendImageBWToServer();
      lastImageBWExport = currentTime;
    }
  }

  // Small delay to avoid excessive CPU usage
  delay(1000); // Check every second
}
