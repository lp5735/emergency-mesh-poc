# Emergency WiFi Bridge - Interface Specification

## Overview

This document specifies the WebSocket and HTTP REST API interface exposed by emergency mesh nodes running the WiFi Bridge firmware. HQ clients, field devices, and web applications use these interfaces to communicate with the LoRa mesh network.

**Base URL**: `http://192.168.4.1` (default WiFi AP IP)
**WebSocket URL**: `ws://192.168.4.1:81`

## HTTP REST API

### Authentication

**Current Implementation**: Open access (no authentication)
**Future**: Consider basic auth or token-based for production deployments

### Endpoints

#### 1. Send Message to Mesh (Simple GET)

**Endpoint**: `GET /send?msg=<message>`
**Purpose**: Simple way to send a message to the mesh (broadcast to all nodes)

**Query Parameters**:
- `msg` (required) - Message text to send

**Example**:
```
GET http://192.168.4.1/send?msg=Hello%20mesh
```

**Response** (Plain text):
```
Sent: Hello mesh
```

**Error Responses**:
- `400 Bad Request` - Missing 'msg' parameter with usage instructions
- `500 Internal Server Error` - Failed to send to mesh

---

#### 2. Test Endpoint

**Endpoint**: `GET /test`
**Purpose**: Quick test endpoint to verify mesh communication

**Response** (Plain text):
```
Test message sent!
```

or

```
Failed
```

---

#### 3. Store & Forward History

**Endpoint**: `GET /api/history`
**Purpose**: Retrieve message history from Store & Forward module (if enabled)

**Query Parameters**:
- `since` (optional) - Unix timestamp, return messages after this time
- `max` (optional, default: 50, max: 100) - Maximum messages to return

**Response** (if Store & Forward enabled):
```json
{
  "available": 25,
  "requested": 50,
  "note": "Store & Forward enabled - packet extraction coming soon",
  "messages": []
}
```

**Response** (if Store & Forward disabled):
```json
{
  "error": "Store & Forward disabled in build"
}
```

**Error Codes**:
- `503 Service Unavailable` - Store & Forward not available

---

#### 4. Web UI (Main Page)

**Endpoint**: `GET /` or `GET /index.html` or `GET /emergency.html`
**Purpose**: Serve embedded Progressive Web App (PWA)

**Features**:
- Real-time messaging with WebSocket
- Message history persistence (localStorage)
- Username registration
- SOS button
- Works offline (Service Worker)
- Installable as PWA

**Response**: HTML page with embedded CSS/JS (~1.5KB)

---

#### 5. Debug Page

**Endpoint**: `GET /debug`
**Purpose**: Mesh status debugging interface

**Features**:
- Real-time mesh node status
- Signal strength indicators (SNR)
- Online/offline detection
- Last-heard timestamps

**Response**: HTML page with mesh monitoring UI (~2KB)

---

#### 6. Thread Page (1-1 Conversations)

**Endpoint**: `GET /thread?u=<username>`
**Purpose**: Direct messaging between two users

**Query Parameters**:
- `u` (required) - The other user's username

**Features**:
- User registration required
- Filtered message view (only messages between two users)
- Real-time updates via WebSocket

**Response**: HTML page for 1-1 conversations (~1.5KB)

---

#### 7. PWA Service Worker

**Endpoint**: `GET /sw.js`
**Purpose**: Service Worker for Progressive Web App offline capability

**Response**: JavaScript file (~400 bytes)

**Headers**:
- `Content-Type: application/javascript`
- `Cache-Control: no-cache` (allows SW updates)

---

#### 8. PWA Manifest

**Endpoint**: `GET /manifest.json`
**Purpose**: Web App Manifest for PWA installation

**Response**:
```json
{
  "name": "Emergency Mesh Network",
  "short_name": "EmrgMesh",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#111",
  "theme_color": "#f00",
  "icons": [...]
}
```

**Headers**:
- `Content-Type: application/json`
- `Cache-Control: public, max-age=86400` (24 hour cache)

---

#### 9. Captive Portal Endpoints

The following endpoints redirect to the main page for automatic captive portal detection:

- `GET /hotspot-detect.html` - iOS captive portal detection
- `GET /library/test/success.html` - iOS alternate detection
- `GET /generate_204` - Android captive portal detection
- `GET /connecttest.txt` - Windows captive portal detection

