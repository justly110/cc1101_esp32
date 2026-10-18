# CC1101 443MHz Signal Manager - Project Summary

## Overview

This project implements a complete solution for driving the CC1101 443MHz RF module, implementing 443MHz remote control signal acquisition and storage, and providing a web interface for control via ESP32S3's AP mode.

## Files Created

### 1. Main Application (`main/softap_sta.c`)
**Purpose**: Core application that integrates WiFi softAP/station functionality with CC1101 control and web server.

**Key Features**:
- WiFi softAP configuration for device discovery
- Station mode for network connectivity
- CC1101 RF module integration
- Web server startup and background task management
- Signal acquisition task management

### 2. CC1101 Driver (`main/cc1101_driver.c`)
**Purpose**: Complete driver for CC1101 RF module with SPI communication.

**Key Features**:
- SPI interface initialization and configuration
- CC1101 register operations
- Signal acquisition and transmission
- Error handling and diagnostics
- 443MHz frequency optimization

### 3. CC1101 Driver Header (`main/cc1101_driver.h`)
**Purpose**: Interface definitions and function prototypes for CC1101 driver.

**Includes**:
- Configuration structures
- Function declarations
- Status codes
- Constants and macros

### 4. Web Server (`main/web_server.c`)
**Purpose**: HTTP server providing web interface and RESTful API for signal control.

**Key Features**:
- RESTful API endpoints for signal operations
- Web UI for signal capture and management
- Base64 encoding/decoding for JSON compatibility
- Signal file upload/download
- Real-time status monitoring

### 5. Web Server Header (`main/web_server.h`)
**Purpose**: Interface definitions for web server functions.

**Includes**:
- HTTP server configuration
- API endpoint declarations
- Signal data structures
- Utility function prototypes

### 6. Build Configuration (`CMakeLists.txt`)
**Purpose**: Build system configuration for the project.

**Includes**:
- ESP-IDF project setup
- Component definitions
- Target platform specification
- Source file management

### 7. SDK Configuration (`sdkconfig`)
**Purpose**: ESP-IDF configuration settings.

**Includes**:
- WiFi configuration
- SPI settings
- HTTP server configuration
- CC1101 specific settings
- Component configurations

### 8. Documentation (`README.md`)
**Purpose**: Comprehensive documentation and setup guide.

**Includes**:
- Project overview and features
- Hardware requirements
- Software installation
- Building and flashing instructions
- Web interface usage
- API reference
- Troubleshooting guide
- Configuration options

### 9. Setup and Test (`setup_and_test.py`)
**Purpose**: Quick setup and test script.

**Includes**:
- Environment validation
- Project structure checking
- Build process automation
- Demonstration execution
- Usage instructions

### 10. Demo Script (`demo_cc1101_signal_manager.py`)
**Purpose**: Interactive demonstration of system capabilities.

**Includes**:
- CC1101 initialization simulation
- Signal capture demonstration
- Signal transmission testing
- Web server simulation
- JSON export functionality

## System Architecture

### 1. Hardware Layer
- **ESP32S3**: Main microcontroller with WiFi capabilities
- **CC1101**: 443MHz RF transceiver module
- **SPI Interface**: Communication between ESP32S3 and CC1101
- **GPIO**: Control and status signals

### 2. Software Layer
- **CC1101 Driver**: Low-level RF module control
- **Web Server**: HTTP interface and API
- **Main Application**: Integration and task management
- **FreeRTOS**: Real-time operating system

### 3. Network Layer
- **SoftAP**: Access point for device discovery
- **Station**: Connection to external network
- **Web Interface**: HTTP-based control
- **REST API**: Programmatic control

### 4. Storage Layer
- **File System**: Signal data storage
- **JSON**: Configuration and export
- **Base64**: Data encoding for transmission

## Key Features

### CC1101 Control
- ✅ SPI interface initialization
- ✅ 443MHz frequency configuration
- ✅ Signal acquisition with configurable sample rate
- ✅ Signal transmission capabilities
- ✅ Error handling and recovery

### Signal Processing
- ✅ Real-time signal acquisition
- ✅ Base64 encoding for JSON compatibility
- ✅ Signal validation and error handling
- ✅ Efficient memory management

### Web Interface
- ✅ RESTful API for signal control
- ✅ Interactive web UI for easy operation
- ✅ Signal file management
- ✅ Real-time status monitoring

### Network Configuration
- ✅ ESP32S3 running in AP+STA mode
- ✅ SoftAP for device discovery and web access
- ✅ Station mode for network connectivity
- ✅ DNS configuration for proper routing
- ✅ NAT routing for integrated network operations

## Technical Specifications

