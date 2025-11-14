# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Emergency Responder Mesh Network** - A specialized fork of Meshtastic firmware designed for emergency response scenarios.

### What This Fork Does Differently

This is a **modified Meshtastic implementation** that replaces the standard BLE + Protobuf client interface with a **WiFi AP + REST/WebSocket** interface for emergency responders.

**Architecture**:
```
[Responder's Phone/Tablet Browser]
           ↕ WiFi (192.168.4.1)
    [ESP32 WiFi Access Point]
      ↕ Bridge Module
   [Meshtastic LoRa Mesh Core]
      ↕ LoRa Radio
    [Other Mesh Nodes]
```

**Key Modifications**:
- **REMOVED**: BLE interface, Protobuf client protocol, standard Meshtastic apps
- **KEPT**: LoRa mesh routing, radio management, encryption, power management
- **ADDED**: WiFi AP mode, REST API, WebSocket real-time, embedded web UI, emergency-specific features

**Why WiFi Instead of BLE?**
- No app installation required - works in any browser
- Multiple simultaneous connections (6+ phones per node)
- Higher bandwidth for forms, images, voice clips
- Standard HTTP/WebSocket protocols
- Easier field debugging and testing

**Base Documentation**: https://meshtastic.org/docs/ (for core mesh concepts only)

## Development Setup

### Building Emergency WiFi Firmware

This project uses PlatformIO for cross-platform builds. **Primary target: Heltec WiFi LoRa 32 V2**.

**IMPORTANT**: Use the **POC build environment** for emergency WiFi development:

```bash
# Install PlatformIO
pip install -U platformio

# Build Emergency WiFi variant (Heltec V2) - POC Configuration
pio run -e heltec_v2_pwa_poc

# Flash to device
pio run -e heltec_v2_pwa_poc --target upload

# Monitor serial output
pio device monitor -b 115200

# Clean build (if needed)
pio run -e heltec_v2_pwa_poc --target clean
```

**Build Configurations**:
- **`heltec_v2_pwa_poc`** (USE THIS): Emergency WiFi Bridge POC with full networking stack
  - Config: `variants/esp32/heltec_v2/platformio-poc.ini`
  - Includes: ESPAsyncWebServer, ArduinoWebSockets, WiFi tuning, Bluetooth disabled
  - Partition: `huge_app.csv` for larger firmware

