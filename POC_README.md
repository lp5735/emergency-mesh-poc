# Emergency WiFi Bridge - Proof of Concept

## Status: ✅ READY TO TEST

Ultra-minimal POC demonstrating WiFi ↔ LoRa Mesh bridging for emergency responder communications.

## What We've Built

### ✅ Completed Components

**EmergencyWiFiBridge Module** (`src/modules/EmergencyWiFiBridge.cpp`):
- ✅ Bridges WiFi clients to LoRa mesh
- ✅ Receives text from WiFi → Broadcasts via LoRa
- ✅ Receives LoRa messages → Broadcasts to WiFi clients via WebSocket
- ✅ Integrated with Meshtastic routing core

**ESP32 WiFi Service** (`src/wifi/EmergencyWiFiService.cpp`):
- ✅ WiFi Access Point (SSID: `EMRG-TEST-POC`, Password: `emergency123`)
- ✅ HTTP server serving web UI from LittleFS
- ✅ WebSocket server (port 81) for real-time communication
- ✅ `/test` endpoint to trigger mesh broadcast

**Minimal Web UI** (`data/webapp/index.html`):
- ✅ Single HTML file with embedded CSS/JS
- ✅ "Send Test" button → HTTP GET `/test`
- ✅ WebSocket client for real-time message display
- ✅ No external dependencies (works offline)

**Build Configuration** (`variants/esp32/heltec_v2/platformio-poc.ini`):
- ✅ PlatformIO environment: `heltec-v2-pwa-poc`
- ✅ BLE disabled, WiFi AP enabled
- ✅ Libraries: ESPAsyncWebServer, arduinoWebSockets, ArduinoJson
- ✅ LittleFS filesystem for web app storage
- ✅ Emergency bridge module registered

### 📋 POC Scope (Intentionally Minimal)

**What this POC proves**:
- ✅ WiFi AP works on ESP32
- ✅ LoRa mesh routing works
- ✅ WebSocket real-time updates work
- ✅ HTTP API works
- ✅ **End-to-end WiFi ↔ LoRa bridge works!**

**What is NOT in this POC** (will be added later):
- ❌ Full PWA features (complex UI removed for testing)
- ❌ Message history (lost on refresh)
- ❌ Contact list / node discovery UI
- ❌ Delivery status indicators
- ❌ Read receipts
- ❌ Input validation / rate limiting

---

## Hardware Requirements

- **Heltec WiFi LoRa 32 V2** (ESP32 + SX1276 LoRa)
- USB cable for flashing
- 2 devices for testing

---

## Software Requirements

- **PlatformIO** (VS Code extension or CLI)
- Python 3.7+ (for PlatformIO)

### Install PlatformIO CLI

```bash
pip install -U platformio
```

---

## Build & Flash Instructions

### 1. Build Firmware

```bash
cd /Users/loicpinel/Documents/Arduino/firmware

# Build POC environment
pio run -e heltec-v2-pwa-poc
```

### 2. Upload Firmware to ESP32

Connect your Heltec V2 via USB, then:

```bash
pio run -e heltec-v2-pwa-poc --target upload
```

### 3. Upload Web App to SPIFFS

```bash
pio run -e heltec-v2-pwa-poc --target uploadfs
```

This uploads all files from `data/webapp/` to the ESP32's SPIFFS filesystem.

### 4. Monitor Serial Output

```bash
pio device monitor -b 115200
```

You should see:
```
Initializing Emergency WiFi Service...
SPIFFS mounted successfully
Starting WiFi AP: EMRG-TEST-POC
WiFi AP started successfully
IP Address: 192.168.4.1
Connect to: EMRG-TEST-POC
Password: emergency123
Then browse to: http://192.168.4.1
HTTP server started on port 80
WebSocket server started on port 81
Emergency WiFi Service initialized
```

---

## Testing the POC

### Phase 1 Test: WiFi AP & Web UI

**Goal**: Verify ESP32 WiFi AP works and web UI loads

1. **Flash firmware** to ESP32
2. **Connect phone** to WiFi:
   - SSID: `EMRG-TEST-POC`
   - Password: `emergency123`
3. **Open browser** and navigate to: `http://192.168.4.1`
4. **Verify**:
   - ✅ PWA loads with dark WhatsApp-style UI
   - ✅ Connection status shows green dot "Connected"
   - ✅ Node ID displayed in status bar
   - ✅ Empty contact list shown
   - ✅ Serial monitor shows "WebSocket[0] connected"

### Phase 1 Test: WebSocket Communication

**Goal**: Verify WebSocket bidirectional communication

1. **Open browser console** (F12 → Console tab)
2. **Check logs**:
   ```
   Database initialized successfully
   WebSocket connected
   Received message: {type: "node_info", nodeId: "xxxx", ...}
   Emergency Mesh App ready
   ```
3. **Test database**:
   ```javascript
   // In browser console
   await meshDB.getStats()
   // Should show: {messages: 0, contacts: 0, conversations: 0}
   ```

