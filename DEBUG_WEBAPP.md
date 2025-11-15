# Emergency WiFi Bridge - Web App Testing Guide

**Status**: Fully functional WebSocket-based web UI with local WiFi chat + LoRa mesh relay

## What This Does

The Emergency WiFi Bridge creates a WiFi Access Point on the ESP32 and serves an interactive web UI that allows:

1. **Local WiFi Chat**: Messages sent from any phone are broadcast to all phones connected to the same AP
2. **LoRa Mesh Relay**: Messages are also forwarded to the LoRa mesh network for distant nodes
3. **Real-time Updates**: WebSocket connection provides instant bidirectional communication
4. **Mobile-Responsive**: Works on phones, tablets, and laptops (no app installation required)

**Architecture**:
```
[Phone A Browser] ←→ [WiFi AP 192.168.4.1] ←→ [Phone B Browser]
                            ↕
                      [LoRa Mesh]
                            ↕
                    [Other Mesh Nodes]
```

## Quick Start

### 1. Build and Flash Firmware

```bash
# Clean any previous build artifacts
pio run -e heltec_v2_pwa_poc --target clean

# Build and upload to ESP32
pio run -e heltec_v2_pwa_poc --target upload

# Monitor serial output
platformio device monitor --environment heltec-v2_0
```

### 2. Connect to WiFi AP

1. ESP32 creates WiFi network: **EMRG-NODE-XXXX** (last 4 digits of MAC address)
2. Password: **OPEN** (no password required for emergency access)
3. Connect your phone/laptop to this network

### 3. Access Web UI

Open browser and navigate to: **http://192.168.4.1/**

You should see:
- Green terminal-style interface
- Connection status indicator
- Message log with timestamps
- Sent/Received counters
- Text input box

## Testing Phone-to-Phone Chat

### Test 1: Local WiFi Chat (2 phones on same AP)

**Goal**: Verify that messages between phones connected to the same WiFi AP work

1. **Phone A**: Connect to EMRG-NODE-XXXX
2. **Phone B**: Connect to EMRG-NODE-XXXX
3. **Phone A**: Open http://192.168.4.1/ in browser
4. **Phone B**: Open http://192.168.4.1/ in browser
5. **Phone A**: Type "Hello from Phone A" and press Enter
6. **Expected Result**:
   - Phone A shows: "Sent: Hello from Phone A" (yellow)
   - Phone B shows: "WiFi: Hello from Phone A (from local client)" (green)
7. **Phone B**: Type "Hi from Phone B" and press Enter
8. **Expected Result**:
   - Phone B shows: "Sent: Hi from Phone B" (yellow)
   - Phone A shows: "WiFi: Hi from Phone B (from local client)" (green)

**Serial Console** (if monitoring):
```
WebSocket[0] connected from 192.168.4.2
Handling message from client 0: {"text":"Hello from Phone A","timestamp":...}
Message text: Hello from Phone A
Broadcasted to WiFi clients: {"type":"message","from":"local","text":"Hello from Phone A",...}
Also sent to LoRa mesh
```

### Test 2: LoRa Mesh Relay (2 ESP32 devices)

**Goal**: Verify that messages are relayed between WiFi clients via LoRa mesh

**Setup**:
- ESP32 Device A: Running emergency WiFi firmware (EMRG-NODE-A)
- ESP32 Device B: Running emergency WiFi firmware (EMRG-NODE-B)
- Both devices on same LoRa channel

**Test Steps**:
1. **Phone A**: Connect to EMRG-NODE-A WiFi
2. **Phone B**: Connect to EMRG-NODE-B WiFi
3. **Phone A**: Open http://192.168.4.1/ in browser
4. **Phone B**: Open http://192.168.4.1/ in browser
5. **Phone A**: Send message "Test LoRa relay"
6. **Expected Result**:
   - Phone A shows: "Sent: Test LoRa relay" (yellow)
   - Phone B shows: "LoRa: Test LoRa relay (from: XXXX, RSSI: -XX dBm)" (green)

**Serial Console** (Device A):
```
Sending to LoRa mesh: Test LoRa relay
EmergencyWiFiBridge: Text message sent to mesh
```

**Serial Console** (Device B):
```
EmergencyWiFiBridge: Received message from mesh, from=0xXXXX, size=XX
EmergencyWiFiBridge: Message text: Test LoRa relay
EmergencyWiFiBridge: Broadcasted to WiFi clients: {"type":"message","from":"XXXX","text":"Test LoRa relay",...}
```

## Web UI Features

### Connection Status
- **Green**: "Connected to Emergency Mesh" - WebSocket active
- **Red**: "Disconnected - reconnecting..." - Connection lost, auto-retry in 3s

### Message Log
- **Yellow**: Sent messages from this device
- **Green (WiFi)**: Messages from other WiFi clients on same AP
- **Green (LoRa)**: Messages from LoRa mesh (includes sender ID and RSSI)
- **Blue**: System info messages
- **Red**: Error messages

### Statistics
- **SENT**: Count of messages sent from this device
- **RECEIVED**: Count of messages received (WiFi + LoRa)

### Message Input
- Type message in text box
- Press Enter or click "Send Message" button
- Message is sent via WebSocket to ESP32
- ESP32 broadcasts to all WiFi clients AND forwards to LoRa mesh

## Technical Details

### WebSocket Protocol

**Connection**: `ws://192.168.4.1:81`

**Client → Server (Send Message)**:
```json
{
  "text": "Hello world",
  "timestamp": 1234567890
}
```

**Server → Client (Received Message)**:
```json
{
  "type": "message",
  "from": "local",          // or node ID for LoRa messages
  "text": "Hello world",
  "timestamp": 12345,
  "rssi": 0,                // WiFi messages have 0 RSSI
  "snr": 0,                 // WiFi messages have 0 SNR
  "source": "wifi"          // or "lora"
}
```

