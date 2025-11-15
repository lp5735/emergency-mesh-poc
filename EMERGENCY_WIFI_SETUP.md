# Emergency WiFi + WebServer Setup Guide

## Complete Integration of WiFi AP Mode with Meshtastic WebServer

This guide shows how to combine your WiFi AP with Meshtastic's existing HTTP WebServer for the emergency UI.

---

## Architecture Overview

```
[Responder Phone Browser]
    ↓ WiFi: EMRG-NODE-XXXX
    ↓ http://192.168.4.1/static/emergency.html
[ESP32 WiFi AP Mode] ← Your WiFiMeshBridge
    ↓
[Meshtastic HTTP WebServer] ← Existing code
    ↓ Endpoints:
    ↓   GET /json/nodes → Contact list
    ↓   GET /api/v1/fromradio → Receive messages
    ↓   PUT /api/v1/toradio → Send messages
    ↓
[PhoneAPI (TYPE_HTTP)] ← Existing code
    ↓
[Meshtastic LoRa Mesh] ← Existing code
```

**Key Insight**: Zero custom backend code needed! Just WiFi AP + existing WebServer + UI.

---

## Step 1: Build Configuration

### File: `platformio.ini`

Add emergency WiFi build environment:

```ini
[env:heltec-v2-emergency]
extends = env:heltec-v2.1

build_flags =
  ${env:heltec-v2.1.build_flags}

  ; Enable WiFi AP
  -DENABLE_WIFI_AP=1
  -DHAS_WIFI=1

  ; Keep WebServer enabled (DON'T exclude it!)
  ; -DMESHTASTIC_EXCLUDE_WEBSERVER=0  ; Already default

  ; Optional: Disable BLE to save memory for WiFi
  -DDISABLE_BLUETOOTH=1

  ; Debug
  -DDEBUG_WIFI=1

lib_deps =
  ${env:heltec-v2.1.lib_deps}

src_filter =
  ${env.src_filter}
  +<src/wifi/WiFiMeshBridge.cpp>
  +<src/wifi/WiFiMeshBridge.h>
```

---

## Step 2: Main Startup Integration

### File: `src/main.cpp`

Add WiFi AP initialization before the main loop:

```cpp
#include "configuration.h"

// ... existing includes ...

#ifdef ENABLE_WIFI_AP
#include "wifi/WiFiMeshBridge.h"
#endif

void setup() {
    // ... existing Meshtastic setup ...

    #ifdef ENABLE_WIFI_AP
    // Initialize WiFi AP Mode
    Serial.println("\n=== Emergency WiFi Mode ===");
    wifiMeshBridge.init();
    // This will:
    //   1. Start WiFi AP (EMRG-NODE-XXXX)
    //   2. Start Meshtastic WebServer
    //   3. Serve emergency.html at /static/emergency.html
    #endif

    // ... rest of setup ...
}

void loop() {
    // ... existing Meshtastic loop ...

    #ifdef ENABLE_WIFI_AP
    wifiMeshBridge.loop();
    #endif
}
```

---

## Step 3: WiFi AP Configuration

Your existing `WiFiMeshBridge.cpp` now:

1. **Creates WiFi AP**:
   - SSID: `EMRG-NODE-<MAC>`
   - Password: `emergency123`
   - IP: `192.168.4.1/24`
   - Max clients: 8

2. **Starts WebServer** automatically (via `initWebServer()`)

3. **Serves** the emergency UI from filesystem

---

## Step 4: Upload Emergency UI to Filesystem

### Create filesystem structure:

```
data/
└── static/
    └── emergency.html    ← Your UI file (already created!)
```

### Upload to ESP32:

```bash
# Build filesystem image and upload
pio run --target uploadfs -e heltec-v2-emergency

# Or manually via WebServer (if already running):
# http://192.168.4.1/upload
```

---

## Step 5: Build and Flash

```bash
# Clean build
pio run --target clean -e heltec-v2-emergency

# Build firmware
pio run -e heltec-v2-emergency

# Flash to device
pio run --target upload -e heltec-v2-emergency

# Monitor serial output
pio device monitor -b 115200
```

**Expected Serial Output**:

```
Initializing WiFi Mesh Bridge...
Starting WiFi AP: EMRG-NODE-A1B2
MAC Address: XX:XX:XX:XX:A1:B2
WiFi AP started successfully
IP Address: 192.168.4.1
WiFi Mesh Bridge initialized
Starting Meshtastic WebServer...
WebServer started on http://192.168.4.1
Emergency UI: http://192.168.4.1/static/emergency.html
*** Ready for emergency connections! ***
```

---

## Step 6: Connect and Test

### From Phone:

1. **Connect to WiFi**:
   - SSID: `EMRG-NODE-XXXX`
   - Password: `emergency123`

2. **Open Browser**:
   - URL: `http://192.168.4.1/static/emergency.html`

