# CC1101 443MHz Signal Manager - Setup Guide

This document provides setup and usage instructions for the CC1101 443MHz Remote Control Signal Manager.

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Software Installation](#software-installation)
4. [Building and Flashing](#building-and-flashing)
5. [First Run and Configuration](#first-run-and-configuration)
6. [Web Interface Usage](#web-interface-usage)
7. [API Reference](#api-reference)
8. [Troubleshooting](#troubleshooting)
9. [Advanced Configuration](#advanced-configuration)
10. [Future Enhancements](#future-enhancements)

## System Overview

The CC1101 443MHz Signal Manager is a complete solution for:

- **Driving the CC1101 RF module** at 443MHz frequency
- **Acquiring remote control signals** via RF demodulation
- **Storing signal data** with automatic file management
- **Transmitting stored signals** for replay
- **Providing web-based control** via ESP32S3 AP mode
- **Managing signals** with JSON export/import

### Key Components

1. **CC1101 Driver** (`cc1101_driver.c/h`)
   - SPI interface management
   - RF configuration and calibration
   - Signal acquisition and transmission
   - Error handling and diagnostics

2. **Web Server** (`web_server.c/h`)
   - RESTful API for signal control
   - Web UI for easy operation
   - Signal file management
   - Real-time status monitoring

3. **Main Application** (`softap_sta.c`)
   - WiFi softAP and station configuration
   - CC1101 integration
   - Web server startup
   - Signal acquisition tasks

4. **Configuration Files**
   - `CMakeLists.txt` - Build system configuration
   - `sdkconfig` - ESP-IDF configuration
   - `README.md` - This documentation

## Hardware Requirements

### Required Components

1. **ESP32S3 Microcontroller**
   - ESP32-S3 series microcontroller
   - At least 240KB RAM
   - Multiple UART, SPI, GPIO interfaces

2. **CC1101 RF Module**
   - CC1101 433/915MHz RF transceiver
   - SPI interface compatible
   - GDO0/GDO2 pins for interrupts

3. **Connection Wiring**

```
ESP32S3          CC1101
------------     ------------
GND             GND
3.3V            VCC
GPIO2 (CS)      CS
GPIO18 (MOSI)   MOSI
GPIO19 (MISO)   MISO
GPIO21 (SCLK)   SCLK
GPIO9  (IRQ)    GDO0 (optional)
GPIO46 (GDO2)   GDO2 (optional)
```

### Optional Components

- **Display** (OLED/I2C) for local status
- **Push buttons** for manual control
- **LEDs** for status indication

## Software Installation

### Prerequisites

1. **ESP-IDF (Espressif IoT Development Framework)**
   - Latest stable version (recommended: 5.x)
   - Install from: https://github.com/espressif/esp-idf

2. **Development Tools**
   - CMake 3.16 or higher
   - Python 3.7+
   - Git for version control

3. **Environment Setup**

```bash
# Set up ESP-IDF environment
cd ~/esp
git clone https://github.com/espressif/esp-idf.git esp-idf
cd esp-idf
./install.sh esp32s3

# Source the environment
source ./setenv.sh
```

### Project Dependencies

The project uses standard ESP-IDF components:

```bash
# Clone the project
cd ~/esp
project_path="$HOME/esp/softap_sta"
git clone https://github.com/user/softap_sta.git "$project_path"

# Install dependencies
idf.py dep ensure
```

## Building and Flashing

### Build Steps

1. **Navigate to project directory**

```bash
cd D:/code/softap_sta
```

2. **Set target** (if not already configured)

```bash
idf.py set-target esp32s3
```

3. **Configure project** (first time)

```bash
idf.py menuconfig
```

**Configuration Options:**

- **Example Configuration** → WiFi SoftAP settings:
  - WiFi AP SSID: `CC1101-AP`
  - WiFi AP Password: `password123`
  - WiFi AP Channel: `1`

- **Example Configuration** → WiFi STA settings:
  - WiFi Remote AP SSID: `[Your home/office WiFi]`
  - WiFi Remote AP Password: `[Your WiFi password]`
  - WiFi Maximum STA Retry: `10`

- **Example Configuration** → CC1101 Settings:
  - CC1101 CS GPIO: `10`
  - CC1101 IRQ GPIO: `9`
  - CC1101 SPI Host: `SPI2_HOST`
  - CC1101 Max Signal Size: `1048576`

4. **Build the application**

```bash
idf.py build
```

**Expected output:**
```
... (compiling components) ...
CC1101 driver version 1.0.0
Web server ready
Project build complete
```

5. **Flash to device**

```bash
idf.py -p COMx flash
```

Replace `COMx` with your ESP32S3 port (e.g., `COM3`, `/dev/ttyUSB0`).

6. **Monitor output**

```bash
idf.py -p COMx monitor
```

## First Run and Configuration

### Initial Setup

After flashing, the system will:

1. **Initialize WiFi** in AP+STA mode
2. **Start web server** for remote control
3. **Configure CC1101** module
4. **Begin background tasks** for signal acquisition

### First Connection

1. **Connect your device** to WiFi:
   - Network Name: `CC1101-AP`
   - Password: `password123`

2. **Open web browser** and navigate to:
   `http://192.168.1.1/`

3. **Web Interface Overview**:

```
┌─────────────────────────────────────┐
│ CC1101 443MHz Remote Control Manager │
├─────────────────────────────────────┤
│                                     │
│      System Status: Running         │
│      CC1101: Initialized           │
│      WiFi: Connected                │
│      Signals: 0                     │
│                                     │
│  ┌─────────────────────────────┐   │
│  │ Start Signal Capture         │   │
│  │ Upload Signal File           │   │
│  │ Transmit Stored Signal       │   │
│  └─────────────────────────────┘   │
│                                     │
│  [Stored Signals]                  │
│  ┌─────────────────────────────┐   │
│  │ (empty - no signals yet)       │   │
│  └─────────────────────────────┘   │
│                                     │
│  [Captured Signal Data]            │
│  ┌─────────────────────────────┐   │
│  │ (no signal data)              │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │ [Refresh]  [API Status]         │   │
│  └─────────────────────────────┘   │
└─────────────────────────────────────┘
```

### Signal Capture

1. **Start Signal Capture**:
   - Click "Start Signal Capture" button
   - System will begin recording RF signals
   - Signals are automatically saved as `.dat` files

2. **Upload Signal File**:
   - Click "Upload Signal File" → "Choose File"
   - Select a signal file from your computer
   - File will be uploaded and stored

3. **Signal Management**:
   - Signals are listed in "Stored Signals" section
   - Click "Load" to view signal data
   - Click "Delete" to remove signals

### API Usage

The system provides RESTful API endpoints:

#### GET /api/status
Returns system status information.

```json
{
  "status": "running",
  "cc1101_initialized": true,
  "capture_active": false,
  "web_server_running": true,
  "total_signals": 3,
  "frequency": 443000000,
  "modulation": "GFSK"
}
```

#### GET /api/signal/list
Returns list of stored signals.

```json
[
  {
    "id": 1,
    "filename": "signal_000001.dat",
    "size": 8192,
    "timestamp": 1701234567,
    "modified_time": 1701234567
  },
  {
    "id": 2,
    "filename": "signal_000002.dat",
    "size": 16384,
    "timestamp": 1701234600,
    "modified_time": 1701234600
  }
]
```

#### POST /api/signal/capture
Captures a new signal or uploads a file.

**Request Body (JSON):**
```json
{
  "type": "auto",
  "data": "base64_encoded_signal_data"
}
```

**Response:**
```json
{
  "status": "success",
  "message": "Signal captured and saved successfully",
  "filename": "signal_000003_auto_1701234567.dat",
  "size": 8192,
  "timestamp": 1701234567
}
```

#### GET /api/signal/download/{signal_id}
Downloads a stored signal file.

#### DELETE /api/signal/capture?id={signal_id}
Deletes a signal.

## Web Interface Usage

### Main Dashboard

The web interface provides all necessary controls:

1. **System Status Panel**
   - CC1101 module status
   - WiFi connection status
   - Signal count
   - System uptime

2. **Control Panel**
   - Start/Stop signal capture
   - Upload local signal files
   - Transmit stored signals
   - Manual transmission control

3. **Signal Management**
   - List all stored signals
   - View signal details (size, timestamp, etc.)
   - Load and display signal data
   - Delete unwanted signals

4. **Signal Data Display**
   - Hexadecimal representation of signal data
   - Search and filter capabilities
   - Export options

### Signal Capture Process

1. **Initial State**: System idle, ready for commands
2. **Start Capture**: System begins listening for RF signals
3. **Real-time Updates**: Signal list auto-refreshes every 5 seconds
4. **Capture Complete**: Signal automatically saved with timestamp
5. **Ready for Next Command**: System waits for user input

### File Upload

1. **Click "Upload Signal File"** button
2. **Select file** from your computer
3. **System validates** file format and size
4. **File is base64 encoded** and saved
5. **Feedback provided** for upload status

## API Reference

### Endpoint: GET /api/status

**Purpose**: Get system status
**Method**: GET
**URL**: `/api/status`
**Response**: JSON object with system status

### Endpoint: GET /api/signal/list

**Purpose**: List stored signals
**Method**: GET
**URL**: `/api/signal/list`
**Response**: JSON array of signal objects

### Endpoint: POST /api/signal/capture

**Purpose**: Capture new signal or upload file
**Method**: POST
**URL**: `/api/signal/capture`
**Content-Type**: `application/json`
**Request Body**: JSON with signal data
**Response**: JSON confirmation

### Endpoint: GET /api/signal/download/{signal_id}

**Purpose**: Download stored signal file
**Method**: GET
**URL**: `/api/signal/download/{signal_id}`
**Response**: Signal file (application/octet-stream)

### Endpoint: DELETE /api/signal/capture

**Purpose**: Delete stored signal
**Method**: DELETE
**URL**: `/api/signal/capture`
**Query Parameters**: `id={signal_id}`
**Response**: JSON confirmation

## Troubleshooting

### Common Issues

#### Issue: Web server not accessible

**Symptoms**: Cannot connect to `http://192.168.1.1/`

**Solutions**:

1. **Check WiFi connection**:
   - Ensure ESP32S3 is connected to power
   - Verify AP SSID: `CC1101-AP`
   - Check password: `password123`

2. **Network configuration**:
   - Ensure device is on same network
   - Check router firewall settings
   - Verify ESP32S3 has correct IP address

3. **Port conflicts**:
   - Ensure port 80 is not used by other service
   - Check firewall settings

#### Issue: CC1101 not responding

**Symptoms**: Signal capture fails, CC1101 initialization errors

**Solutions**:

1. **Hardware connections**:
   - Verify all wiring is correct
   - Check GPIO pin assignments
   - Ensure proper power supply to CC1101

2. **SPI configuration**:
   - Verify SPI bus speed matches CC1101 requirements
   - Check CS pin configuration
   - Ensure IRQ pin is properly connected

3. **Configuration**:
   - Check `sdkconfig` settings
   - Verify CC1101 GPIO assignments
   - Ensure SPI host selection is correct

#### Issue: JSON parsing errors

**Symptoms**: "JSON parsing failed: The escape characters in the JSON string caused parsing error"

**Solutions**:

1. **Check base64 encoding**:
   - Ensure signal data is properly base64 encoded
   - Verify no invalid characters in JSON

2. **File size limits**:
   - Check maximum signal size configuration
   - Ensure files are not too large

3. **JSON escaping**:
   - Properly escape special characters
   - Use proper JSON escaping functions

#### Issue: Signal files not saving

**Symptoms**: Signals captured but not stored

**Solutions**:

1. **Storage permissions**:
   - Ensure write permissions to storage directory
   - Check available disk space

2. **Directory structure**:
   - Verify `/sdcard/signals/` directory exists
   - Check file system mount status

3. **Memory issues**:
   - Check system memory usage
   - Verify buffer sizes are adequate

### Debugging Steps

1. **Check serial output**:
   ```bash
   idf.py monitor
   ```

2. **Verify WiFi status**:
   - Check AP is broadcasting
   - Verify STA connection

3. **Test CC1101 directly**:
   - Use oscilloscope to verify SPI communication
   - Check GDO0/GDO2 signals

4. **Check web server logs**:
   - Monitor HTTP request/response logs
   - Check for error conditions

## Advanced Configuration

### Modifying CC1101 Parameters

To change CC1101 configuration, edit `sdkconfig`:

```makefile
CONFIG_CC1101_FREQUENCY=433000000
CONFIG_CC1101_CHANNEL=0
CONFIG_CC1101_SAMPLE_RATE=19200
CONFIG_CC1101_POWER_LEVEL=5
```

### Custom Signal Processing

For advanced signal processing, modify `cc1101_driver.c`:

```c
// Example: Add signal filtering
void apply_signal_filter(uint8_t* signal, uint32_t size) {
    // Implement low-pass filter
    // Remove noise
    // Enhance signal quality
}
```

### Multiple CC1101 Modules

For multiple modules, update GPIO assignments:

```makefile
CONFIG_CC1101_CS_GPIO_0=10
CONFIG_CC1101_CS_GPIO_1=11
CONFIG_CC1101_IRQ_GPIO_0=9
CONFIG_CC1101_IRQ_GPIO_1=8
```

### Custom Web Interface

To customize the web interface, modify `web_server.c`:

```c
// Example: Add custom HTML/CSS
const char* custom_html = """
<style>
    .custom-signal { color: #ff6600; }
    .signal-list { background: #f0f0f0; }
</style>
""";
```

## Future Enhancements

### Planned Features

1. **Real-time Signal Visualization**
   - Oscilloscope-style signal display
   - Frequency spectrum analyzer
   - Real-time signal strength monitoring

2. **Advanced Signal Processing**
   - Automatic signal classification
   - Noise reduction algorithms
   - Signal quality metrics

3. **Enhanced Security**
   - HTTPS support for web interface
   - Authentication for API access
   - Signal encryption for transmission

4. **Mobile Integration**
   - Android/iOS mobile app
   - Bluetooth Low Energy control
   - Push notifications for events

5. **Cloud Integration**
   - AWS IoT integration
   - Google Cloud Functions
   - Azure IoT Hub support

6. **Multiple Protocol Support**
   - support for other RF protocols (NRF24L01, SX1278)
   - Multiple frequency bands
   - Protocol auto-detection

### Technical Improvements

1. **Performance Optimization**
   - DMA-based SPI transfers
   - Multi-threading for signal processing
   - Reduced memory footprint

2. **Reliability Enhancements**
   - Automatic reconnection
   - Error recovery mechanisms
   - Health monitoring

3. **Extensibility**
   - Plugin architecture for signal processing
   - Configurable via web interface
   - REST API for integration

## License

This project is licensed under the Unlicense OR CC0-1.0.
See LICENSE file for details.

## Acknowledgments

- **Espressif Systems** for ESP32S3 and ESP-IDF
- **CC1101 Community** for technical documentation
- **Open Source Contributors** for SPI and networking libraries
- **Arduino/ESP communities** for inspiration and examples

## Support

For issues, questions, or contributions:

1. **GitHub Issues**: Report bugs, request features
2. **Documentation**: Check `README.md` for detailed information
3. **Examples**: Use `demo_cc1101_signal_manager.py` for testing
4. **Community**: Join ESP32/Arduino forums for discussion

## Quick Start Checklist

- [ ] Install ESP-IDF and development tools
- [ ] Clone this repository
- [ ] Configure `sdkconfig` settings
- [ ] Build and flash to ESP32S3
- [ ] Connect to WiFi network
- [ ] Access web interface at `http://192.168.1.1/`
- [ ] Start signal capture
- [ ] Upload or capture signals
- [ ] Monitor and manage signals

## Conclusion

The CC1101 443MHz Signal Manager provides a complete solution for RF signal acquisition and management. With its web-based interface and comprehensive API, it offers easy control and monitoring of CC1101 operations. The system is designed to be extensible, reliable, and user-friendly, making it suitable for both hobbyists and professionals.

The implementation includes thorough error handling, proper documentation, and examples to help users get started quickly. Future enhancements will continue to expand its capabilities for advanced RF signal processing and management.
