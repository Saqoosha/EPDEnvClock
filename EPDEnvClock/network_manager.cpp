#include "network_manager.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <esp_timer.h>
#include <sys/time.h>

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

namespace
{
  // NTP servers to try in order (Japan-optimized)
  // 1. ntp.nict.jp - NICT (Japan's official time source)
  // 2. jp.pool.ntp.org - NTP Pool Japan zone (multiple servers)
  // 3. time.google.com - Google public NTP (global, highly reliable)
  const char *NTP_SERVERS[] = {
      "ntp.nict.jp",
      "jp.pool.ntp.org",
      "time.google.com"};
  constexpr size_t NTP_SERVER_COUNT = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
  constexpr size_t NTP_PACKET_SIZE = 48;
  constexpr uint32_t SEVENTY_YEARS = 2208988800UL; // 1970 - 1900 in seconds

  struct NtpQueryResult
  {
    int64_t correctedUnixUsAtReceive = 0; // Estimated true Unix time at local receive moment (us)
    int64_t offsetUs = 0;                // Estimated offset (server - client) at receive moment (us)
    int64_t rttUs = 0;                   // Estimated round-trip delay (us)
    unsigned long waitTimeMs = 0;        // Millis from wait start until response detected
  };

  inline uint32_t readU32BE(const byte *p)
  {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
  }

  inline void writeU32BE(byte *p, uint32_t v)
  {
    p[0] = (byte)((v >> 24) & 0xFF);
    p[1] = (byte)((v >> 16) & 0xFF);
    p[2] = (byte)((v >> 8) & 0xFF);
    p[3] = (byte)(v & 0xFF);
  }

  inline uint32_t usecToNtpFrac(uint32_t usec)
  {
    // frac = usec / 1e6 * 2^32
    return (uint32_t)(((uint64_t)usec << 32) / 1000000ULL);
  }

  inline uint32_t ntpFracToUsec(uint32_t frac)
  {
    // usec = frac / 2^32 * 1e6
    return (uint32_t)(((uint64_t)frac * 1000000ULL) >> 32);
  }

  inline int64_t timevalToUnixUs(const struct timeval &tv)
  {
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
  }

  inline int64_t ntpTimestampToUnixUs(uint32_t ntpSec, uint32_t ntpFrac)
  {
    // NTP epoch(1900) -> Unix epoch(1970)
    int64_t unixSec = (int64_t)ntpSec - (int64_t)SEVENTY_YEARS;
    int64_t usec = (int64_t)ntpFracToUsec(ntpFrac);
    return unixSec * 1000000LL + usec;
  }