- **`heltec-v2_0`** (DON'T USE): Basic Meshtastic build - missing WiFi libraries
  - Config: `variants/esp32/heltec_v2/platformio.ini`
  - Too minimal for emergency WiFi features

### Testing

```bash
# Run core mesh tests (crypto, routing)
pio test -e native -f test_crypto
pio test -e native -f test_meshpacket_serializer

# Test WiFi interface (requires device)
# 1. Flash firmware to device
# 2. Connect to "EMRG-NODE-XXXX" WiFi network (password: emergency123)
# 3. Test REST API
curl -X POST http://192.168.4.1/api/send -d "dest=0x1234&msg=Test"
curl http://192.168.4.1/api/nodes
curl http://192.168.4.1/api/status

# 4. Test WebSocket (requires wscat: npm install -g wscat)
wscat -c ws://192.168.4.1:81

# 5. Open web UI in browser
# Navigate to: http://192.168.4.1/
```

### Code Quality

```bash
# Format code (required before commits)
trunk fmt

# Run code checks
trunk check

# Run static analysis
pio check
```

## Architecture Overview

### Emergency WiFi Bridge Architecture

This fork maintains Meshtastic's multi-platform design but focuses on **ESP32 variants** for WiFi AP capabilities.

**Layered Architecture**:
- **Core Mesh Layer** (`src/mesh/`): Unchanged LoRa routing, encryption, radio management
- **Bridge Layer** (`src/modules/EmergencyWiFiBridge.cpp`): **NEW** - WiFi ↔ LoRa packet translation
- **WiFi Service Layer** (`src/wifi/`): **NEW** - WiFi AP, REST API, WebSocket server
- **Web UI Layer** (`src/wifi/WebUI.h`): **NEW** - Embedded HTML/JS interface
- **Platform Layer** (`src/platform/esp32/`): ESP32-specific WiFi and network stack
- **Variant Layer** (`variants/esp32/heltec_v2/`): Heltec V2 board configuration

### What Was Removed

- **BLE Stack** (`src/bluetooth/`): Disabled via `DISABLE_BLUETOOTH` flag
- **Protobuf Client Protocol**: Mesh still uses protobufs internally, but WiFi uses JSON
- **Standard Modules**: Most telemetry modules excluded to save flash for WiFi stack
- **Multi-platform Support**: Focus on ESP32 only (nRF52/RP2040 lack WiFi AP)

### Key Components (Emergency Fork)

#### Mesh Networking (`src/mesh/`) - **UNCHANGED**

Core LoRa mesh functionality preserved from base Meshtastic:
- **RadioInterface**: LoRa radio abstraction (SX126x for Heltec V2)
- **Router**: Message routing with flooding and reliable delivery
  - `FloodingRouter`: Broadcasts to all nodes
  - `ReliableRouter`: ACK-based reliable delivery with retries
- **Channels**: Multi-channel encryption with AES-256
- **CryptoEngine**: Packet encryption/decryption
- **NodeDB**: Mesh topology and node information database

#### Emergency WiFi Bridge (`src/modules/EmergencyWiFiBridge.cpp`) - **NEW**

Core bridge module that translates between WiFi clients and LoRa mesh:

**Key Responsibilities**:
- Accept packets from WiFi clients (via REST/WebSocket)
- Translate to Meshtastic packet format
- Route through mesh using existing `ReliableRouter`
- Receive packets from mesh and broadcast to WiFi clients
- Maintain client connection state

**Packet Format**:
```cpp
struct BridgePacket {
    uint16_t dest_node;    // Destination node ID
    uint16_t src_node;     // Source node ID
    uint16_t msg_id;       // Message ID for tracking
    uint8_t  msg_type;     // TEXT, SOS, FORM, VOICE
    uint8_t  chunk_info;   // For multi-packet messages
    uint8_t  payload[32];  // Message payload
    uint16_t crc;          // Error detection
} __attribute__((packed));
```

#### WiFi Service Layer (`src/wifi/`) - **NEW**

Three main components:

1. **EmergencyWiFiService.cpp**: WiFi AP management
   - Creates access point "EMRG-NODE-XXXX" (last 4 MAC digits)
   - IP: 192.168.4.1/24
   - Supports 6+ simultaneous phone connections
   - Client authentication and session management

2. **APIHandlers.cpp**: REST endpoints
   - `POST /api/send` - Send message to mesh
   - `GET /api/nodes` - Get mesh topology (JSON)
   - `GET /api/status` - Node status (battery, signal, etc.)
   - `POST /api/voice` - Upload voice clip chunks
   - `GET /` - Serve web UI

3. **WebSocketServer.cpp**: Real-time bidirectional communication
   - Port 81
   - Broadcasts incoming mesh messages to all clients
   - Accepts outgoing messages from clients
   - Binary mode for voice data

#### Web UI (`src/wifi/WebUI.h`) - **NEW**

Embedded single-page HTML/CSS/JS application:
- **No external dependencies** - works offline
- **Responsive design** - mobile-first for phone/tablet use
- **Features**:
  - Message compose and send
  - Real-time message feed (WebSocket)
  - Node list with signal strength
  - SOS quick-send button
  - Voice recording (MediaRecorder API)
  - Form submission (incident reports)

Served from ESP32 flash, no SD card required.

#### Modified Modules System

**KEPT**:
- `RoutingModule`: Core routing (required)
- `AdminModule`: Remote config (useful for field adjustments)

**REMOVED** (to save flash for WiFi stack):
- `TextMessageModule`: Replaced by WiFi bridge
- `PositionModule`: GPS optional for emergency use
- `StoreForwardModule`: Not needed with WiFi bridge
- Most telemetry modules

#### Power Management - **MODIFIED**

Enhanced for WiFi AP power consumption:

```cpp
// Power states adapted for WiFi
// ON (WiFi AP active) → STANDBY (WiFi off, LoRa listening) → DEEP_SLEEP
```

**Key Modifications**:
- WiFi AP automatically disabled after 5 min of no client connections
- Button press or incoming LoRa message wakes WiFi AP
- Display shows WiFi status and connected client count
- Battery monitoring critical due to higher WiFi power draw

**Power Consumption**:
- WiFi AP active: ~240mA
- WiFi AP idle (no clients): ~120mA
- LoRa RX only: ~15mA
- Deep sleep: ~5mA

#### Display System (`src/graphics/`) - **SIMPLIFIED**

Heltec V2 OLED display shows:
- WiFi SSID and connection status
- Number of connected phones
- Mesh node count
- Battery level
- Last message indicator

**Removed**: Complex UI framework, most graphic screens

#### Protocol System - **HYBRID**

Two protocol layers:

1. **WiFi ↔ Phone**: JSON over REST/WebSocket
   - No protobufs on WiFi side
   - Simple JSON messages for browser compatibility
   - Example: `{"type":"send","dest":"0x1234","msg":"Help needed"}`

2. **LoRa Mesh ↔ LoRa Mesh**: Protobufs (internal, unchanged)
   - Mesh routing still uses Meshtastic protobufs
   - Generated code in `src/mesh/generated/` (don't edit)
   - Bridge translates JSON ↔ Protobuf

### Configuration System - **SIMPLIFIED**

Emergency-specific compile-time configuration in `src/configuration.h`:

```cpp
// Emergency WiFi Bridge Configuration
#define DISABLE_BLUETOOTH 1
#define ENABLE_WIFI_AP 1
#define DISABLE_MESHTASTIC_PROTO 1
#define ENABLE_EMERGENCY_BRIDGE 1

// WiFi AP Settings
#define WIFI_AP_SSID "EMRG-NODE-%04X"  // Last 4 MAC digits
#define WIFI_AP_PASS "emergency123"
#define WIFI_MAX_CLIENTS 6
#define WIFI_CHANNEL 6
#define WIFI_HIDDEN false

// Emergency Features
#define ENABLE_VOICE_CLIPS 1
#define ENABLE_SOS_BUTTON 1
#define ENABLE_INCIDENT_FORMS 1
```

**Runtime Config**: Minimal - most settings are compile-time for reliability

## Common Development Tasks (Emergency Fork)

### Adding WiFi API Endpoints

1. Add handler function in `src/wifi/APIHandlers.cpp`
2. Register route in `EmergencyWiFiService::setupHTTPEndpoints()`
3. Update web UI (`src/wifi/WebUI.h`) to call new endpoint
4. Test with curl before browser testing

Example:
```cpp
// In APIHandlers.cpp
void handleNewEndpoint(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["status"] = "ok";
    doc["data"] = getSomeData();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

// In EmergencyWiFiService.cpp
void setupHTTPEndpoints() {
    httpServer.on("/api/new", HTTP_GET, handleNewEndpoint);
}
```

### Adding WebSocket Message Types

1. Define message type in `BridgePacket` struct
2. Add handler in `EmergencyWiFiService::handleWSMessage()`
3. Update bridge logic in `EmergencyWiFiBridge::onWiFiPacket()`
4. Update web UI JavaScript to send/receive new type

### Modifying Web UI

Web UI is embedded in `src/wifi/WebUI.h` as a C++ raw string literal:
```cpp
const char* EMERGENCY_UI = R"(
<!DOCTYPE html>
<html>
...
</html>
)";
```

**Development workflow**:
1. Edit HTML/CSS/JS in external file for syntax highlighting
2. Copy into raw string literal in WebUI.h
3. Rebuild firmware
4. Flash and test in browser

**Tip**: Use browser dev tools while connected to `http://192.168.4.1/`

### Adjusting Power Management

WiFi AP power consumption is significant. Modify timeouts in `PowerFSM.cpp`:

```cpp
// Time before WiFi AP shuts down (no clients)
#define WIFI_IDLE_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes

// Time before deep sleep (WiFi already off)
#define SLEEP_TIMEOUT_MS (15 * 60 * 1000)     // 15 minutes
```

### Flash Memory Optimization

ESP32 has 4MB flash but WiFi stack is large. Save space:

1. **Disable unused modules** in `platformio.ini`:
   ```ini
   -DMESHTASTIC_EXCLUDE_ENVIRONMENTAL=1
   -DMESHTASTIC_EXCLUDE_DETECTION_SENSOR=1
   ```

2. **Minimize web UI size**:
   - Remove comments from HTML/CSS/JS
   - Minify JavaScript
   - Use short variable names
   - No external libraries

3. **Check flash usage**:
   ```bash
   pio run -e heltec-v2-emergency-wifi -t size
   ```

### Debugging WiFi Issues

```cpp
// Enable WiFi debug in src/platform/esp32/architecture.h
#define WIFI_DEBUG 1

// Check logs
pio device monitor -b 115200 --filter esp32_exception_decoder
```

Common issues:
- **Can't connect to AP**: Check WiFi channel (some phones don't support ch 12-14)
- **Slow response**: Reduce `WIFI_MAX_CLIENTS` or increase task priority
- **Disconnects**: Check power supply - WiFi needs stable 3.3V at 500mA+

## Important Notes (Emergency Fork)

- **This is a Fork**: Not intended for upstream Meshtastic - emergency use case specific
- **ESP32 Only**: WiFi AP requires ESP32 - nRF52/RP2040 not supported
- **Code Formatting**: Run `trunk fmt` before committing
- **Mesh Core Intact**: Don't modify `src/mesh/` unless absolutely necessary - routing is proven
- **Threading**: WiFi runs on separate task from LoRa - use mutexes for shared state
- **Airtime Limits**: LoRa airtime is regulated - respect duty cycle limits in `airtime.cpp`
- **Power Budget**: WiFi AP draws 15x more power than LoRa RX - battery life is limited
- **Testing**: Always test with real phones and multiple clients, not just curl

## Next Steps for Implementation

See `DESIGN.md` for detailed component specifications and implementation plan.
