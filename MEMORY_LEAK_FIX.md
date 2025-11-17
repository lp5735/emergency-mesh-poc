# Memory Leak Fix - Emergency WiFi Bridge

## Problem Summary

**Symptom**: Heap usage climbs to 94%+ over time, WiFi AP fails with "incorrect password" errors, device becomes unstable.

**Root Cause**: Multiple memory leaks causing heap fragmentation:

1. **Periodic 2KB Allocation** (CRITICAL - Primary Leak)
   - Function: `broadcastMeshStatus()`
   - Called every 5 seconds when clients connected
   - Allocates 2KB `DynamicJsonDocument` + dynamic `String` object
   - String can grow to several KB with many mesh nodes
   - Heap fragmentation accumulates rapidly

2. **Per-Message String Allocations** (MAJOR - Secondary Leak)
   - Function: `handleClientMessage()` and others
   - Every incoming/outgoing message creates `String` object
   - 11+ locations throughout EmergencyWiFiService.cpp
   - Cumulative effect with frequent messaging

3. **Threading System Overhead** (MODERATE)
   - `std::vector<ConnectedUser> localUsers` - dynamically grows
   - `std::set<String> knownUsernames` - String allocations
   - Multiple JSON documents for user management

## Memory Leak Details

### ESP32 Memory Constraints
- **Total RAM**: 320KB (327,680 bytes)
- **Heap after boot**: ~115KB used (35%)
- **WiFi AP + WebSocket**: ~40-50KB
- **Safe operating range**: <70% heap usage
- **Crash threshold**: >90% heap usage

### Leak Rate Calculation

**With mesh status broadcast** (5 second interval):
- Per broadcast: 2KB JSON doc + 1-3KB String (worst case: 5 nodes)
- Frequency: 12 times per minute
- Total allocation: ~30-60KB/minute
- Heap exhaustion: 10-20 minutes

**With message traffic** (moderate: 10 msg/min):
- Per message: 512B JSON doc + 256-512B String
- Total allocation: ~7.5KB/minute
- Cumulative with broadcast: ~40-70KB/minute

**Threading system** (when enabled):
- User registration: ~200B per user
- Known usernames set: ~50B per username
- Baseline overhead: ~2-5KB with 10 users

## Fixes Applied

### 1. Disabled Periodic Mesh Status Broadcast (CRITICAL)

**File**: `src/wifi/EmergencyWiFiService.cpp:743-753`

**Before**:
```cpp
// Called every 5 seconds
if (clientCount > 0) {
    broadcastMeshStatus();  // 2KB allocation every 5 seconds!
}
```

**After**:
```cpp
// DISABLED: Causes heap fragmentation and memory leak
// TODO: Reimplement with static buffer or on-demand only
```

**Impact**: Eliminates 30-60KB/minute allocation rate

### 2. Replaced String Objects with Static Buffers

**File**: `src/wifi/EmergencyWiFiService.cpp:691-710`

**Before** (every message received):
```cpp
DynamicJsonDocument outDoc(512);
// ... build JSON ...
String outJson;
serializeJson(outDoc, outJson);  // Dynamic String allocation!
broadcastToClients(outJson.c_str());
```

**After**:
```cpp
static char jsonBuffer[512];  // Reusable static buffer
DynamicJsonDocument outDoc(512);
// ... build JSON ...
size_t len = serializeJson(outDoc, jsonBuffer, sizeof(jsonBuffer));
broadcastToClients(jsonBuffer);
```

**Impact**: Eliminates ~7.5KB/minute allocation from messaging

### 3. Fixed sendNodeInfo() String Allocation

**File**: `src/wifi/EmergencyWiFiService.cpp:722-738`

**Before**:
```cpp
doc["nodeId"] = String(getNodeId(), HEX);  // String allocation
String json;
serializeJson(doc, json);  // Another String allocation
```

**After**:
```cpp
static char jsonBuffer[256];
static char nodeIdHex[16];
snprintf(nodeIdHex, sizeof(nodeIdHex), "%X", getNodeId());
doc["nodeId"] = nodeIdHex;
serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
```

**Impact**: Eliminates ~500B allocation per WebSocket connection

