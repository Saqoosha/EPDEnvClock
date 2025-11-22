#include "network_manager.h"

#include <WiFi.h>
#include <time.h>

#include "wifi_config.h"

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
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  updateStatus(statusCallback, "Connecting WiFi...");

  const unsigned long startTime = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts % 4 == 0)
    {
      char statusMsg[48];
      snprintf(statusMsg, sizeof(statusMsg), "WiFi connecting... %d", attempts);
      updateStatus(statusCallback, statusMsg);
    }
  }
  Serial.println();

  const unsigned long connectionTime = millis() - startTime;

  if (WiFi.status() == WL_CONNECTED)
  {
    state.wifiConnected = true;
    state.wifiConnectTime = connectionTime;
    Serial.print("Wi-Fi connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[TIMING] Wi-Fi connection time: ");
    Serial.print(connectionTime);
    Serial.println(" ms");

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "WiFi OK! (%lums)", connectionTime);
    updateStatus(statusCallback, statusMsg);
    delay(500);
    return true;
  }

  state.wifiConnected = false;
  state.wifiConnectTime = 0;
  Serial.println("Wi-Fi connection failed!");
  Serial.print("[TIMING] Wi-Fi connection attempt time: ");
  Serial.print(connectionTime);
  Serial.println(" ms");

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

  Serial.print("Waiting for NTP time sync");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10)
  {
    Serial.print(".");
    delay(1000);
    attempts++;

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "NTP syncing... %d", attempts);
    updateStatus(statusCallback, statusMsg);
  }
  Serial.println();

  const unsigned long syncTime = millis() - startTime;

  if (attempts < 10)
  {
    state.ntpSynced = true;
    state.ntpSyncTime = syncTime;
    Serial.println("Time synchronized!");
    Serial.print("Current time: ");
    Serial.print(timeinfo.tm_hour);
    Serial.print(":");
    Serial.println(timeinfo.tm_min);
    Serial.print("[TIMING] NTP sync time: ");
    Serial.print(syncTime);
    Serial.println(" ms");

    char statusMsg[48];
    snprintf(statusMsg, sizeof(statusMsg), "NTP OK! (%lums)", syncTime);
    updateStatus(statusCallback, statusMsg);
    delay(500);
    return true;
  }

  state.ntpSynced = false;
  state.ntpSyncTime = 0;
  Serial.println("NTP time sync failed!");
  Serial.print("[TIMING] NTP sync attempt time: ");
  Serial.print(syncTime);
  Serial.println(" ms");

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