All redirect to: `http://192.168.4.1/`

---

## WebSocket Protocol

### Connection

**URL**: `ws://192.168.4.1:81`
**Protocol**: Text frames (JSON) only
**Reconnection**: Automatic with exponential backoff (1s → 3s max)

### Message Format

All WebSocket messages use JSON format. Messages from clients can be simple or structured.

### Client → Server Messages

#### 1. Simple Text Message (Broadcast)

Simplest form - just send message text with username:

```json
{
  "text": "Hello everyone",
  "username": "John",
  "timestamp": 1234567890
}
```

or

```json
{
  "msg": "Hello everyone",
  "username": "John"
}
```

**Behavior**:
- Broadcasts to all WiFi clients on same AP
- Forwards to LoRa mesh (broadcast to all nodes)
- No response/acknowledgment

---

#### 2. User Registration

Register a username (required for thread/DM features):

```json
{
  "type": "register",
  "username": "John"
}
```

**Server Response** (Success):
```json
{
  "type": "username_confirmed",
  "username": "John"
}
```

**Server Response** (Error):
```json
{
  "type": "username_error",
  "message": "Username already taken"
}
```

**Validation Rules**:
- Username must be 3-20 characters
- Alphanumeric + spaces allowed
- Global uniqueness across entire mesh network
- 2-second timeout for mesh-wide conflict detection

---

#### 3. Direct Message (1-1 Communication)

Send a message to specific user:

```json
{
  "type": "direct_message",
  "from_username": "John",
  "to_username": "Jane",
  "text": "Private message",
  "timestamp": 1234567890
}
```

**Behavior**:
- Broadcasts to all local WiFi clients (they filter by username)
- Forwards to LoRa mesh (other nodes relay to intended recipient)
- No server-side filtering/routing

---

### Server → Client Messages

#### 1. Node Info (On Connection)

Sent automatically when WebSocket connects:

```json
{
  "type": "node_info",
  "nodeId": "ABCD",
  "name": "Test Node",
  "timestamp": 1234567890
}
```

---

#### 2. Incoming Message from Mesh

Real-time notification when a LoRa message is received:

```json
{
  "type": "message",
  "from": "1234",
  "text": "Message content",
  "timestamp": 1234567890,
  "rssi": -85,
  "snr": 7.5,
  "source": "lora"
}
```

**Fields**:
- `from`: Node ID (hex, no 0x prefix)
- `text`: Message content (plain text or JSON string)
- `rssi`: Signal strength in dBm
- `snr`: Signal-to-noise ratio in dB
- `source`: Always "lora" for mesh messages

---

#### 3. Incoming Message from WiFi Client

When another WiFi client on same AP sends a message:

```json
{
  "type": "message",
  "from": "local",
  "username": "John",
  "text": "Hello",
  "timestamp": 1234567890,
  "rssi": 0,
  "snr": 0,
  "source": "wifi"
}
```

**Fields**:
- `from`: Always "local" for WiFi-originated messages
- `username`: Sender's registered username
- `source`: Always "wifi" for local WiFi messages
- `rssi`/`snr`: Always 0 for WiFi messages

---

#### 4. Direct Message

Private message between users:

```json
{
  "type": "direct_message",
  "from_username": "John",
  "to_username": "Jane",
  "text": "Private message",
  "timestamp": 1234567890
}
```

**Behavior**:
- Broadcast to all clients (client-side filtering by username)
- Only displayed if client matches `from_username` or `to_username`

---

#### 5. Mesh Status Update

Periodic mesh status broadcast (every 5 seconds if clients connected):

```json
{
  "type": "mesh_status",
  "timestamp": 1234567890,
  "total_nodes": 5,
  "online_nodes": 3,
  "nodes": [
    {
      "id": "!12345678",
      "long_name": "Emergency Node 01",
      "short_name": "EN01",
      "snr": 8.5,
      "rssi": -85,
      "last_heard": 1234567880,
      "is_online": true,
      "seconds_ago": 10,
      "battery_level": 85
    }
  ]
}
```

**Fields**:
- `id`: Meshtastic node ID format (!xxxxxxxx)
- `is_online`: True if heard within last 5 minutes
- `seconds_ago`: Time since last heard
- `battery_level`: Battery % (if available)

---

#### 6. User Joined

Notification when a user registers (local or remote):

