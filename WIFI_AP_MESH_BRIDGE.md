# WiFi AP Mesh Bridge - Implementation Guide

## Overview

This document describes the WiFi AP + LoRa mesh bridge implementation for emergency P2P communications.

**Goal**: Enable P2P apps (Softros LAN Messenger, Jami, etc.) to communicate across multiple nodes via LoRa mesh, using standard WiFi connectivity.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Node 1 (ESP32)                          │
│                                                                  │
│  [WiFi AP: EMRG-NODE-XXXX]                                     │
│     ├─ DHCP Server (192.168.4.100-107)                         │
│     ├─ Event Monitoring (connect/disconnect)                   │
│     └─ Connection Management (stale cleanup)                   │
│              ↓                                                   │
│  [Routing Table]                                                │
│     ├─ 192.168.4.100 → Local (Phone A)                         │
│     ├─ 192.168.4.101 → Local (Phone B)                         │
│     └─ 192.168.4.102 → Node 2 (via LoRa)                       │
│              ↓                                                   │
│  [Packet Interceptor] (LWIP hooks)                             │
│              ↓                                                   │
│  [IP ↔ LoRa Bridge]                                            │
│     ├─ Compress IP packets                                      │
│     ├─ Fragment if needed                                       │
│     └─ Route via mesh                                           │
│              ↓                                                   │
│  [LoRa Radio - SX1262]                                         │
└─────────────────────────────────────────────────────────────────┘
              │
              ↓ LoRa 868MHz (EU)
              ↓
┌─────────────────────────────────────────────────────────────────┐
│                         Node 2 (ESP32)                          │
│                                                                  │
│  [LoRa Radio] ← Receive packet                                 │
│              ↓                                                   │
│  [IP ↔ LoRa Bridge]                                            │
│     ├─ Decompress packet                                        │
│     ├─ Reassemble fragments                                     │
│     └─ Inject to WiFi                                           │
│              ↓                                                   │
│  [WiFi AP] → 192.168.4.102 (Phone C)                          │
└─────────────────────────────────────────────────────────────────┘
```

## Features Implemented

### Phase 1: WiFi AP Stability ✅

**Problem**: Connection failures due to missing DHCP and buffer exhaustion

**Solutions**:
1. ✅ **DHCP Server** - Automatic IP assignment (192.168.4.100-107)
2. ✅ **WiFi Event Handlers** - Track client connect/disconnect
3. ✅ **Connection Management** - Monitor and cleanup stale connections every 5s
4. ✅ **Increased LWIP Buffers** - Doubled TCP PCBs, sockets, and packet buffers
5. ✅ **WiFi Performance Tuning** - Optimized RX/TX buffers and AMPDU

**Key Files**:
- `src/wifi/WiFiMeshBridge.h` - Header with routing table
- `src/wifi/WiFiMeshBridge.cpp` - Implementation with DHCP + events
- `variants/esp32/heltec_v2/platformio-poc.ini` - Build config with LWIP tuning

### Phase 2: IP Packet Routing (Framework Ready) ✅

**Goal**: Transparent IP packet routing across LoRa mesh

**Architecture**:
1. ✅ **Routing Table** - Maps IP addresses to LoRa node IDs
2. ✅ **LWIP Netif Hooks** - Access point for packet interception
3. ✅ **Packet Bridging Functions** - `bridgePacketToMesh()` and `injectPacketFromMesh()`
4. ⏳ **Packet Compression** - TODO: Implement IP header compression for LoRa
5. ⏳ **LWIP Integration** - TODO: Register actual packet hooks

**Status**: Framework is in place, core packet processing needs implementation.

## Configuration

### Build Configuration

File: `variants/esp32/heltec_v2/platformio-poc.ini`

```ini
[env:heltec_v2_pwa_poc]
board = heltec_wifi_lora_32_V2

build_flags =
  ; Enable features
  -D ENABLE_WIFI_AP=1
  -D ENABLE_EMERGENCY_BRIDGE=1

  ; LWIP Network Stack (increased limits)
  -D CONFIG_LWIP_MAX_ACTIVE_TCP=32      # Was 16
  -D MEMP_NUM_TCP_PCB=20                # Was 10
  -D MEMP_NUM_UDP_PCB=16                # Was 8
  -D PBUF_POOL_SIZE=32                  # Was 16

  ; WiFi Performance
  -D CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
  -D CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM=32
```

### WiFi AP Settings

**Network Configuration**:
- SSID: `EMRG-NODE-XXXX` (last 2 MAC bytes)
- Password: `emergency123`
- IP: `192.168.4.1`
- Gateway: `192.168.4.1`
- Subnet: `255.255.255.0`
- DHCP Pool: `192.168.4.100 - 192.168.4.107`
- Max Clients: 8
- Channel: 6 (2.4GHz)

## Building and Flashing

```bash
# Clean previous build
pio run -e heltec_v2_pwa_poc --target clean

