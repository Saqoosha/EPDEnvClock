#include "imagebw_export.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "server_config.h"
#include "logger.h"

bool ImageBWExporter_Send(const uint8_t *buffer, size_t length)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    LOGW(LogTag::IMAGEBW, "WiFi not connected, skipping export");
    return false;
  }

  HTTPClient http;
  String url = "http://" + String(IMAGEBW_SERVER_IP) + ":" + String(IMAGEBW_SERVER_PORT) + "/imagebw";

  LOGD(LogTag::IMAGEBW, "Sending to server: %s", url.c_str());

  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Content-Length", String(length));

  const unsigned long startTime = millis();
  // HTTPClient::POST expects a non-const pointer, but it does not modify the payload.
  const int httpResponseCode = http.POST(const_cast<uint8_t *>(buffer), length);
  const unsigned long sendTime = millis() - startTime;

  bool success = false;

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    LOGI(LogTag::IMAGEBW, "Response code: %d", httpResponseCode);
    LOGD(LogTag::IMAGEBW, "Response: %s", response.c_str());
    LOGD(LogTag::IMAGEBW, "Send time: %lu ms", sendTime);

    if (httpResponseCode == 200)
    {
      success = true;
    }
  }
  else
  {
    LOGE(LogTag::IMAGEBW, "Error: %d", httpResponseCode);
  }

  http.end();
  return success;
}
