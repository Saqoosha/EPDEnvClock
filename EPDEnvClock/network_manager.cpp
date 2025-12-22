#include "network_manager.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <esp_timer.h>

#include "wifi_config.h"
#include "server_config.h"
#include "logger.h"
#include "deep_sleep_manager.h"
#include <HTTPClient.h>

namespace
{
void updateStatus(StatusCallback callback, const char *message)
{
  if (callback != nullptr)
  {
    callback(message);
  }
}
} // namespace

bool NetworkManager_ConnectWiFi(NetworkState &state, StatusCallback statusCallback)
{
  LOGI(LogTag::NETWORK, "Connecting to Wi-Fi: %s", WIFI_SSID);

  updateStatus(statusCallback, "Connecting WiFi...");

  const unsigned long startTime = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    LOGD(LogTag::NETWORK, ".");
    attempts++;

    if (attempts % 4 == 0)
    {
      char statusMsg[48];
      snprintf(statusMsg, sizeof(statusMsg), "WiFi connecting... %d", attempts);
      updateStatus(statusCallback, statusMsg);
    }
  }

  const unsigned long connectionTime = millis() - startTime;

  if (WiFi.status() == WL_CONNECTED)
  {
    state.wifiConnected = true;
    state.wifiConnectTime = connectionTime;
    LOGI(LogTag::NETWORK, "Wi-Fi connected! IP address: %s", WiFi.localIP().toString().c_str());
    LOGD(LogTag::NETWORK, "Wi-Fi connection time: %lu ms", connectionTime);

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "WiFi OK! (%lums)", connectionTime);
    updateStatus(statusCallback, statusMsg);
    delay(500);
    return true;
  }

  state.wifiConnected = false;
  state.wifiConnectTime = 0;
  LOGW(LogTag::NETWORK, "Wi-Fi connection failed!");
  LOGD(LogTag::NETWORK, "Wi-Fi connection attempt time: %lu ms", connectionTime);

  updateStatus(statusCallback, "WiFi FAILED!");
  delay(1000);
  return false;
}

bool NetworkManager_SyncNtp(NetworkState &state, StatusCallback statusCallback)
{
  constexpr long gmtOffset_sec = 9 * 3600; // JST = UTC+9

  updateStatus(statusCallback, "Syncing NTP...");

  // Save RTC time immediately before NTP sync to measure accurate drift
  DeepSleepManager_SaveRtcTimeBeforeSync();

  const unsigned long startTime = millis();

  // === Custom NTP implementation (same as MeasureNtpDrift but also sets time) ===
  constexpr size_t NTP_PACKET_SIZE = 48;
  constexpr uint32_t SEVENTY_YEARS = 2208988800UL; // 1970 - 1900 in seconds

  WiFiUDP udp;
  byte packet[NTP_PACKET_SIZE] = {0};

  // NTP request header: LI=0, Version=3, Mode=3 (client)
  packet[0] = 0xE3; // 11100011

  udp.begin(123);
  udp.beginPacket("ntp.nict.jp", 123);
  udp.write(packet, NTP_PACKET_SIZE);
  udp.endPacket();

  // Wait for response (max 2 seconds)
  unsigned long startWait = millis();
  int packetSize = 0;
  while (packetSize == 0 && millis() - startWait < 2000)
  {
    delay(10);
    packetSize = udp.parsePacket();

    // Update status periodically
    if ((millis() - startWait) % 500 == 0)
    {
      char statusMsg[48];
      snprintf(statusMsg, sizeof(statusMsg), "NTP syncing... %lums", millis() - startWait);
      updateStatus(statusCallback, statusMsg);
    }
  }

  if (packetSize < NTP_PACKET_SIZE)
  {
    LOGW(LogTag::NETWORK, "NTP sync failed: no response (size=%d)", packetSize);
    udp.stop();
    state.ntpSynced = false;
    state.ntpSyncTime = 0;
    updateStatus(statusCallback, "NTP FAILED!");
    delay(1000);
    return false;
  }

  // Read response
  udp.read(packet, NTP_PACKET_SIZE);
  udp.stop();

  // Extract transmit timestamp (bytes 40-47)
  uint32_t ntpSeconds = (packet[40] << 24) | (packet[41] << 16) | (packet[42] << 8) | packet[43];
  uint32_t ntpFraction = (packet[44] << 24) | (packet[45] << 16) | (packet[46] << 8) | packet[47];

  // Convert to Unix timestamp and milliseconds
  time_t unixSeconds = ntpSeconds - SEVENTY_YEARS;
  uint32_t milliseconds = (uint32_t)(((uint64_t)ntpFraction * 1000) >> 32);

  const unsigned long syncTime = millis() - startTime;

  // Set system time
  struct timeval tv;
  tv.tv_sec = unixSeconds;
  tv.tv_usec = milliseconds * 1000;
  settimeofday(&tv, NULL);

  // Set timezone
  setenv("TZ", "JST-9", 1);
  tzset();

  // Save sync duration for drift calculation
  DeepSleepManager_SaveNtpSyncDuration(syncTime);

  // Get local time for logging
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  state.ntpSynced = true;
  state.ntpSyncTime = syncTime;
  LOGI(LogTag::NETWORK, "Time synchronized via custom NTP!");
  LOGI(LogTag::NETWORK, "Current time: %d:%02d:%02d.%03u",
       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, milliseconds);
  LOGD(LogTag::NETWORK, "NTP sync time: %lu ms", syncTime);

  char statusMsg[48];
  snprintf(statusMsg, sizeof(statusMsg), "NTP OK! (%lums)", syncTime);
  updateStatus(statusCallback, statusMsg);
  delay(500);
  return true;
}