  // Try to get NTP response from a single server
  // Returns true if successful, fills in NTP-derived corrected time + RTT/offset.
  bool tryNtpServer(const char *server, NtpQueryResult &out, StatusCallback statusCallback)
  {
    // DNS resolution
    LOGD(LogTag::NETWORK, "NTP: Resolving %s...", server);
    IPAddress ntpServerIP;
    if (!WiFi.hostByName(server, ntpServerIP))
    {
      LOGW(LogTag::NETWORK, "NTP: DNS resolution failed for %s", server);
      return false;
    }
    LOGD(LogTag::NETWORK, "NTP: Resolved to %s", ntpServerIP.toString().c_str());

    WiFiUDP udp;
    byte packet[NTP_PACKET_SIZE] = {0};

    // NTP request header: LI=0, Version=3, Mode=3 (client)
    // (0x1B = 00 011 011)
    packet[0] = 0x1B;

    // Client transmit timestamp (t1) in request (bytes 40-47)
    // Server will echo it back as Originate timestamp in response.
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    const int64_t t1UnixUs = timevalToUnixUs(tv1);
    const uint32_t t1NtpSec = (uint32_t)((uint64_t)tv1.tv_sec + (uint64_t)SEVENTY_YEARS);
    const uint32_t t1NtpFrac = usecToNtpFrac((uint32_t)tv1.tv_usec);
    writeU32BE(&packet[40], t1NtpSec);
    writeU32BE(&packet[44], t1NtpFrac);

    // Open UDP socket
    int udpBeginResult = udp.begin(123);
    LOGD(LogTag::NETWORK, "NTP: UDP begin result: %d", udpBeginResult);

    // Send NTP request
    int beginPacketResult = udp.beginPacket(ntpServerIP, 123);
    if (beginPacketResult == 0)
    {
      LOGW(LogTag::NETWORK, "NTP: beginPacket failed for %s", server);
      udp.stop();
      return false;
    }

    size_t bytesWritten = udp.write(packet, NTP_PACKET_SIZE);
    int endPacketResult = udp.endPacket();
    LOGD(LogTag::NETWORK, "NTP: Sent %zu bytes, endPacket=%d", bytesWritten, endPacketResult);

    if (endPacketResult == 0)
    {
      LOGW(LogTag::NETWORK, "NTP: endPacket failed for %s", server);
      udp.stop();
      return false;
    }

    // Wait for response (max 2 seconds)
    unsigned long startWait = millis();
    int packetSize = 0;
    LOGD(LogTag::NETWORK, "NTP: Waiting for response from %s...", server);
    while (packetSize == 0 && millis() - startWait < 2000)
    {
      delay(10);
      packetSize = udp.parsePacket();

      // Update status periodically
      if ((millis() - startWait) % 500 == 0)
      {
        char statusMsg[48];
        snprintf(statusMsg, sizeof(statusMsg), "NTP %s %lums", server, millis() - startWait);
        updateStatus(statusCallback, statusMsg);
      }
    }

    unsigned long waitTime = millis() - startWait;
    if (packetSize < NTP_PACKET_SIZE)
    {
      LOGW(LogTag::NETWORK, "NTP: No response from %s after %lums (size=%d)", server, waitTime, packetSize);
      udp.stop();
      return false;
    }
    LOGD(LogTag::NETWORK, "NTP: Received %d bytes from %s after %lums", packetSize, server, waitTime);

    // Capture destination timestamp (t4) as soon as packet is available
    struct timeval tv4;
    gettimeofday(&tv4, NULL);
    const int64_t t4UnixUs = timevalToUnixUs(tv4);

    // Read response
    udp.read(packet, NTP_PACKET_SIZE);
    udp.stop();

    // Parse timestamps from response
    // Originate (t1)  : bytes 24-31 (client transmit echoed by server)
    // Receive (t2)    : bytes 32-39
    // Transmit (t3)   : bytes 40-47
    const uint32_t t1RespSec = readU32BE(&packet[24]);
    const uint32_t t1RespFrac = readU32BE(&packet[28]);
    const uint32_t t2Sec = readU32BE(&packet[32]);
    const uint32_t t2Frac = readU32BE(&packet[36]);
    const uint32_t t3Sec = readU32BE(&packet[40]);
    const uint32_t t3Frac = readU32BE(&packet[44]);

    // Convert to Unix microseconds
    // Prefer our local captured t1UnixUs (more direct); but keep response for sanity log.
    const int64_t t2UnixUs = ntpTimestampToUnixUs(t2Sec, t2Frac);
    const int64_t t3UnixUs = ntpTimestampToUnixUs(t3Sec, t3Frac);

    // Standard NTP offset/delay (assuming symmetric network delay)
    // offset = ((t2 - t1) + (t3 - t4)) / 2
    // delay  = (t4 - t1) - (t3 - t2)
    const int64_t offsetUs = ((t2UnixUs - t1UnixUs) + (t3UnixUs - t4UnixUs)) / 2;
    const int64_t rttUs = (t4UnixUs - t1UnixUs) - (t3UnixUs - t2UnixUs);

    out.offsetUs = offsetUs;
    out.rttUs = rttUs;
    out.waitTimeMs = waitTime;
    out.correctedUnixUsAtReceive = t4UnixUs + offsetUs;

    LOGI(LogTag::NETWORK, "NTP: Got time from %s", server);
    LOGD(LogTag::NETWORK, "NTP: t1(req)=%ld.%03ld, t1(resp)=%lu.%03u, t2=%lld.%03lld, t3=%lld.%03lld, t4=%ld.%03ld, offset=%lldms, rtt=%lldms",
         (long)tv1.tv_sec, (long)(tv1.tv_usec / 1000),
         (unsigned long)(t1RespSec - SEVENTY_YEARS), (unsigned) (ntpFracToUsec(t1RespFrac) / 1000),
         (long long)(t2UnixUs / 1000000LL), (long long)((t2UnixUs % 1000000LL) / 1000LL),
         (long long)(t3UnixUs / 1000000LL), (long long)((t3UnixUs % 1000000LL) / 1000LL),
         (long)tv4.tv_sec, (long)(tv4.tv_usec / 1000),
         (long long)(offsetUs / 1000LL), (long long)(rttUs / 1000LL));
    return true;
  }
} // namespace

