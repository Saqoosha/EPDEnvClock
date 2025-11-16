#include "imagebw_export.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "server_config.h"

bool ImageBWExporter_Send(const uint8_t *buffer, size_t length)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[ImageBW] WiFi not connected, skipping export");
    return false;
  }

  HTTPClient http;
  String url = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + "/imagebw";

  Serial.print("[ImageBW] Sending to server: ");
  Serial.println(url);

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