bool NetworkManager_CheckNtpResync(NetworkState &state, unsigned long intervalMs, StatusCallback statusCallback)
{
  // NOTE: This function is currently unused. NTP sync timing is handled by DeepSleepManager_ShouldSyncWiFiNtp()
  // which uses boot count instead of millis() for better accuracy across deep sleep cycles.
  // If this function is needed in the future, it should use DeepSleepManager_ShouldSyncWiFiNtp() instead.

  state.wifiConnected = (WiFi.status() == WL_CONNECTED);

  // This function is deprecated - use DeepSleepManager_ShouldSyncWiFiNtp() instead
  // Keeping the function signature for API compatibility but it always returns false
  (void)intervalMs; // Unused parameter
  (void)statusCallback; // Unused parameter
  return false;
}

void NetworkManager_UpdateConnectionState(NetworkState &state)
{
  state.wifiConnected = (WiFi.status() == WL_CONNECTED);
}

bool NetworkManager_SetupTimeFromRTC()
{
  // Setup timezone (same as NTP sync)
  constexpr long gmtOffset_sec = 9 * 3600;
  constexpr int daylightOffset_sec = 0;
  constexpr const char *ntpServer = ""; // Empty string means no NTP server (we'll use RTC time)

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Get RTC state
  RTCState &rtcState = DeepSleepManager_GetRTCState();

  // If RTC has saved time, restore it
  if (rtcState.savedTime > 0)
  {
    // Calculate elapsed time since sleep started
    int64_t currentTimeUs = esp_timer_get_time();
    int64_t bootOverheadUs = currentTimeUs; // Time since boot until now

    // Calculate wakeup time with microsecond precision to avoid truncation drift
    // (same calculation as restoreTimeFromRTC() in deep_sleep_manager.cpp)
    int64_t savedTimeUs = (int64_t)rtcState.savedTime * 1000000LL + rtcState.savedTimeUs;
    int64_t wakeupTimeUs = savedTimeUs + (int64_t)rtcState.sleepDurationUs + bootOverheadUs;

    struct timeval tv;
    tv.tv_sec = (time_t)(wakeupTimeUs / 1000000LL);
    tv.tv_usec = (suseconds_t)(wakeupTimeUs % 1000000LL);
    settimeofday(&tv, NULL);

    // Re-apply timezone
    setenv("TZ", "JST-9", 1);
    tzset();

    LOGI(LogTag::NETWORK, "Time restored from RTC: %lu.%03lu", (unsigned long)tv.tv_sec, (unsigned long)(tv.tv_usec / 1000));
    return true;
  }

  LOGW(LogTag::NETWORK, "No RTC time available");
  return false;
}

