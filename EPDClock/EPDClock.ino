#include <time.h>
#include <WiFi.h>

#include "display_manager.h"
#include "imagebw_export.h"
#include "network_manager.h"
#include "sensor_manager.h"
#include "server_config.h"

namespace
{
  constexpr size_t kFrameBufferSize = 27200;
  constexpr unsigned long kNtpSyncInterval = 3600000; // 1 hour

  NetworkState networkState;
  bool sensorInitialized = false;

#if ENABLE_IMAGEBW_EXPORT
bool enableImageBWExport = true;
#else
bool enableImageBWExport = false;
#endif

void exportFrameBuffer()
{
  if (!enableImageBWExport)
  {
    return;
  }

  ImageBWExporter_Send(DisplayManager_GetFrameBuffer(), kFrameBufferSize);
}

void handleSensorInitializationResult()
{
  if (sensorInitialized)
  {
    Serial.println("SDC41 sensor initialized successfully!");
    return;
  }

  Serial.println("Warning: SDC41 sensor initialization failed!");
  Serial.println("Please check connections:");
  Serial.println("  SDA -> GPIO 38");
  Serial.println("  SCL -> GPIO 21");
  Serial.println("  VDD -> 3.3V");
  Serial.println("  GND -> GND");
}

void updateDisplay(bool forceUpdate)
{
  if (DisplayManager_UpdateDisplay(networkState, forceUpdate))
  {
    exportFrameBuffer();
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== EPD Clock with SDC41 Sensor ===");

  randomSeed(analogRead(0));

  DisplayManager_Init();
  DisplayManager_DrawSetupStatus("EPD Ready!");

  sensorInitialized = SensorManager_Begin();
  handleSensorInitializationResult();

  // Read sensor once after initialization to get initial data for first display
  // Use blocking read to ensure we have data before first display
  if (sensorInitialized)
  {
    Serial.println("Reading initial sensor data...");
    if (SensorManager_ReadBlocking(10000))
    {
      Serial.println("Initial sensor data read successfully");
    }
    else
    {
      Serial.println("Warning: Failed to read initial sensor data");
    }
  }

  DisplayManager_DrawSetupStatus("Connecting WiFi...");
  if (NetworkManager_ConnectWiFi(networkState, DisplayManager_DrawSetupStatus))
  {
    if (NetworkManager_SyncNtp(networkState, DisplayManager_DrawSetupStatus))
    {
      networkState.lastNtpSync = millis();
    }
  }
  else
  {
    networkState.ntpSynced = false;
  }

  DisplayManager_DrawSetupStatus("Starting...");
  updateDisplay(true);
}

void loop()
{
  bool displayUpdated = false;

  if (NetworkManager_CheckNtpResync(networkState, kNtpSyncInterval, DisplayManager_DrawSetupStatus))
  {
    displayUpdated = true;
    updateDisplay(true);
  }
  else
  {
    NetworkManager_UpdateConnectionState(networkState);
  }

  if (!displayUpdated)
  {
    updateDisplay(false);
  }

  delay(1000);
}
