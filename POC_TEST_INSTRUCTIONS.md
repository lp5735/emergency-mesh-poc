# Emergency WiFi Bridge - POC Test Instructions

## What We Built

Ultra-minimal proof of concept to test LoRa mesh communication via WiFi.

**Architecture**:
```
Phone A ──WiFi──> Node A ──LoRa Mesh──> Node B <──WiFi── Phone B
   |                |                       |                |
192.168.4.1      EMRG-A                  EMRG-B         192.168.4.1
```

**What happens when you click "Send"**:
1. Phone A → HTTP GET `/test` → Node A WiFi AP
2. Node A → Broadcasts "Hello Mesh!" via LoRa
3. Node B → Receives via LoRa → Broadcasts via WebSocket
4. Phone B → Displays message in real-time!

## Hardware Requirements

- **2x Heltec WiFi LoRa 32 V2** boards
- **2x Phones/Tablets** with WiFi
- USB cables for flashing

## Step 1: Flash Firmware to Both Boards

```bash
# Flash Node A
pio run -e heltec-v2-pwa-poc --target upload

# Flash Node B (same command, different device)
# Disconnect Node A, connect Node B, then:
pio run -e heltec-v2-pwa-poc --target upload
```

**Important**: Upload filesystem too!
```bash
pio run -e heltec-v2-pwa-poc --target uploadfs
```

## Step 2: Power On and Check Serial Logs

```bash
pio device monitor -b 115200
```

**Expected output**:
```
Initializing Emergency WiFi Service...
LittleFS mounted successfully
Files in LittleFS:
  /webapp/index.html (4119 bytes)
Starting WiFi AP: EMRG-TEST-POC
WiFi AP started successfully
IP Address: 192.168.4.1
Connect to: EMRG-TEST-POC
Password: emergency123
Then browse to: http://192.168.4.1
```

**Troubleshooting**:
- If `/webapp/index.html` not found → Run `uploadfs` again
- If WiFi AP doesn't start → Check power supply (USB must provide 500mA+)

## Step 3: Connect Phones to WiFi

**Phone A**:
1. WiFi Settings → Connect to `EMRG-TEST-POC`
2. Password: `emergency123`
3. Open browser → Navigate to `http://192.168.4.1`

**Phone B**:
1. Connect to **same** WiFi network (for POC, both nodes use same SSID)
2. Open browser → Navigate to `http://192.168.4.1`

**Note**: For real deployment, each node will have unique SSID like `EMRG-NODE-A1B2`. For POC testing with one SSID, make sure phones connect to **different physical nodes** by placing Node A and Node B in different rooms.

## Step 4: Test Mesh Communication

**On Phone A**:
1. Webpage should show: ✅ Connected to node
2. Click button: "📡 Send 'Hello Mesh!' to all nodes"
3. You should see local confirmation: "📤 Message sent"

**On Phone B** (should happen within 1-2 seconds):
1. Real-time message appears: "📩 Message received"
2. Text: "Hello Mesh!"
3. Timestamp and signal strength (RSSI/SNR)

## Expected Serial Logs

**Node A** (after clicking send):
```
Test endpoint hit! Sending mesh broadcast...
EmergencyWiFiBridge: Sending to mesh: Hello Mesh!
EmergencyWiFiBridge: Packet allocated, sending 11 bytes
EmergencyWiFiBridge: ✓ Message sent to mesh successfully
```

**Node B** (after receiving):
```
EmergencyWiFiBridge: Received message from mesh, from=0xXXXX, size=11
EmergencyWiFiBridge: Message text: Hello Mesh!
EmergencyWiFiBridge: Broadcasted to WiFi clients: {"type":"message","from":"XXXX","text":"Hello Mesh!","timestamp":12345,"rssi":-45,"snr":9.5}
```

## Success Criteria

✅ Both nodes create WiFi AP successfully
✅ Phones connect and load webpage
✅ WebSocket shows "Connected to node"
✅ Clicking send on Phone A shows message on Phone B
✅ Round-trip time < 2 seconds
✅ RSSI and SNR values appear in message

## Troubleshooting

### WiFi Issues

**Problem**: Can't connect to WiFi AP
- Check SSID is `EMRG-TEST-POC`
- Password is exactly `emergency123`
- ESP32 WiFi channel 6 (some phones don't support ch 12+)
- Move closer to node (< 10 meters for testing)

**Problem**: Can't access http://192.168.4.1
- Check phone actually connected (some phones don't auto-assign IP)
- Try pinging: `ping 192.168.4.1`
- Disable cellular data on phone
- Try different browser

### WebSocket Issues

**Problem**: Shows "Disconnected" or "Connection error"
- Check serial logs for WebSocket errors
- Ensure port 81 is not blocked
- Try refreshing page
- Check browser console (F12) for errors

### LoRa Mesh Issues

**Problem**: Message not received on other node
- Check both nodes have same LoRa frequency (915 MHz for US)
- Ensure nodes are < 100 meters apart for initial test
- Check serial logs for "Packet allocated" on sender
- Check serial logs for "Received message from mesh" on receiver
- Verify antennas are attached (CRITICAL - running without antenna damages radio!)

**Problem**: Message received but not shown on phone
- Check WebSocket is connected
- Check serial logs for "Broadcasted to WiFi clients"
- Refresh browser page
- Check browser console for JavaScript errors

## Next Steps After Successful Test

Once you confirm bidirectional communication works:

1. **Add unique SSIDs** - Modify code to use MAC-based SSID per node
2. **Test range** - Move nodes apart to test real-world LoRa range
3. **Add message history** - Store last 10 messages
4. **Add node info** - Display node list with signal strength
5. **Performance testing** - Measure latency, packet loss
6. **Multi-hop testing** - Add 3rd node to test mesh routing

## Hardware Tips

- **Always use antennas** - Running LoRa without antenna damages the radio chip
- **Power supply** - USB must provide 500mA+ when WiFi AP is active
- **Placement** - For best LoRa range, place nodes high and away from metal
- **Interference** - WiFi 2.4GHz and LoRa 915MHz don't interfere, but concrete/metal does

## POC Limitations

This POC is intentionally minimal:
- ❌ No encryption (added later)
- ❌ No message history (in-memory only)
- ❌ No node discovery UI
- ❌ No battery monitoring
- ❌ Single SSID for both nodes (testing simplification)
- ❌ No error recovery
- ✅ **Just testing LoRa mesh works!**