int32_t NetworkManager_MeasureNtpDrift()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    LOGW(LogTag::NETWORK, "Cannot measure NTP drift: WiFi not connected");
    return INT32_MIN;
  }

  // Custom NTP implementation with millisecond precision
  // NTP packet: 48 bytes, we read transmit timestamp from bytes 40-47
  // Bytes 40-43: seconds since 1900-01-01
  // Bytes 44-47: fractional seconds (2^32 = 1 second)
  constexpr size_t NTP_PACKET_SIZE = 48;
  constexpr uint32_t SEVENTY_YEARS = 2208988800UL; // 1970 - 1900 in seconds

  WiFiUDP udp;
  byte packet[NTP_PACKET_SIZE] = {0};

  // NTP request header: LI=0, Version=3, Mode=3 (client)
  packet[0] = 0xE3; // 11100011

  udp.begin(123);
  udp.beginPacket("ntp.nict.jp", 123);
  udp.write(packet, NTP_PACKET_SIZE);
  udp.endPacket();

  // Wait for response (max 1 second)
  unsigned long startWait = millis();
  int packetSize = 0;
  while (packetSize == 0 && millis() - startWait < 1000)
  {
    delay(10);
    packetSize = udp.parsePacket();
  }

  if (packetSize < NTP_PACKET_SIZE)
  {
    LOGW(LogTag::NETWORK, "NTP drift measurement failed: no response (size=%d)", packetSize);
    udp.stop();
    return INT32_MIN;
  }

  // Capture system time immediately after receiving NTP response
  struct timeval tvSystem;
  gettimeofday(&tvSystem, NULL);

  // Read NTP response
  udp.read(packet, NTP_PACKET_SIZE);
  udp.stop();

  // Extract transmit timestamp (bytes 40-47)
  uint32_t ntpSeconds = ((uint32_t)packet[40] << 24) | ((uint32_t)packet[41] << 16) |
                        ((uint32_t)packet[42] << 8) | (uint32_t)packet[43];
  uint32_t ntpFraction = ((uint32_t)packet[44] << 24) | ((uint32_t)packet[45] << 16) |
                         ((uint32_t)packet[46] << 8) | (uint32_t)packet[47];

  // Convert NTP time to Unix epoch (subtract 70 years)
  time_t ntpUnixSec = ntpSeconds - SEVENTY_YEARS;

  // Convert fractional part to milliseconds: fraction / 2^32 * 1000
  uint32_t ntpMs = (uint32_t)(((uint64_t)ntpFraction * 1000) >> 32);

  // System time
  time_t systemSec = tvSystem.tv_sec;
  uint32_t systemMs = tvSystem.tv_usec / 1000;

  // Calculate drift in milliseconds (NTP - System)
  // Positive = system is behind (slow), Negative = system is ahead (fast)
  int64_t ntpTotalMs = (int64_t)ntpUnixSec * 1000 + ntpMs;
  int64_t systemTotalMs = (int64_t)systemSec * 1000 + systemMs;
  int32_t driftMs = (int32_t)(ntpTotalMs - systemTotalMs);

  LOGI(LogTag::NETWORK, "NTP drift measured: %d ms (NTP: %ld.%03u, System: %ld.%03u)",
       driftMs, (long)ntpUnixSec, ntpMs, (long)systemSec, systemMs);

  return driftMs;
}

bool NetworkManager_SendBatchData(const String &payload)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    LOGW(LogTag::NETWORK, "Cannot send batch data: WiFi not connected");
    return false;
  }

  HTTPClient http;

  // Construct URL
  String url = String(SENSOR_API_URL) + String(SENSOR_API_ENDPOINT);

  LOGI(LogTag::NETWORK, "Sending batch data (%d bytes) to %s", payload.length(), url.c_str());
  // Log first 500 chars of payload for debugging
  LOGI(LogTag::NETWORK, "Payload preview: %.500s", payload.c_str());

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Add API Key if configured
  String apiKey = String(API_KEY);
  if (apiKey.length() > 0)
  {
    http.addHeader("X-API-Key", apiKey);
  }

  // Add Cloudflare Access headers if configured
  String cfClientId = String(CF_ACCESS_CLIENT_ID);
  String cfClientSecret = String(CF_ACCESS_CLIENT_SECRET);
  if (cfClientId.length() > 0 && cfClientSecret.length() > 0)
  {
    http.addHeader("CF-Access-Client-Id", cfClientId);
    http.addHeader("CF-Access-Client-Secret", cfClientSecret);
  }

  int httpResponseCode = http.POST(payload);

  bool success = false;
  if (httpResponseCode > 0)
  {
    String response = http.getString();
    LOGI(LogTag::NETWORK, "Batch sent! Response code: %d", httpResponseCode);
    LOGD(LogTag::NETWORK, "Response: %s", response.c_str());
    success = (httpResponseCode >= 200 && httpResponseCode < 300);
  }
  else
  {
    LOGE(LogTag::NETWORK, "Error sending batch data: %s", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  return success;
}