**Server → Client (Node Info)**:
```json
{
  "type": "node_info",
  "nodeId": "B5A0",
  "name": "Test Node",
  "timestamp": 12345
}
```

### HTTP Endpoints (Legacy)

Still available for debugging:

- `GET /` - Serves embedded web UI (HTML/CSS/JS)
- `GET /send?msg=hello` - Send text message to mesh
- `GET /test` - Quick test endpoint

Example:
```bash
# From laptop connected to EMRG-NODE-XXXX
curl "http://192.168.4.1/send?msg=Hello%20from%20curl"
# Response: "Sent: Hello from curl"
```

### WiFi AP Configuration

**SSID**: `EMRG-NODE-XXXX` (last 4 MAC digits)
**Password**: None (open network)
**IP Address**: `192.168.4.1`
**Channel**: 6 (configurable in platformio-poc.ini)
**Max Clients**: 6+ simultaneous connections

### Build Configuration

File: [`variants/esp32/heltec_v2/platformio-poc.ini`](variants/esp32/heltec_v2/platformio-poc.ini)

**Key Flags**:
```ini
-D ENABLE_WIFI_AP=1           # Enable WiFi AP mode
-D ENABLE_EMERGENCY_BRIDGE=1  # Enable emergency WiFi bridge module
-D MESHTASTIC_EXCLUDE_BLUETOOTH=1  # Disable BLE (conflicts with WiFi AP)
-D MESHTASTIC_EXCLUDE_GPS=1   # Disable GPS to save memory
```

**WiFi Network Tuning**:
```ini
-D CONFIG_LWIP_MAX_ACTIVE_TCP=32
-D CONFIG_LWIP_MAX_SOCKETS=16
-D MEMP_NUM_TCP_PCB=20
```

**Libraries**:
- `ESPAsyncWebServer` - HTTP server
- `WebSockets` - WebSocket server
- `ArduinoJson` - JSON parsing/serialization

## Troubleshooting

### Can't Connect to WiFi AP

**Problem**: EMRG-NODE-XXXX network doesn't appear

**Solutions**:
1. Check serial console for "WiFi AP started" message
2. Verify WiFi AP is enabled in platformio-poc.ini
3. Some phones don't support WiFi channel 12-14 (change to channel 6)
4. Check power supply - WiFi AP needs stable 3.3V at 500mA+

### Web UI Doesn't Load

**Problem**: http://192.168.4.1/ shows "Unable to connect"

**Solutions**:
1. Verify you're connected to EMRG-NODE-XXXX network
2. Ping 192.168.4.1 to test connectivity
3. Check serial console for "HTTP server started on port 80"
4. Try clearing browser cache

### WebSocket Won't Connect

**Problem**: Status shows "Disconnected - reconnecting..."

**Solutions**:
1. Check JavaScript console for errors (F12 in browser)
2. Verify WebSocket server started (serial: "WebSocket server started on port 81")
3. Some corporate/hotel WiFi networks block WebSocket (try different network)
4. Check firewall settings

### Messages Not Appearing on Other Phone

**Problem**: Phone A sends message but Phone B doesn't receive it

**WiFi Chat Debugging**:
1. Check both phones are connected to same EMRG-NODE-XXXX network
2. Check serial console for "Broadcasted to WiFi clients" message
3. Verify both phones have WebSocket connected (green status)
4. Check JavaScript console for errors

**LoRa Relay Debugging**:
1. Check serial console for "Also sent to LoRa mesh" on sender device
2. Check serial console for "Received message from mesh" on receiver device
3. Verify LoRa settings match (channel, region, spreading factor)
4. Check LoRa antenna is connected
5. Devices should be within LoRa range (typically 2-5 km line-of-sight)

### High Power Consumption

**Problem**: Battery drains quickly

**Solutions**:
1. WiFi AP mode uses ~240mA (15x more than LoRa RX)
2. WiFi AP auto-shuts down after 5 min of no clients
3. Reduce max clients from 6 to 4
4. Use larger battery or solar charging

## Files Modified

### Core Implementation

1. **[src/wifi/EmergencyWiFiService.cpp](src/wifi/EmergencyWiFiService.cpp)**
   - HTTP server with embedded web UI (lines 125-275)
   - WebSocket server on port 81
   - Phone-to-phone chat broadcast (lines 336-382)
   - LoRa mesh relay integration

2. **[src/modules/EmergencyWiFiBridge.cpp](src/modules/EmergencyWiFiBridge.cpp)**
   - Message send/receive for LoRa mesh
   - WebSocket broadcast for incoming LoRa messages

3. **[variants/esp32/heltec_v2/platformio-poc.ini](variants/esp32/heltec_v2/platformio-poc.ini)**
   - Build configuration and WiFi tuning

### Documentation

- **[CLAUDE.md](CLAUDE.md)** - Developer guide for Claude Code
- **[EMERGENCY_WIFI_DESIGN.md](EMERGENCY_WIFI_DESIGN.md)** - Architecture overview
- **[DEBUG_WEBAPP.md](DEBUG_WEBAPP.md)** - This file

## Next Steps

1. **Add Voice Clips**: Record audio on phone, chunk into 32-byte packets, send via WebSocket
2. **Add Forms**: Incident report forms with structured data
3. **Add SOS Button**: One-click emergency broadcast with GPS coordinates
4. **Optimize Power**: Implement WiFi sleep mode when no clients
5. **Add Authentication**: Optional password protection for WiFi AP
6. **Add Mesh Topology**: Visualize connected nodes and signal strength

## Credits

Based on Meshtastic firmware (https://meshtastic.org/)
Modified for emergency responder use case with WiFi AP + WebSocket interface