```json
{
  "type": "user_joined",
  "username": "John",
  "timestamp": 1234567890,
  "node_id": 43981
}
```

---

#### 7. User Left

Notification when a user disconnects:

```json
{
  "type": "user_left",
  "username": "John",
  "timestamp": 1234567890,
  "node_id": 43981
}
```

---

## Data Types & Formats

### Node ID Format

**Meshtastic Format**: `!xxxxxxxx` (8 hex digits with ! prefix)
**Example**: `!12345678`

**WiFi Service Format**: Last 2 bytes of MAC address (decimal)
**Example**: `43981` (0xABCD)

### Message Types

Currently, the system handles primarily text messages. Message type is inferred from content:

| Content Pattern | Type | Description |
|----------------|------|-------------|
| Plain text | text | Standard text message |
| Starts with "🆘" | sos | Emergency SOS message |
| JSON with `type` field | structured | User management, direct messages, etc. |

### Timestamps

**Format**: Milliseconds since boot (`millis()`)
**Type**: Integer (uint32_t)
**Example**: `1234567890`

**Note**: Not Unix timestamp - relative to ESP32 boot time

### Signal Strength Metrics

**RSSI**: Received Signal Strength Indicator (dBm)
- Excellent: > -60 dBm
- Good: -60 to -80 dBm
- Fair: -80 to -100 dBm
- Poor: < -100 dBm

**SNR**: Signal-to-Noise Ratio (dB)
- Excellent: > 10 dB
- Good: 5 to 10 dB
- Fair: 0 to 5 dB
- Poor: < 0 dB

**WiFi Messages**: Always report `rssi: 0` and `snr: 0`

---

## Error Handling

### HTTP Error Codes

| Code | Meaning | Example |
|------|---------|---------|
| 200 | Success | Message sent successfully |
| 400 | Bad Request | Missing 'msg' parameter |
| 404 | Not Found | Unknown endpoint |
| 500 | Internal Server Error | Failed to send to mesh |
| 503 | Service Unavailable | Store & Forward not available |

### WebSocket Error Handling

**Connection Errors**: Client implements automatic reconnection with exponential backoff (1s, 2s, 3s max)

**Message Errors**: Malformed JSON is logged and ignored (no error sent to client)

---

## Rate Limits & Constraints

### WiFi Access Point

- **SSID Format**: `EMRG-NODE-XXXX` (last 2 bytes of MAC)
- **Password**: None (open network for emergency access)
- **IP Address**: Fixed at `192.168.4.1/24`
- **Channel**: 6
- **Max simultaneous clients**: 4 (configurable, ESP32 limitation)
- **DNS**: All domains redirect to 192.168.4.1 (captive portal)

### LoRa Mesh Constraints

- **Max message size**: ~237 bytes for text payload
- **Broadcast only**: Current implementation doesn't support targeted sends
- **No ACK**: Broadcast messages don't request acknowledgments
- **Airtime limits**: LoRa duty cycle regulations apply (varies by region)

### WebSocket

- **Port**: 81
- **Max connections**: Limited by WiFi AP client limit (4)
- **Message format**: JSON text frames only
- **No binary support**: All data must be JSON-serializable

### Memory Constraints

- **Heap usage**: ~128KB free after WiFi/WebSocket init
- **Message history**: Stored in RAM (last 50 messages in localStorage on client)
- **No persistent storage**: Messages not saved to flash/SD

---

## HQ Client Implementation Guidelines

### Connection Management

1. **Connect to WiFi AP**: `EMRG-NODE-XXXX` (open network, no password)
2. **Verify connectivity**: HTTP GET to `http://192.168.4.1/test`
3. **Establish WebSocket**: `ws://192.168.4.1:81`
4. **Handle auto-connect**: On connection, server sends `node_info` message
5. **Implement reconnection**: Exponential backoff on disconnect (1s, 2s, 3s max)

### Data Persistence

HQ clients should:
- Store all received messages in local database (SQLite, IndexedDB, etc.)
- Subscribe to `mesh_status` broadcasts (every 5 seconds) for node tracking
- Persist user registrations and direct message history
- Use `localStorage` for browser-based clients (see web UI example)

### Message Handling

**Incoming Message Types to Handle**:
- `node_info` - Initial connection, store node ID
- `message` - LoRa and WiFi messages, check `source` field
- `mesh_status` - Node list updates, merge with local database
- `direct_message` - Private messages (filter by username)
- `user_joined` / `user_left` - Track active users across mesh

