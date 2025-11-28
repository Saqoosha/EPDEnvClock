#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

// ============================================
// ImageBW Export Server (local Python server)
// ============================================
// Enable/disable ImageBW export feature
// Set to 1 to enable, 0 to disable
#define ENABLE_IMAGEBW_EXPORT 0

// Local server for ImageBW export
#define IMAGEBW_SERVER_IP "192.168.11.9"
#define IMAGEBW_SERVER_PORT 8080

// ============================================
// Sensor Logger API (Cloudflare Pages)
// ============================================
#define SENSOR_API_ENDPOINT "/api/sensor"

// URL and API key are in a separate file (gitignored)
#include "secrets.h"

#endif
