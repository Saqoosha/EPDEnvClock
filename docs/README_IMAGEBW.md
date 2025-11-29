# ImageBW Wi-Fi Export Usage Guide

## Overview

A feature to send ImageBW canvas arrays from Arduino to a Python server on Mac via Wi-Fi and save them as PNG images.

## Setup

### 1. Configure Server Settings

Set the server's IP address and port in `server_config.h`:

```c
#define ENABLE_IMAGEBW_EXPORT 1           // 1 to enable, 0 to disable
#define IMAGEBW_SERVER_IP "192.168.3.1"   // Mac's IP address
#define IMAGEBW_SERVER_PORT 8080          // Server port
```

### 2. Start the Python Server

```bash
python3 imagebw_server.py --port 8080
```

The server provides the following endpoints:

- `POST /imagebw` - Receives binary ImageBW data (27,200 bytes)
- `POST /imagebw/base64` - Receives Base64-encoded ImageBW data
- `GET /status` - Check server status

### 3. Upload to Arduino

1. Update `server_config.h` with your Mac's IP address
2. Upload `EPDEnvClock.ino` using Arduino IDE or arduino-cli

## Operation

### Automatic Transmission

- Automatically sends ImageBW whenever the display is updated
- Periodic transmission is also possible with `IMAGEBW_EXPORT_INTERVAL` (default: 60 seconds)

### Configuration Options

The following settings can be configured in `EPDEnvClock.ino`:

```cpp
bool enableImageBWExport = true;  // Set to false to disable
const unsigned long IMAGEBW_EXPORT_INTERVAL = 60000; // Set to 0 to disable automatic transmission
```

## Output

Received ImageBW data is saved as PNG files in the `output/` directory:

- Filename format: `imagebw_YYYYMMDD_HHMMSS.png`
- Image size: 800x272 pixels (1-bit black and white)

## Troubleshooting

### Cannot Connect to Server

- Verify Mac's IP address: `ifconfig | grep "inet "`
- Check if port 8080 is open in the firewall
- Verify Arduino and Mac are connected to the same Wi-Fi network

### Data Not Being Received

- Check Arduino's serial monitor for `[ImageBW]` logs
- Verify Wi-Fi connection status
- Confirm `enableImageBWExport` is set to `true`

### Image Not Displaying Correctly

- Verify ImageBW data size is 27,200 bytes
- You can test by running `convert_imagebw.py` directly

## Testing

### Check Server Status

```bash
curl http://localhost:8080/status
```

### Manually Test Image Conversion

```bash
# If you have a binary file
python3 convert_imagebw.py <input_file.bin> output.png
```