# Build with new WiFi AP features
pio run -e heltec_v2_pwa_poc

# Flash to device
pio run -e heltec_v2_pwa_poc --target upload

# Monitor serial output
pio device monitor -b 115200
```

## Testing

### Test 1: WiFi AP Stability

**Goal**: Verify DHCP and connection management work

**Steps**:
1. Flash firmware to Heltec V2
2. Power on and monitor serial output
3. Should see:
   ```
   === WiFi Mesh Bridge Initialization ===
   Starting WiFi AP: EMRG-NODE-XXXX
   ✓ WiFi AP started successfully
   ✓ DHCP server started
   IP Pool: 192.168.4.100 - 192.168.4.107
   === WiFi Mesh Bridge Ready ===
   ```

4. Connect phone to `EMRG-NODE-XXXX` (password: `emergency123`)
5. Monitor serial for:
   ```
   ✓ WiFi client connected: MAC xx:xx:xx:xx:xx:xx
     Assigned IP: 192.168.4.100
     Total clients: 1/8
   Route added: IP 192.168.4.100 → Node 0xXXXX (local)
   ```

6. Check phone received IP:
   ```bash
   # On phone (requires terminal app)
   ip addr show wlan0
   # Should show: 192.168.4.100/24
   ```

7. Connect multiple phones (up to 8)
8. Verify each gets unique IP (192.168.4.100-107)
9. Disconnect phones, verify cleanup:
   ```
   ✗ WiFi client disconnected: MAC xx:xx:xx:xx:xx:xx
     Remaining clients: 0/8
   ```

**Expected Result**: ✅ All clients get IP, can ping 192.168.4.1

### Test 2: LAN App Communication (Single Node)

**Goal**: Verify P2P apps work on same node

**Apps to Test**:
- Softros LAN Messenger
- Jami (local network mode)
- Any UDP broadcast discovery app

**Steps**:
1. Connect 2 phones to same node
2. Open Softros on both phones
3. Verify phones discover each other via broadcast
4. Send test messages
5. Monitor serial for connection stats

**Expected Result**: ✅ P2P apps work locally (no LoRa needed yet)

### Test 3: Routing Table

**Goal**: Verify routing table tracks clients

**Steps**:
1. Connect 2 phones
2. Wait 6 seconds (routing table prints every 5s)
3. Check serial output:
   ```
   Connected clients (2):
     [1] MAC: aa:bb:cc:dd:ee:ff, IP: 192.168.4.100
     [2] MAC: 11:22:33:44:55:66, IP: 192.168.4.101
   Routing table (2 entries):
     192.168.4.100 → Node 0xXXXX (local, age: 10s)
     192.168.4.101 → Node 0xXXXX (local, age: 15s)
   ```

**Expected Result**: ✅ Routing table accurately tracks all clients

### Test 4: Multi-Client Stress Test

**Goal**: Verify stability with max clients

**Steps**:
1. Connect 8 phones simultaneously
2. Run Softros on all phones
3. Send messages between all pairs
4. Monitor for errors:
   - "Max clients reached" warnings (expected)
   - Connection drops (not expected)
   - LWIP buffer errors (not expected)

5. Leave running for 30 minutes
6. Check connection stability

**Expected Result**: ✅ All 8 clients remain connected and functional

### Test 5: LoRa Mesh Routing (Future)

**Goal**: Verify packet routing across nodes

**Requires**: 2+ nodes with LoRa

**Steps** (when packet compression implemented):
1. Flash 2 Heltec V2 boards
2. Connect Phone A to Node 1
3. Connect Phone B to Node 2
4. Open Softros on both phones
5. Verify cross-node discovery and messaging

**Expected Result**: ⏳ P2P apps work across LoRa mesh (TODO)

## Monitoring and Debugging

### Serial Output

**Normal Operation**:
```
=== WiFi Mesh Bridge Ready ===
✓ WiFi client connected: MAC xx:xx:xx:xx:xx:xx
  Assigned IP: 192.168.4.100
  Total clients: 1/8
Route added: IP 192.168.4.100 → Node 0xXXXX (local)

Connected clients (1):
  [1] MAC: xx:xx:xx:xx:xx:xx, IP: 192.168.4.100