3. **Test Features**:
   - ✅ See contact list (from `/json/nodes`)
   - ✅ Select a contact
   - ✅ Type and send message
   - ✅ Press SOS button

---

## How It Works

### Contact List

```javascript
// Frontend calls:
fetch('/json/nodes')

// Meshtastic returns:
{
  "status": "ok",
  "data": {
    "nodes": [
      {
        "id": "!12345678",
        "long_name": "Node Alpha",
        "short_name": "Alpha",
        "snr": 8.5,
        "last_heard": 1234567890,
        "via_mqtt": false,
        "position": {...}
      },
      ...
    ]
  }
}
```

### Send Message (Simplified)

For now, the UI shows messages locally. To actually send via mesh, you need to:

1. **Encode protobuf** (requires protobuf.js or similar)
2. **PUT to /api/v1/toradio** with binary protobuf data

**Future Enhancement**: Add a simplified JSON endpoint wrapper (optional).

### Receive Messages

Poll `/api/v1/fromradio` every 2 seconds for new protobuf messages.

**Future Enhancement**: Add WebSocket support for real-time push.

---

## Network Flow Diagram

```
┌─────────────────┐
│ Phone Browser   │
│ 192.168.4.2     │
└────────┬────────┘
         │ WiFi
         ▼
┌─────────────────┐
│ ESP32 WiFi AP   │
│ 192.168.4.1     │
├─────────────────┤
│ HTTP Server     │
│  - Port 80      │
│  - Port 443     │
└────────┬────────┘
         │
         ├─► GET /static/emergency.html → Serve UI
         ├─► GET /json/nodes → NodeDB contacts
         ├─► PUT /api/v1/toradio → PhoneAPI
         └─► GET /api/v1/fromradio → PhoneAPI

         ▼
┌─────────────────┐
│ PhoneAPI        │
│ (TYPE_HTTP)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ LoRa Mesh       │
│ RadioInterface  │
└─────────────────┘
```

---

## Troubleshooting

### WebServer not starting

**Check**:
```cpp
#if !MESHTASTIC_EXCLUDE_WEBSERVER
```

Make sure `MESHTASTIC_EXCLUDE_WEBSERVER` is NOT defined in platformio.ini.

### Can't access http://192.168.4.1

**Verify**:
1. Phone connected to `EMRG-NODE-XXXX`
2. Phone IP: Should be `192.168.4.2` or similar
3. Serial output shows: `WebServer started on http://192.168.4.1`

**Test**:
```bash
# From phone (in terminal app or browser):
ping 192.168.4.1
curl http://192.168.4.1/json/nodes
```

### UI loads but no contacts

**Check**:
1. Other LoRa nodes are powered on
2. Wait 60 seconds for node discovery
3. Check `/json/nodes` directly in browser

### Messages not sending

**Current Status**:
- UI shows messages locally (no protobuf encoding yet)
- To actually send, need to add protobuf.js library

**Quick Fix**:
Add this to emergency.html `<head>`:
```html
<script src="https://cdn.jsdelivr.net/npm/protobufjs@7/dist/protobuf.min.js"></script>
```

Then implement proper protobuf encoding (see next steps).

---

## File Checklist

✅ Files you've created/modified:

1. `data/static/emergency.html` ✅ Created
2. `src/wifi/WiFiMeshBridge.cpp` ✅ Modified (added WebServer init)
3. `src/wifi/WiFiMeshBridge.h` ✅ Existing
4. `platformio.ini` ⏳ Need to add `[env:heltec-v2-emergency]`
5. `src/main.cpp` ⏳ Need to add WiFi AP init call

---

## Next Steps

### Phase 1: Get it working (Today!)

1. ✅ Emergency UI created
2. ✅ WiFi AP + WebServer integration shown
3. ⏳ Add build config to platformio.ini
4. ⏳ Add init call in main.cpp
5. ⏳ Build and flash
6. ⏳ Upload filesystem
7. ⏳ Test!

### Phase 2: Message Exchange (Next)

1. Add protobuf.js to emergency.html
2. Implement proper protobuf encoding for `/api/v1/toradio`
3. Implement protobuf decoding for `/api/v1/fromradio`
4. Test end-to-end message delivery

### Phase 3: Polish (Future)

1. Add WebSocket for real-time (extend existing WebServer)
2. Add message persistence (localStorage in browser)
3. Add delivery receipts
4. Add voice/form endpoints (optional)

---

## Summary

**You DON'T need custom backend code!**

✅ WiFi AP: Your `WiFiMeshBridge.cpp` (30 lines)
✅ HTTP Server: Meshtastic's existing WebServer (0 new lines)
✅ Message API: Meshtastic's existing PhoneAPI (0 new lines)
✅ Contact List: Meshtastic's existing NodeDB (0 new lines)
✅ Emergency UI: Your `emergency.html` (1 file)

**Total custom code**: ~30 lines C++ + 1 HTML file!

Everything else leverages existing, proven Meshtastic infrastructure.
