#include "network_manager.h"

#include <WiFi.h>
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
  constexpr const char *ntpServer = "ntp.nict.jp";
  constexpr long gmtOffset_sec = 9 * 3600;
  constexpr int daylightOffset_sec = 0;

  updateStatus(statusCallback, "Syncing NTP...");

  const unsigned long startTime = millis();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  LOGD(LogTag::NETWORK, "Waiting for NTP time sync");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10)
  {
    LOGD(LogTag::NETWORK, ".");
    delay(1000);
    attempts++;

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "NTP syncing... %d", attempts);
    updateStatus(statusCallback, statusMsg);
  }

  const unsigned long syncTime = millis() - startTime;

  if (attempts < 10)
  {
    state.ntpSynced = true;
    state.ntpSyncTime = syncTime;
    LOGI(LogTag::NETWORK, "Time synchronized!");
    LOGI(LogTag::NETWORK, "Current time: %d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    LOGD(LogTag::NETWORK, "NTP sync time: %lu ms", syncTime);

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "NTP OK! (%lums)", syncTime);
    updateStatus(statusCallback, statusMsg);
    delay(500);
    return true;
  }

  state.ntpSynced = false;
  state.ntpSyncTime = 0;
  LOGW(LogTag::NETWORK, "NTP time sync failed!");
  LOGD(LogTag::NETWORK, "NTP sync attempt time: %lu ms", syncTime);

  updateStatus(statusCallback, "NTP FAILED!");
  delay(1000);
  return false;
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

    // Calculate wakeup time: savedTime + sleepDuration + bootOverhead
    time_t wakeupTime = rtcState.savedTime + (rtcState.sleepDurationUs / 1000000) + (bootOverheadUs / 1000000);

    struct timeval tv;
    tv.tv_sec = wakeupTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // Re-apply timezone
    setenv("TZ", "JST-9", 1);
    tzset();

    LOGI(LogTag::NETWORK, "Time restored from RTC: %lu", (unsigned long)wakeupTime);
    return true;
  }

  LOGW(LogTag::NETWORK, "No RTC time available");
  return false;
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