**Deduplication**:
- Messages don't have unique IDs in current implementation
- Deduplicate by `(from, text, timestamp)` tuple
- Same message may arrive via WiFi broadcast and LoRa mesh

### Multi-Node Support

HQ clients connecting to multiple nodes should:
- Use separate WebSocket connections per node (parallel monitoring)
- Track which node provided each message for coverage analysis
- Deduplicate messages that arrive via multiple nodes
- Monitor mesh status from multiple perspectives to detect network partitions

---

## Example Client Code

### JavaScript WebSocket Client (Simplified)

```javascript
class EmergencyMeshClient {
  constructor(nodeIP = '192.168.4.1') {
    this.baseURL = `http://${nodeIP}`;
    this.wsURL = `ws://${nodeIP}:81`;
    this.ws = null;
    this.username = '';
    this.messageHandlers = [];
    this.reconnectAttempts = 0;
  }

  async connect() {
    // Connect WebSocket
    this.ws = new WebSocket(this.wsURL);

    this.ws.onopen = () => {
      console.log('Connected to mesh node');
      this.reconnectAttempts = 0;
    };

    this.ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      console.log('Received:', msg);
      this.messageHandlers.forEach(handler => handler(msg));
    };

    this.ws.onerror = () => {
      console.error('WebSocket error');
    };

    this.ws.onclose = () => {
      this.reconnectAttempts++;
      const delay = Math.min(3000, 1000 * this.reconnectAttempts);
      console.log(`Disconnected, reconnecting in ${delay}ms...`);
      setTimeout(() => this.connect(), delay);
    };
  }

  registerUsername(username) {
    this.username = username;
    this.ws.send(JSON.stringify({
      type: 'register',
      username: username
    }));
  }

  sendMessage(text, username = this.username) {
    this.ws.send(JSON.stringify({
      text: text,
      username: username || 'Anonymous',
      timestamp: Date.now()
    }));
  }

  sendDirectMessage(toUser, text) {
    this.ws.send(JSON.stringify({
      type: 'direct_message',
      from_username: this.username,
      to_username: toUser,
      text: text,
      timestamp: Date.now()
    }));
  }

  onMessage(handler) {
    this.messageHandlers.push(handler);
  }
}

// Usage
const client = new EmergencyMeshClient('192.168.4.1');
await client.connect();

client.onMessage((msg) => {
  switch(msg.type) {
    case 'node_info':
      console.log(`Connected to node ${msg.nodeId}`);
      break;
    case 'message':
      console.log(`[${msg.source}] ${msg.from}: ${msg.text}`);
      break;
    case 'mesh_status':
      console.log(`Mesh: ${msg.online_nodes}/${msg.total_nodes} nodes online`);
      break;
    case 'username_confirmed':
      console.log(`Username ${msg.username} confirmed`);
      break;
  }
});

// Register username
client.registerUsername('HQ-Command');

// Send broadcast message
client.sendMessage('HQ online and monitoring');

// Send direct message
client.sendDirectMessage('Field-01', 'Status report please');
```

### Python HTTP Client (Simple Send)

```python
import requests

class EmergencyMeshAPI:
    def __init__(self, node_ip='192.168.4.1'):
        self.base_url = f'http://{node_ip}'

    def send_message(self, message):
        """Send message via simple GET endpoint"""
        response = requests.get(f'{self.base_url}/send', params={'msg': message})
        return response.text

    def test_connection(self):
        """Test if node is reachable"""
        try:
            response = requests.get(f'{self.base_url}/test', timeout=5)
            return response.status_code == 200
        except:
            return False

# Usage
api = EmergencyMeshAPI('192.168.4.1')

if api.test_connection():
    print("Node connected")
    result = api.send_message('Status check from HQ')
    print(result)  # "Sent: Status check from HQ"
else:
    print("Node not reachable")
```

### Python WebSocket Client

```python
import asyncio
import websockets
import json