bool NetworkManager_SyncNtp(NetworkState &state, StatusCallback statusCallback)
{
  constexpr long gmtOffset_sec = 9 * 3600; // JST = UTC+9

  updateStatus(statusCallback, "Syncing NTP...");

  // Save RTC time immediately before NTP sync to measure accurate drift
  DeepSleepManager_SaveRtcTimeBeforeSync();

  const unsigned long startTime = millis();

  // Try each NTP server in order until one succeeds
  NtpQueryResult ntpResult;
  const char *successServer = nullptr;

  for (size_t i = 0; i < NTP_SERVER_COUNT; i++)
  {
    if (tryNtpServer(NTP_SERVERS[i], ntpResult, statusCallback))
    {
      successServer = NTP_SERVERS[i];
      break;
    }
    // Brief delay before trying next server
    if (i < NTP_SERVER_COUNT - 1)
    {
      LOGI(LogTag::NETWORK, "NTP: Trying fallback server...");
      delay(100);
    }
  }

  if (successServer == nullptr)
  {
    LOGE(LogTag::NETWORK, "NTP sync failed: all %d servers failed", NTP_SERVER_COUNT);
    state.ntpSynced = false;
    state.ntpSyncTime = 0;
    updateStatus(statusCallback, "NTP FAILED!");
    delay(1000);
    return false;
  }

  const unsigned long syncTime = millis() - startTime;

  // Use corrected time (offset/RTT compensated) instead of raw transmit timestamp
  // Adjust by monotonic elapsed time since receive moment, so settimeofday reflects "now".
  struct timeval tvNow;
  gettimeofday(&tvNow, NULL);
  const int64_t nowUnixUs = timevalToUnixUs(tvNow);
  // Estimate elapsed since receive using system clock (good enough here; offset is already computed)
  // This keeps code simple; the error is typically << 10ms.
  int64_t elapsedSinceReceiveUs = 0;
  if (ntpResult.correctedUnixUsAtReceive != 0)
  {
    // We don't store t4 separately; approximate with now - (correctedAtReceive - offset)
    // correctedAtReceive = t4 + offset  => t4 = correctedAtReceive - offset
    const int64_t t4UnixUsApprox = ntpResult.correctedUnixUsAtReceive - ntpResult.offsetUs;
    elapsedSinceReceiveUs = nowUnixUs - t4UnixUsApprox;
    if (elapsedSinceReceiveUs < 0) elapsedSinceReceiveUs = 0;
  }
  const int64_t correctedNowUnixUs = ntpResult.correctedUnixUsAtReceive + elapsedSinceReceiveUs;

  time_t unixSeconds = (time_t)(correctedNowUnixUs / 1000000LL);
  uint32_t microseconds = (uint32_t)(correctedNowUnixUs % 1000000LL);

  // Set system time
  struct timeval tv;
  tv.tv_sec = unixSeconds;
  tv.tv_usec = microseconds;
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
       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, (unsigned)(tv.tv_usec / 1000));
  LOGD(LogTag::NETWORK, "NTP sync time: %lu ms", syncTime);
  LOGI(LogTag::NETWORK, "NTP offset=%lld ms, RTT=%lld ms, wait=%lu ms (%s)",
       (long long)(ntpResult.offsetUs / 1000LL), (long long)(ntpResult.rttUs / 1000LL),
       ntpResult.waitTimeMs, successServer);

  char statusMsg[48];
  snprintf(statusMsg, sizeof(statusMsg), "NTP OK! (%lums)", syncTime);
  updateStatus(statusCallback, statusMsg);
  delay(50);
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

  // Try each NTP server until one succeeds
  for (size_t serverIdx = 0; serverIdx < NTP_SERVER_COUNT; serverIdx++)
  {
    const char *server = NTP_SERVERS[serverIdx];

    NtpQueryResult result;
    if (!tryNtpServer(server, result, nullptr))
    {
      continue;
    }

    // offsetUs is (server - client) at receive moment.
    // Positive = system is behind (slow), Negative = system is ahead (fast).
    int32_t driftMs = (int32_t)(result.offsetUs / 1000LL);
    LOGI(LogTag::NETWORK, "NTP drift measured via %s: %d ms (offset=%lldms, rtt=%lldms, wait=%lums)",
         server, driftMs, (long long)(result.offsetUs / 1000LL), (long long)(result.rttUs / 1000LL), result.waitTimeMs);
    return driftMs;
  }

  LOGE(LogTag::NETWORK, "NTP drift measurement failed: all %d servers failed", NTP_SERVER_COUNT);
  return INT32_MIN;
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