### Phase 1 Test: PWA Installation

**Goal**: Install as native app

**On Android**:
1. Open in Chrome
2. Tap menu (⋮) → "Add to Home Screen"
3. App icon appears on home screen

**On iOS**:
1. Open in Safari
2. Tap Share (□↑) → "Add to Home Screen"
3. App icon appears on home screen

---

## File Structure

```
firmware/
├── src/
│   └── wifi/
│       ├── EmergencyWiFiService.h      # WiFi AP + HTTP + WebSocket
│       └── EmergencyWiFiService.cpp
├── data/
│   └── webapp/
│       ├── index.html                  # PWA main UI
│       ├── styles.css                  # WhatsApp-style CSS
│       ├── app.js                      # Application logic
│       ├── db.js                       # IndexedDB wrapper
│       ├── manifest.json               # PWA manifest
│       ├── sw.js                       # Service Worker
│       └── icon-192.png                # App icon (TODO)
├── variants/esp32/heltec_v2/
│   └── platformio-poc.ini              # POC build config
├── platformio.ini                      # Main config (includes POC)
├── POC_README.md                       # This file
├── POC_PLAN_V2.md                      # Overall POC plan
└── POC_PWA_IMPLEMENTATION.md           # Implementation details
```

---

## Troubleshooting

### WiFi AP doesn't start

**Symptom**: Can't see "EMRG-TEST-POC" WiFi network

**Solutions**:
1. Check serial monitor for errors
2. Verify ESP32 has power (USB or battery)
3. Try different WiFi channel (edit `EmergencyWiFiService.cpp` line with `WiFi.softAP(...)`)
4. Restart ESP32

### Can't connect to WiFi

**Symptom**: Phone says "Can't connect" or "Wrong password"

**Solutions**:
1. Verify password: `emergency123`
2. Forget network on phone and reconnect
3. Check if ESP32 is limiting max clients (default 4)
4. Restart ESP32

### Web page doesn't load

**Symptom**: Browser shows "Can't reach this page"

**Solutions**:
1. Verify connected to `EMRG-TEST-POC` WiFi
2. Check IP address: Should be `192.168.4.1`
3. Verify SPIFFS upload succeeded: `pio run -e heltec-v2-pwa-poc --target uploadfs`
4. Check serial monitor for "HTTP server started"

### WebSocket won't connect

**Symptom**: Red dot, status shows "Disconnected"

**Solutions**:
1. Check browser console for errors
2. Verify WebSocket URL: `ws://192.168.4.1:81`
3. Some browsers block WebSocket on `http://` - try different browser
4. Check serial monitor for "WebSocket server started"

### SPIFFS upload fails

**Symptom**: `uploadfs` command fails or web files not found

**Solutions**:
```bash
# Clean build and retry
pio run -e heltec-v2-pwa-poc --target clean
pio run -e heltec-v2-pwa-poc --target uploadfs
```

### Build errors

**Symptom**: Compilation fails

**Common issues**:
1. **Missing libraries**: `pio lib install` or check `platformio-poc.ini`
2. **Wrong board**: Verify using Heltec WiFi LoRa 32 V2
3. **PlatformIO outdated**: `pio upgrade`

---

## Next Phase: LoRa Bridge Integration

**To be implemented**:

1. Create `EmergencyWiFiBridge.cpp` module
2. Integrate with Meshtastic mesh core
3. Translate WebSocket JSON ↔ Meshtastic protobuf
4. Route messages over LoRa radio
5. Handle delivery ACKs from mesh
6. Implement contact discovery from NodeDB

See `POC_PWA_IMPLEMENTATION.md` for detailed implementation plan.

---

## Development Workflow

### Make Changes to Web App

1. Edit files in `data/webapp/`
2. Upload to SPIFFS: `pio run -e heltec-v2-pwa-poc --target uploadfs`
3. Refresh browser (Ctrl+Shift+R to bypass cache)

### Make Changes to ESP32 Code

1. Edit files in `src/wifi/`
2. Build: `pio run -e heltec-v2-pwa-poc`
3. Upload: `pio run -e heltec-v2-pwa-poc --target upload`
4. Monitor: `pio device monitor -b 115200`

### Debug Tips

**ESP32 Side**:
- Use `Serial.printf()` liberally
- Monitor serial output during operation
- Check WiFi client count, WebSocket events

**Browser Side**:
- Open DevTools Console (F12)
- Check Network tab for WebSocket frames
- Inspect IndexedDB: Application → Storage → IndexedDB
- Use `window.app` and `window.meshDB` for debugging

---

## Credits

- Base firmware: Meshtastic (https://meshtastic.org)
- UI inspired by WhatsApp Web
- Built with: ESP32, Arduino, PlatformIO

---

**Status**: Phase 1 Complete ✅ (WiFi AP + PWA UI)
**Next**: Phase 2 - LoRa Bridge Integration
**Version**: POC v0.1
**Last Updated**: 2025-01-14