class EmergencyMeshWSClient:
    def __init__(self, node_ip='192.168.4.1'):
        self.ws_url = f'ws://{node_ip}:81'
        self.username = 'HQ-Command'

    async def connect(self):
        async with websockets.connect(self.ws_url) as websocket:
            print(f"Connected to {self.ws_url}")

            # Register username
            await websocket.send(json.dumps({
                'type': 'register',
                'username': self.username
            }))

            # Send a message
            await websocket.send(json.dumps({
                'text': 'HQ monitoring active',
                'username': self.username,
                'timestamp': int(time.time() * 1000)
            }))

            # Listen for messages
            async for message in websocket:
                msg = json.loads(message)
                print(f"Received: {msg}")

                if msg.get('type') == 'message':
                    print(f"[{msg.get('source')}] {msg.get('from')}: {msg.get('text')}")
                elif msg.get('type') == 'mesh_status':
                    print(f"Mesh status: {msg.get('online_nodes')}/{msg.get('total_nodes')} nodes")

# Usage
client = EmergencyMeshWSClient('192.168.4.1')
asyncio.run(client.connect())
```

---

## Security Considerations

### Current Implementation (POC)

- **No authentication**: Open WiFi access for emergency scenarios
- **No encryption on WiFi**: Open network (no password required)
- **No API security**: All endpoints publicly accessible
- **Minimal input validation**: Basic length checks only
- **No rate limiting**: Trust-based system

### Why Open Network?

This is designed for **emergency response scenarios** where:
- First responders need immediate access without password sharing
- Time is critical - no credentials to remember/share
- Physical proximity provides implicit access control
- LoRa mesh messages are encrypted (AES-256) - WiFi is just the local bridge

### Production Recommendations

1. **WiFi Security**: Add WPA2/WPA3 with pre-shared key for controlled deployments
2. **HTTPS**: Implement TLS (requires certificate management on ESP32)
3. **Authentication**: Add token-based auth for HQ/command clients
4. **Input Validation**: Implement comprehensive sanitization
5. **Rate Limiting**: Prevent message flooding

### Privacy Notes

- **Node IDs**: Derived from MAC addresses (hardware identifiers)
- **Usernames**: Globally visible across entire mesh network
- **Message Content**: Not encrypted on WiFi (anyone in range can intercept)
- **LoRa Mesh**: Messages ARE encrypted (AES-256) between mesh nodes
- **Location Data**: Not currently transmitted (can be added)

---

## Testing Tools

### cURL Examples

```bash
# Test connectivity
curl http://192.168.4.1/test

# Send message (simple GET)
curl "http://192.168.4.1/send?msg=Hello%20from%20HQ"

# Get Store & Forward history (if enabled)
curl "http://192.168.4.1/api/history?max=10"
```

### WebSocket Testing (wscat)

```bash
# Install wscat
npm install -g wscat

# Connect to WebSocket
wscat -c ws://192.168.4.1:81

# Send message
> {"text":"Hello mesh","username":"Tester","timestamp":1234567890}

# Register username
> {"type":"register","username":"HQ-Command"}

# Send direct message
> {"type":"direct_message","from_username":"HQ-Command","to_username":"Field-01","text":"Status?"}
```

### Browser Testing

Simply connect to WiFi network `EMRG-NODE-XXXX` and browse to `http://192.168.4.1/` - captive portal should open automatically.

---

## Implementation Status

### Currently Implemented ✅

- WiFi Access Point with captive portal
- WebSocket server for real-time messaging
- HTTP endpoints: `/`, `/send`, `/test`, `/debug`, `/thread`, `/api/history`
- PWA support (Service Worker, Manifest, offline capability)
- Username registration with mesh-wide uniqueness
- Direct messaging (1-1 conversations)
- Mesh status broadcasting
- User join/leave notifications
- Message persistence in browser (localStorage)

### Not Yet Implemented ❌

- Targeted message send (currently broadcast only)
- Message acknowledgments
- Voice clips / binary data
- Incident forms (structured data)
- GPS/location reporting
- Store & Forward data extraction (placeholder only)
- Authentication / authorization
- Rate limiting
- HTTPS/TLS

---

## References

- **Emergency WiFi Bridge Code**: [src/wifi/EmergencyWiFiService.cpp](src/wifi/EmergencyWiFiService.cpp)
- **WebSocket Bridge Code**: [src/modules/EmergencyWiFiBridge.cpp](src/modules/EmergencyWiFiBridge.cpp)
- **Meshtastic Documentation**: https://meshtastic.org/docs/
- **ESP32 WebSocket Library**: https://github.com/Links2004/arduinoWebSockets
- **ESPAsyncWebServer**: https://github.com/me-no-dev/ESPAsyncWebServer