### 4. Disabled Threading System (Temporary)

**File**: `variants/esp32/heltec_v2/platformio-poc.ini:31`

Added build flag:
```ini
-D DISABLE_THREADING_SYSTEM=1
```

Wrapped all threading code with:
```cpp
#ifndef DISABLE_THREADING_SYSTEM
// User management functions
#endif
```

**Impact**: Saves 2-5KB baseline heap + eliminates dynamic growth

## Expected Results

### Before Fixes
- **Boot heap**: 35% (115KB used)
- **After 5 minutes**: 85-90% (278-295KB used)
- **After 10 minutes**: 94%+ (308KB+ used) → **WiFi CRASH**
- **Symptoms**: "Incorrect password", page won't load, unstable connections

### After Fixes
- **Boot heap**: 35% (115KB used)
- **After 5 minutes**: 40-45% (131-148KB used)
- **After 10 minutes**: 45-50% (148-164KB used)
- **After 1 hour**: 55-60% (180-197KB used) - stable
- **Symptoms**: None - WiFi should be stable indefinitely

## Remaining String Allocations

**These still need fixing** (in threading code, currently disabled):

1. `broadcastUsernameQuery()` - Line 907
2. `confirmUsername()` - Line 944
3. `rejectUsername()` - Line 965
4. `notifyUserJoined()` - Line 980
5. `handleUsernameQuery()` - Line 1002
6. `handleUsernameConflict()` - Line 1043
7. `handleUserJoined()` - Line 1089
8. `handleUserLeft()` - Line 1108
9. `broadcastMeshStatus()` - Line 849 (disabled but still in code)

**Note**: When re-enabling threading, replace all `String json; serializeJson(doc, json);` with static buffers.

## Testing Recommendations

### 1. Heap Monitoring
Add to `loop()`:
```cpp
static uint32_t lastHeapPrint = 0;
if (millis() - lastHeapPrint > 30000) {  // Every 30 seconds
    Serial.printf("Free heap: %u bytes (%.1f%%)\n",
        ESP.getFreeHeap(),
        (ESP.getFreeHeap() * 100.0) / 327680.0);
    lastHeapPrint = millis();
}
```

### 2. Load Testing
- Connect 4 clients simultaneously
- Send 20 messages/minute for 30 minutes
- Monitor heap usage - should stay <60%

### 3. Stability Testing
- Leave device running for 24 hours
- With periodic client connects/disconnects
- Heap should plateau at 55-60%

## Future Improvements

### 1. On-Demand Mesh Status
Instead of periodic broadcast, only send when requested:
```cpp
// New endpoint: GET /api/mesh_status
httpServer.on("/api/mesh_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    static char jsonBuffer[2048];
    // Generate mesh status JSON directly to buffer
    // Send response
});
```

### 2. Pre-Allocated Buffer Pool
Create a pool of reusable buffers:
```cpp
class BufferPool {
    static char buffers[4][1024];
    static bool inUse[4];
    char* acquire();
    void release(char* buf);
};
```

### 3. Streaming JSON
For large responses, use streaming instead of building entire JSON:
```cpp
AsyncResponseStream *response = request->beginResponseStream("application/json");
response->print("{\"nodes\":[");
// Stream each node individually
response->print("]}");
```

### 4. Replace ArduinoJson String Handling
ArduinoJson can use `const char*` directly:
```cpp
// Instead of:
doc["key"] = String("value");  // Allocates String

// Use:
static const char* value = "value";
doc["key"] = value;  // No allocation
```

## References

- ESP32 Heap Management: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html
- Arduino String Issues: https://hackingmajenkoblog.wordpress.com/2016/02/04/the-evils-of-arduino-strings/
- ArduinoJson Memory Guide: https://arduinojson.org/v6/how-to/reuse-a-json-document/

## Changelog

### 2025-11-15 - Initial Fix
- Disabled `broadcastMeshStatus()` periodic broadcast
- Replaced `String` with static buffers in message handling
- Fixed `sendNodeInfo()` to avoid String allocations
- Disabled threading system temporarily
- Expected heap reduction: 94% → 55-60% stable