### CC1101 Configuration
- **Frequency**: 443MHz ± 0.5MHz
- **Modulation**: GFSK
- **Sample Rate**: 38.4 kSamples/s
- **Output Power**: +10 dBm (adjustable)
- **Bandwidth**: 500 kHz
- **Channels**: Channel 1

### Web Server Configuration
- **Port**: 80 (HTTP)
- **Max Connections**: 4 concurrent
- **Max URI Handlers**: 16
- **Max Response Headers**: 20
- **Stack Size**: 8KB
- **Task Priority**: 1

### Signal Storage
- **Max Signal Size**: 1MB
- **Storage Directory**: `/sdcard/signals/`
- **File Format**: `.dat` files
- **Encoding**: Base64 for JSON transmission

## Build Instructions

### Prerequisites
1. Install ESP-IDF (latest stable version)
2. Clone the project repository
3. Install dependencies: `idf.py dep ensure`

### Build Steps

1. **Navigate to project directory**:
   ```bash
   cd D:/code/softap_sta
   ```

2. **Set target** (if not already configured):
   ```bash
   idf.py set-target esp32s3
   ```

3. **Build the application**:
   ```bash
   idf.py build
   ```

4. **Flash to device**:
   ```bash
   idf.py -p COMx flash  # Replace COMx with your port
   ```

5. **Monitor output**:
   ```bash
   idf.py -p COMx monitor
   ```

### Configuration Options

Use `idf.py menuconfig` to configure:

- **WiFi SoftAP Settings**: SSID, password, channel
- **WiFi Station Settings**: Remote AP credentials
- **CC1101 Settings**: GPIO assignments, frequency
- **Web Server Settings**: Port, connection limits

## Usage Instructions

### Initial Setup

1. **After flashing and running**, the system will:
   - Initialize WiFi softAP (SSID: `CC1101-AP`, Password: `password123`)
   - Start web server for remote control
   - Configure CC1101 module
   - Begin background tasks for signal acquisition

2. **Connect your device** to WiFi:
   - Network Name: `CC1101-AP`
   - Password: `password123`

3. **Open web browser** and navigate to:
   `http://192.168.1.1/`

### Web Interface Usage

1. **Signal Capture**: Click "Start Signal Capture" to begin recording RF signals
2. **File Upload**: Click "Upload Signal File" to upload local signal files
3. **Signal Management**: View and manage captured signals in "Stored Signals" section
4. **Transmission**: Click "Transmit Stored Signal" to send saved signals
5. **Status Monitoring**: View system status in real-time

### API Usage

The system provides RESTful API endpoints:

- `GET /api/status` - System status
- `GET /api/signal/list` - List stored signals
- `POST /api/signal/capture` - Capture/upload signal
- `GET /api/signal/download/{id}` - Download signal file
- `DELETE /api/signal/capture` - Delete signal

## Testing and Demo

### Quick Test

Run the demo script to test the system:

```bash
python demo_cc1101_signal_manager.py
```

### Setup and Test

Run the setup script for automatic testing:

```bash
python setup_and_test.py
```

## Troubleshooting

### Common Issues

1. **Web server not accessible**: Check WiFi connection and port forwarding
2. **CC1101 not responding**: Verify hardware connections and SPI configuration
3. **JSON parsing errors**: Check base64 encoding and JSON formatting
4. **Signal files not saving**: Check storage permissions and disk space

### Debugging

1. **Check serial output** for error messages
2. **Verify WiFi status** for connection issues
3. **Test CC1101 directly** using oscilloscope
4. **Check web server logs** for HTTP errors

## Future Enhancements

### Planned Features

1. **Real-time Signal Visualization**
   - Oscilloscope-style display
   - Frequency spectrum analyzer
   - Signal strength monitoring

2. **Advanced Signal Processing**
   - Automatic signal classification
   - Noise reduction algorithms
   - Signal quality metrics

3. **Enhanced Security**
   - HTTPS support
   - Authentication for API access
   - Signal encryption

4. **Mobile Integration**
   - Android/iOS mobile app
   - Bluetooth Low Energy control
   - Push notifications

5. **Cloud Integration**
   - AWS IoT, Google Cloud, Azure IoT support
   - Real-time data streaming

## License

This project is licensed under the Unlicense OR CC0-1.0.
See LICENSE file for details.

## Support

For issues, questions, or contributions:
- **GitHub Issues**: Report bugs, request features
- **Documentation**: Check README.md for detailed information
- **Community**: Join ESP32/Arduino forums for discussion

## Conclusion

The CC1101 443MHz Signal Manager provides a complete solution for RF signal acquisition and management. With its web-based interface and comprehensive API, it offers easy control and monitoring of CC1101 operations. The system is designed to be robust, user-friendly, and extensible for future enhancements.

The implementation includes thorough error handling, proper documentation, and examples to help users get started quickly.