```

**Error Conditions**:
```
⚠ WARNING: Max clients reached!                    # 8 clients connected
⚠ WARNING: Client count exceeds maximum!           # Internal error
ERROR: Failed to start WiFi AP                     # WiFi init failed
ERROR: AP Config Failed                            # IP config failed
⚠ Warning: Could not hook network interface        # LWIP hook failed
```

### LWIP Stats

To enable detailed LWIP statistics, add to `platformio-poc.ini`:
```ini
-D LWIP_STATS=1
-D LWIP_STATS_DISPLAY=1
```

### WiFi Diagnostics

```cpp
// In WiFiMeshBridge.cpp checkConnections()
wifi_sta_list_t wifi_sta_list;
esp_wifi_ap_get_sta_list(&wifi_sta_list);
// Logs MAC and IP of all connected clients
```

## Known Issues and Limitations

### Current Limitations

1. **Packet Bridging Not Active**: Framework in place, but actual IP packet interception needs LWIP hook registration
2. **No Packet Compression**: IP packets are logged but not compressed for LoRa transmission
3. **Route Discovery**: Routing table is manual - no automatic discovery protocol yet
4. **Fragment Handling**: Large packets (>200 bytes) not fragmented for LoRa

### Workarounds

**Issue**: WiFi + Bluetooth coexistence
- **Status**: Both enabled in `platformio-poc.ini`
- **Caveat**: May have reduced performance vs single mode
- **Solution**: Monitor power consumption and throughput

**Issue**: DHCP lease expiration
- **Status**: Default 120 minute lease
- **Workaround**: Phones auto-renew, but monitor for lease expiration issues

## Next Steps: Packet Compression

**Phase 2 completion** requires implementing actual IP packet interception and compression.

### TODO: Packet Compression Layer

**File**: `src/wifi/PacketCompressor.cpp` (new)

**Key Functions**:
```cpp
// Compress IP packet for LoRa transmission
// Input: Full IP packet (20-1500 bytes)
// Output: Compressed packet (<200 bytes ideal)
size_t compressIPPacket(const uint8_t* ip_packet, size_t len,
                        uint8_t* compressed, size_t max_len);

// Decompress LoRa packet back to IP
// Input: Compressed packet
// Output: Full IP packet
size_t decompressIPPacket(const uint8_t* compressed, size_t len,
                          uint8_t* ip_packet, size_t max_len);
```

**Compression Strategy**:
1. Extract IP header (20 bytes)
   - Source/Dest IP (8 bytes) → Map to node IDs (4 bytes)
   - Protocol, TTL, checksum → Store minimal info (2 bytes)

2. Extract transport header (UDP 8 bytes, TCP 20 bytes)
   - Source/Dest port (4 bytes) → Keep as-is
   - Checksum, flags → Recompute on receive side

3. Fragment if payload > 200 bytes
   - Split into chunks with sequence numbers
   - Reassemble on receive side

**Expected Compression**:
- IP header: 20 bytes → 6 bytes (70% reduction)
- UDP header: 8 bytes → 4 bytes (50% reduction)
- Total overhead: 28 bytes → 10 bytes (64% reduction)
- Usable LoRa payload: ~220 bytes after overhead

### TODO: LWIP Hook Registration

**File**: `src/wifi/WiFiMeshBridge.cpp:setupNetworkBridge()`

**Add**:
```cpp
// Register input hook to intercept outgoing packets
netif_ap->input = netifInput;

// Register output hook to intercept incoming packets
netif_ap->output = netifOutput;
```

**Then implement**:
```cpp
err_t WiFiMeshBridge::netifInput(struct pbuf *p, struct netif *inp) {
    // Extract IP packet from pbuf
    // Check destination IP
    // If remote → call bridgePacketToMesh()
    // If local → pass through normally
}
```

## Power Consumption

**WiFi AP Mode**:
- Active (clients connected): ~240mA
- Idle (no clients): ~120mA
- LoRa TX: ~120mA
- LoRa RX: ~15mA

**Battery Life** (3000mAh):
- WiFi + LoRa active: ~10 hours
- WiFi only: ~12 hours
- LoRa only: ~200 hours

**Recommendation**: Use power management to disable WiFi AP after 5 minutes of no clients.

## Security Considerations

**Current Security**:
- ✅ WPA2-PSK password (`emergency123`)
- ✅ Isolated network (192.168.4.0/24)
- ⚠️ No encryption between nodes (LoRa plaintext)

**Future Enhancements**:
- Add Meshtastic encryption for LoRa packets
- Use rotating WiFi passwords
- Implement node authentication

## Performance Metrics

**Target Metrics**:
- WiFi AP startup: <2 seconds
- DHCP lease assignment: <1 second
- Client connection: <3 seconds
- Packet routing latency: <50ms (local), <500ms (via LoRa)
- Max throughput: ~1 Mbps (WiFi local), ~5 kbps (via LoRa)

**Actual Measurements**: TBD after testing

## Support

For issues or questions:
1. Check serial monitor for error messages
2. Verify LWIP buffer settings in `platformio-poc.ini`
3. Test with single client first before adding more
4. Monitor heap usage: `ESP.getFreeHeap()`

## References

- Meshtastic docs: https://meshtastic.org/docs/
- ESP32 WiFi: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html
- LWIP: https://www.nongnu.org/lwip/
- LoRa: https://lora-alliance.org/
