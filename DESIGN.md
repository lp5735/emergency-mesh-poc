# Emergency Responder Mesh Network - Design Specification

**Version**: 1.0
**Target Hardware**: Heltec WiFi LoRa 32 V2 (ESP32 + SX1276)
**Base**: Meshtastic Firmware (Modified Fork)

---

## Executive Summary

This document specifies the design for an Emergency Responder Mesh Network that transforms Meshtastic's proven LoRa mesh core into a WiFi-accessible system for emergency response scenarios. Emergency responders connect their phones/tablets to ESP32 nodes via WiFi AP, access a web interface, and communicate over the LoRa mesh without installing apps.

**Key Innovation**: Replace BLE + Protobuf client interface with WiFi AP + REST/WebSocket + embedded web UI while keeping Meshtastic's mature mesh routing layer intact.

---

## 1. System Architecture

### 1.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Emergency Response Device                 │
│                      (Phone/Tablet Browser)                  │
└───────────────────┬─────────────────────────────────────────┘
                    │ WiFi (192.168.4.1)
                    │ HTTP REST / WebSocket
┌───────────────────▼─────────────────────────────────────────┐
│                   ESP32 WiFi Access Point                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  WiFi Stack  │  │  Web Server  │  │  WebSocket   │      │
│  │  (AP Mode)   │  │  (Port 80)   │  │  (Port 81)   │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         └──────────────────┴──────────────────┘              │
│                            │                                 │
│  ┌─────────────────────────▼──────────────────────────────┐ │
│  │          Emergency WiFi Bridge Module                   │ │
│  │     (JSON ↔ Protobuf Translation Layer)                │ │
│  └─────────────────────────┬──────────────────────────────┘ │
│                            │                                 │
│  ┌─────────────────────────▼──────────────────────────────┐ │
│  │        Meshtastic Core (UNCHANGED)                      │ │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐       │ │
│  │  │  Routing   │  │  Channels  │  │  Crypto    │       │ │
│  │  │  (Flood/   │  │  (AES-256) │  │  (Nanopb)  │       │ │
│  │  │   Reliable)│  │            │  │            │       │ │
│  │  └────────────┘  └────────────┘  └────────────┘       │ │
│  └─────────────────────────┬──────────────────────────────┘ │
└────────────────────────────┼────────────────────────────────┘
                             │ LoRa Radio (433/868/915 MHz)
                             ▼
┌────────────────────────────────────────────────────────────┐
│                    LoRa Mesh Network                        │
│  [Node 1] ←──→ [Node 2] ←──→ [Node 3] ←──→ [Node N]       │
└────────────────────────────────────────────────────────────┘
```

### 1.2 Component Layers

| Layer | Components | Status | Dependencies |
|-------|-----------|--------|--------------|
| **Presentation** | Web UI (HTML/CSS/JS) | NEW | Embedded in ESP32 flash |
| **Application** | REST API, WebSocket Server | NEW | ESPAsyncWebServer, WebSockets lib |
| **Bridge** | EmergencyWiFiBridge Module | NEW | ArduinoJson, MeshService |
| **Core Mesh** | Routing, Crypto, Channels | UNCHANGED | RadioLib, Nanopb |
| **Hardware** | SX1276 LoRa Radio, ESP32 WiFi | EXISTING | Heltec V2 board |

---

## 2. Component Specifications

### 2.1 WiFi Access Point Service

**File**: `src/wifi/EmergencyWiFiService.h/.cpp`

#### 2.1.1 Requirements

- **R-WIFI-001**: Create WiFi AP with SSID format "EMRG-NODE-{MAC4}" (last 4 MAC hex digits)
- **R-WIFI-002**: Support 6+ simultaneous client connections
- **R-WIFI-003**: Static IP configuration: 192.168.4.1/24
- **R-WIFI-004**: Configurable password (default: "emergency123")
- **R-WIFI-005**: Auto-shutdown after 5 min of no client activity
- **R-WIFI-006**: Wake on button press or incoming LoRa message

#### 2.1.2 Class Interface

```cpp
class EmergencyWiFiService {
public:
    // Lifecycle
    void init();                    // Initialize WiFi AP
    void start();                   // Start AP and servers
    void stop();                    // Stop AP (power save)
    void loop();                    // Service loop (call from main)

    // State
    bool isActive();                // Is AP running?
    uint8_t getClientCount();       // Number connected clients

    // Client management
    struct Client {
        uint8_t id;                 // Client slot index (0-5)
        IPAddress ip;               // Client IP address
        uint32_t lastActivity;      // millis() of last message
        uint16_t responderID;       // Optional: responder badge number
        char name[32];              // Optional: responder name
    };

    Client* getClients();           // Get client array
    void disconnectClient(uint8_t id);

    // Messaging
    void broadcastToClients(const char* json);
    void sendToClient(uint8_t clientID, const char* json);

private:
    AsyncWebServer httpServer;
    WebSocketsServer wsServer;
    Client clients[WIFI_MAX_CLIENTS];
    uint32_t lastClientActivity;
    bool apActive;

    void setupHTTPEndpoints();
    void setupWebSocket();
    void checkIdleTimeout();
};
```

#### 2.1.3 WiFi Configuration

```cpp
// Network settings
#define WIFI_AP_SSID_PREFIX "EMRG-NODE-"
#define WIFI_AP_PASSWORD "emergency123"
#define WIFI_AP_CHANNEL 6              // 2.4 GHz channel
#define WIFI_AP_HIDDEN false           // Visible SSID
#define WIFI_MAX_CLIENTS 6             // Max simultaneous connections
#define WIFI_BEACON_INTERVAL 100       // ms

// IP Configuration
#define WIFI_AP_IP IPAddress(192, 168, 4, 1)
#define WIFI_AP_GATEWAY IPAddress(192, 168, 4, 1)
#define WIFI_AP_SUBNET IPAddress(255, 255, 255, 0)

// Power management
#define WIFI_IDLE_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes
```

---

### 2.2 REST API Layer

**File**: `src/wifi/APIHandlers.h/.cpp`

#### 2.2.1 API Endpoints

| Method | Endpoint | Purpose | Request Body | Response |
|--------|----------|---------|--------------|----------|
| GET | `/` | Serve web UI | - | HTML page |
| POST | `/api/send` | Send message to mesh | JSON | Status + msgID |
| GET | `/api/nodes` | Get mesh topology | - | JSON array |
| GET | `/api/status` | Node status | - | JSON object |
| POST | `/api/voice` | Upload voice chunk | Binary | Chunk ACK |
| POST | `/api/form` | Submit incident form | JSON | Form ID |
| GET | `/api/history` | Recent messages | - | JSON array |

#### 2.2.2 Request/Response Formats

**POST /api/send**
```json
// Request
{
  "dest": "0x1234",           // Hex node ID or "broadcast"
  "type": "text",             // text | sos | form | voice
  "priority": "normal",       // normal | high | emergency
  "message": "Need medical assistance at grid ref 12S UD 12345 67890"
}

// Response (success)
{
  "status": "sent",
  "msgId": "0xABCD",
  "timestamp": 1234567890,
  "hops": 0                   // Will update via WebSocket
}

// Response (error)
{
  "status": "error",
  "error": "destination_unreachable",
  "message": "Node 0x1234 not in mesh"
}
```

**GET /api/nodes**
```json
// Response
{
  "nodes": [
    {
      "id": "0x1234",
      "name": "Node-Alpha",
      "rssi": -85,              // Signal strength (dBm)
      "snr": 8.5,               // Signal-to-noise ratio
      "hops": 1,                // Hops from this node
      "lastSeen": 1234567890,   // Unix timestamp
      "battery": 78,            // Battery % (if available)
      "clients": 2              // WiFi clients (if WiFi node)
    },
    // ... more nodes
  ],
  "thisNode": "0x5678",
  "meshSize": 5
}
```

**GET /api/status**
```json
{
  "nodeId": "0x5678",
  "uptime": 3600,               // Seconds
  "battery": {
    "percent": 85,
    "voltage": 3.95,
    "charging": false
  },
  "wifi": {
    "clients": 3,
    "ssid": "EMRG-NODE-5678"
  },
  "lora": {
    "frequency": 915.0,
    "bandwidth": 125,
    "spreadingFactor": 7,
    "txPower": 20,
    "channelUtilization": 12.5  // Percent
  },
  "memory": {
    "free": 125000,
    "total": 320000
  }
}
```

**POST /api/form** (Incident Report)
```json
// Request
{
  "formType": "incident",
  "timestamp": 1234567890,
  "location": {
    "lat": 37.7749,
    "lon": -122.4194,
    "gridRef": "12S UD 12345 67890"
  },
  "incident": {
    "type": "medical",          // medical | fire | hazmat | rescue
    "severity": "high",         // low | medium | high | critical
    "casualties": 2,
    "description": "Two victims with smoke inhalation"
  },
  "responder": {
    "badgeId": "E-1234",
    "name": "J. Smith",
    "unit": "Engine 5"
  }
}

// Response
{
  "status": "submitted",
  "formId": "0xFORM001",
  "broadcast": true             // Form sent to all nodes
}
```

#### 2.2.3 Handler Implementation Pattern

```cpp
void handleSendMessage(AsyncWebServerRequest *request) {
    // 1. Validate request
    if (!request->hasParam("dest", true) || !request->hasParam("message", true)) {
        request->send(400, "application/json",
            "{\"status\":\"error\",\"message\":\"Missing required fields\"}");
        return;
    }

    // 2. Parse parameters
    String dest = request->getParam("dest", true)->value();
    String msg = request->getParam("message", true)->value();
    String type = request->getParam("type", true) ?
                  request->getParam("type", true)->value() : "text";

    // 3. Create bridge packet
    BridgePacket pkt;
    pkt.dest_node = parseNodeID(dest);
    pkt.msg_type = parseMsgType(type);
    pkt.msg_id = generateMsgID();
    strncpy((char*)pkt.payload, msg.c_str(), 32);

    // 4. Send to mesh via bridge
    if (emergencyBridge.sendToMesh(&pkt)) {
        // 5. Success response
        DynamicJsonDocument doc(256);
        doc["status"] = "sent";
        doc["msgId"] = String(pkt.msg_id, HEX);
        doc["timestamp"] = millis();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        // 6. Error response
        request->send(500, "application/json",
            "{\"status\":\"error\",\"message\":\"Mesh send failed\"}");
    }
}
```

---

### 2.3 WebSocket Real-Time Layer

**File**: `src/wifi/WebSocketServer.h/.cpp`

#### 2.3.1 Requirements

- **R-WS-001**: Real-time bidirectional communication on port 81
- **R-WS-002**: Broadcast incoming mesh messages to all clients
- **R-WS-003**: Accept outgoing messages from clients
- **R-WS-004**: Support binary mode for voice data chunks
- **R-WS-005**: Heartbeat/ping every 30 seconds

#### 2.3.2 Message Protocol

**Client → Server (Outgoing)**
```json
{
  "type": "send",
  "dest": "0x1234",
  "msgType": "text",
  "payload": "Message content"
}

{
  "type": "ping",
  "timestamp": 1234567890
}

{
  "type": "subscribe",
  "events": ["messages", "nodes", "status"]
}
```

**Server → Client (Incoming)**
```json
// New message from mesh
{
  "event": "message",
  "from": "0x1234",
  "fromName": "Node-Alpha",
  "to": "0x5678",              // or "broadcast"
  "msgType": "text",
  "payload": "Message content",
  "timestamp": 1234567890,
  "rssi": -85,
  "hops": 2
}

// Node joined/left mesh
{
  "event": "node_update",
  "node": {
    "id": "0x9999",
    "name": "Node-Bravo",
    "status": "online",         // online | offline
    "lastSeen": 1234567890
  }
}

// Status update
{
  "event": "status",
  "battery": 75,
  "clients": 4,
  "meshSize": 8
}

// Delivery confirmation
{
  "event": "delivery",
  "msgId": "0xABCD",
  "status": "delivered",        // sent | delivered | failed
  "hops": 3
}

// Pong response
{
  "event": "pong",
  "timestamp": 1234567890
}
```

#### 2.3.3 WebSocket Handler

```cpp
void EmergencyWiFiService::setupWebSocket() {
    wsServer.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        switch(type) {
            case WStype_CONNECTED: {
                IPAddress ip = wsServer.remoteIP(num);
                LOG_INFO("WebSocket client %d connected from %s\n", num, ip.toString().c_str());

                // Register client
                clients[num].id = num;
                clients[num].ip = ip;
                clients[num].lastActivity = millis();

                // Send initial state
                sendMeshStatus(num);
                break;
            }

            case WStype_DISCONNECTED:
                LOG_INFO("WebSocket client %d disconnected\n", num);
                clients[num].id = 0xFF;  // Mark as inactive
                break;

            case WStype_TEXT: {
                // Parse JSON message
                DynamicJsonDocument doc(512);
                deserializeJson(doc, payload, length);

                String msgType = doc["type"];
                if (msgType == "send") {
                    handleClientSend(num, doc);
                } else if (msgType == "ping") {
                    sendPong(num);
                } else if (msgType == "subscribe") {
                    handleSubscribe(num, doc);
                }

                clients[num].lastActivity = millis();
                break;
            }

            case WStype_BIN:
                // Binary data (voice chunks)
                handleVoiceChunk(num, payload, length);
                break;
        }
    });

    wsServer.begin();
}

void EmergencyWiFiService::broadcastToClients(const char* json) {
    wsServer.broadcastTXT(json);
}

void EmergencyWiFiService::sendToClient(uint8_t clientID, const char* json) {
    wsServer.sendTXT(clientID, json);
}
```

---

### 2.4 Emergency WiFi Bridge Module

**File**: `src/modules/EmergencyWiFiBridge.h/.cpp`

#### 2.4.1 Requirements

- **R-BRIDGE-001**: Translate JSON WiFi messages to Meshtastic protobuf packets
- **R-BRIDGE-002**: Translate incoming mesh packets to JSON for WiFi clients
- **R-BRIDGE-003**: Maintain message ID mapping for delivery tracking
- **R-BRIDGE-004**: Support chunked messages (voice, images) up to 256 bytes
- **R-BRIDGE-005**: Implement message priority (emergency > high > normal)

#### 2.4.2 Bridge Packet Format

```cpp
// Simplified packet format for WiFi ↔ LoRa translation
struct BridgePacket {
    uint16_t dest_node;        // Destination node ID (0xFFFF = broadcast)
    uint16_t src_node;         // Source node ID
    uint16_t msg_id;           // Unique message ID
    uint8_t  msg_type;         // Message type (see enum below)
    uint8_t  chunk_info;       // Bits[7:4]=current, Bits[3:0]=total
    uint8_t  payload[32];      // Message payload
    uint16_t crc;              // CRC16 for error detection
} __attribute__((packed));     // 40 bytes total

// Message types
enum BridgeMessageType {
    MSG_TEXT = 0x01,           // Plain text message
    MSG_SOS = 0x02,            // Emergency SOS
    MSG_FORM = 0x03,           // Incident report form
    MSG_VOICE = 0x04,          // Voice clip chunk
    MSG_ACK = 0x05,            // Acknowledgment
    MSG_POSITION = 0x06,       // GPS position
    MSG_STATUS = 0x07          // Node status update
};
```

#### 2.4.3 Class Interface

```cpp
class EmergencyWiFiBridge : public MeshModule {
public:
    EmergencyWiFiBridge();

    // MeshModule interface
    void handleReceived(const MeshPacket &mp) override;
    bool wantPacket(const MeshPacket *p) override;

    // WiFi → Mesh
    bool sendToMesh(BridgePacket* pkt);
    bool sendTextMessage(uint16_t dest, const char* text);
    bool sendSOSMessage(uint16_t dest, const char* location);
    bool sendFormData(uint16_t dest, const uint8_t* formData, size_t len);
    bool sendVoiceChunk(uint16_t dest, const uint8_t* audioData, size_t len,
                        uint8_t chunkNum, uint8_t totalChunks);

    // Message tracking
    struct TrackedMessage {
        uint16_t msg_id;
        uint16_t dest_node;
        uint32_t timestamp;
        uint8_t  status;       // SENT | ACKED | FAILED
        uint8_t  hops;
    };

    TrackedMessage* getTrackedMessage(uint16_t msg_id);
    void clearOldMessages();   // Clear messages older than 10 min

private:
    EmergencyWiFiService* wifiService;
    TrackedMessage messageQueue[32];
    uint16_t nextMessageID;

    // Mesh → WiFi
    void forwardToWiFiClients(const MeshPacket &mp);
    void sendDeliveryConfirmation(uint16_t msg_id, uint8_t status, uint8_t hops);

    // Translation helpers
    void jsonToProtobuf(const DynamicJsonDocument& json, MeshPacket* pkt);
    void protobufToJson(const MeshPacket& pkt, DynamicJsonDocument& json);
    uint16_t generateMessageID();
    uint16_t calculateCRC(const BridgePacket* pkt);
};
```

#### 2.4.4 Translation Logic

**WiFi JSON → Mesh Protobuf**
```cpp
void EmergencyWiFiBridge::jsonToProtobuf(const DynamicJsonDocument& json, MeshPacket* pkt) {
    // Parse JSON
    uint16_t dest = parseNodeID(json["dest"]);
    String msgType = json["msgType"];
    String payload = json["payload"];

    // Create Meshtastic packet
    pkt->to = dest;
    pkt->from = nodeDB.getNodeNum();  // This node's ID
    pkt->id = generateMessageID();
    pkt->want_ack = true;              // Request delivery confirmation
    pkt->priority = json.containsKey("priority") ?
                   parsePriority(json["priority"]) :
                   MeshPacket_Priority_DEFAULT;

    // Set port based on message type
    if (msgType == "text") {
        pkt->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
    } else if (msgType == "sos") {
        pkt->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
        pkt->priority = MeshPacket_Priority_MAX;  // Emergency!
    } else if (msgType == "form") {
        pkt->decoded.portnum = PortNum_PRIVATE_APP;
    }

    // Copy payload
    size_t len = payload.length();
    memcpy(pkt->decoded.payload.bytes, payload.c_str(), min(len, (size_t)32));
    pkt->decoded.payload.size = min(len, (size_t)32);

    // Track message
    TrackedMessage tm;
    tm.msg_id = pkt->id;
    tm.dest_node = dest;
    tm.timestamp = millis();
    tm.status = MSG_STATUS_SENT;
    addTrackedMessage(tm);
}
```

**Mesh Protobuf → WiFi JSON**
```cpp
void EmergencyWiFiBridge::protobufToJson(const MeshPacket& pkt, DynamicJsonDocument& json) {
    json["event"] = "message";
    json["from"] = String(pkt.from, HEX);
    json["to"] = (pkt.to == BROADCAST_ADDR) ? "broadcast" : String(pkt.to, HEX);
    json["msgId"] = String(pkt.id, HEX);
    json["timestamp"] = millis();
    json["hops"] = pkt.hop_limit - pkt.hop_start;  // Hops so far

    // Add node name if known
    const NodeInfo* node = nodeDB.getNode(pkt.from);
    if (node && node->user.long_name[0]) {
        json["fromName"] = String(node->user.long_name);
    }

    // Message type and payload
    if (pkt.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
        json["msgType"] = "text";
        json["payload"] = String((char*)pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    } else if (pkt.decoded.portnum == PortNum_PRIVATE_APP) {
        json["msgType"] = "form";
        // Base64 encode binary form data
        json["payload"] = base64Encode(pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    }

    // Signal quality
    if (pkt.rx_rssi != 0) {
        json["rssi"] = pkt.rx_rssi;
        json["snr"] = pkt.rx_snr;
    }
}
```

---

### 2.5 Web UI Layer

**File**: `src/wifi/WebUI.h`

#### 2.5.1 Requirements

- **R-UI-001**: Single-page HTML/CSS/JS application embedded in firmware
- **R-UI-002**: Mobile-first responsive design (320px min width)
- **R-UI-003**: No external dependencies - works offline
- **R-UI-004**: WebSocket auto-reconnect on connection loss
- **R-UI-005**: Visual feedback for all user actions
- **R-UI-006**: Accessible - keyboard navigation, screen reader support

#### 2.5.2 UI Components

**Main Layout**
```
┌─────────────────────────────────────┐
│ Header: EMRG-NODE-5678 | 🔋85% | 📶 │
├─────────────────────────────────────┤
│ Mesh Status: 5 nodes | 3 clients    │
├─────────────────────────────────────┤
│                                      │
│  ┌────────────────────────────────┐ │
│  │   Message Feed (Scrollable)    │ │
│  │                                │ │
│  │  [Alpha] 12:34:                │ │
│  │  Medical needed at Grid 12S... │ │
│  │                                │ │
│  │  [Bravo] 12:35:                │ │
│  │  En route, ETA 5 min           │ │
│  │                                │ │
│  └────────────────────────────────┘ │
│                                      │
│  Destination: [Node Selector ▼]     │
│  Type: [Text ▼] [SOS!] [Form ▼]    │
│  Message: [________________]         │
│           [     Send      ]          │
│           [🎤 Voice] [📋 Form]       │
│                                      │
├─────────────────────────────────────┤
│ Tabs: [Messages] [Nodes] [Status]   │
└─────────────────────────────────────┘
```

#### 2.5.3 HTML Structure (Simplified)

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0">
    <title>Emergency Mesh Node</title>
    <style>
        /* Mobile-first CSS */
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: #1a1a1a;
            color: #f0f0f0;
            padding: 10px;
        }

        .header {
            background: #2a2a2a;
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 10px;
            display: flex;
            justify-content: space-between;
        }

        .status-bar {
            background: #333;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 10px;
        }

        .message-feed {
            height: 400px;
            overflow-y: scroll;
            background: #2a2a2a;
            padding: 10px;
            border-radius: 8px;
            margin-bottom: 10px;
        }

        .message {
            background: #3a3a3a;
            padding: 10px;
            margin-bottom: 8px;
            border-radius: 5px;
            border-left: 4px solid #4CAF50;
        }

        .message.sos {
            border-left-color: #f44336;
            background: #4a2a2a;
        }

        .compose {
            background: #2a2a2a;
            padding: 15px;
            border-radius: 8px;
        }

        select, input, textarea, button {
            width: 100%;
            padding: 12px;
            margin-bottom: 10px;
            border: 1px solid #555;
            border-radius: 5px;
            background: #333;
            color: #f0f0f0;
            font-size: 16px;
        }

        button {
            background: #4CAF50;
            border: none;
            cursor: pointer;
            font-weight: bold;
        }

        button:hover { background: #45a049; }
        button.sos { background: #f44336; }
        button.sos:hover { background: #da190b; }

        .connection-status {
            position: fixed;
            top: 10px;
            right: 10px;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #f44336;
        }

        .connection-status.connected { background: #4CAF50; }
    </style>
</head>
<body>
    <div class="connection-status" id="connStatus"></div>

    <div class="header">
        <div id="nodeInfo">EMRG-NODE-...</div>
        <div id="batteryInfo">🔋---%</div>
    </div>

    <div class="status-bar">
        Mesh: <span id="meshSize">0</span> nodes |
        Clients: <span id="clientCount">0</span>
    </div>

    <div class="message-feed" id="messageFeed"></div>

    <div class="compose">
        <select id="destNode">
            <option value="broadcast">📢 Broadcast to All</option>
        </select>

        <div style="display: flex; gap: 10px;">
            <select id="msgType" style="flex: 1;">
                <option value="text">💬 Text</option>
                <option value="sos">🆘 SOS</option>
            </select>
            <button class="sos" onclick="sendSOS()" style="flex: 1;">🆘 SOS!</button>
        </div>

        <textarea id="messageInput" rows="3" placeholder="Type message..."></textarea>

        <button onclick="sendMessage()">📤 Send Message</button>

        <div style="display: flex; gap: 10px;">
            <button onclick="recordVoice()" style="flex: 1;">🎤 Voice</button>
            <button onclick="showFormUI()" style="flex: 1;">📋 Incident Report</button>
        </div>
    </div>

    <script>
        // WebSocket connection
        let ws;
        let reconnectTimer;

        function connectWebSocket() {
            ws = new WebSocket('ws://192.168.4.1:81');

            ws.onopen = () => {
                console.log('Connected to node');
                document.getElementById('connStatus').classList.add('connected');
                clearTimeout(reconnectTimer);
            };

            ws.onclose = () => {
                console.log('Disconnected from node');
                document.getElementById('connStatus').classList.remove('connected');
                // Auto-reconnect after 3 seconds
                reconnectTimer = setTimeout(connectWebSocket, 3000);
            };

            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);
                handleIncomingMessage(data);
            };

            ws.onerror = (error) => {
                console.error('WebSocket error:', error);
            };
        }

        function handleIncomingMessage(data) {
            if (data.event === 'message') {
                addMessageToFeed(data);
            } else if (data.event === 'node_update') {
                updateNodeList(data.node);
            } else if (data.event === 'status') {
                updateStatus(data);
            } else if (data.event === 'delivery') {
                updateMessageDelivery(data);
            }
        }

        function sendMessage() {
            const dest = document.getElementById('destNode').value;
            const msgType = document.getElementById('msgType').value;
            const message = document.getElementById('messageInput').value;

            if (!message.trim()) {
                alert('Please enter a message');
                return;
            }

            const packet = {
                type: 'send',
                dest: dest,
                msgType: msgType,
                payload: message,
                priority: msgType === 'sos' ? 'emergency' : 'normal'
            };

            ws.send(JSON.stringify(packet));
            document.getElementById('messageInput').value = '';
        }

        function sendSOS() {
            const location = getLocationString();
            const message = `🆘 EMERGENCY SOS from responder at ${location}`;

            const packet = {
                type: 'send',
                dest: 'broadcast',
                msgType: 'sos',
                payload: message,
                priority: 'emergency'
            };

            ws.send(JSON.stringify(packet));

            // Visual feedback
            const btn = event.target;
            btn.textContent = '✓ SOS SENT!';
            setTimeout(() => { btn.textContent = '🆘 SOS!'; }, 2000);
        }

        function addMessageToFeed(msg) {
            const feed = document.getElementById('messageFeed');
            const msgDiv = document.createElement('div');
            msgDiv.className = 'message' + (msg.msgType === 'sos' ? ' sos' : '');

            const time = new Date(msg.timestamp).toLocaleTimeString();
            const from = msg.fromName || msg.from;

            msgDiv.innerHTML = `
                <div style="font-size: 0.85em; color: #999; margin-bottom: 5px;">
                    <strong>${from}</strong> | ${time} | RSSI: ${msg.rssi || 'N/A'} dBm
                </div>
                <div>${msg.payload}</div>
            `;

            feed.appendChild(msgDiv);
            feed.scrollTop = feed.scrollHeight;  // Auto-scroll to bottom
        }

        function updateNodeList(node) {
            const selector = document.getElementById('destNode');

            // Check if node already in list
            let option = selector.querySelector(`option[value="${node.id}"]`);
            if (!option) {
                option = document.createElement('option');
                option.value = node.id;
                selector.appendChild(option);
            }

            option.textContent = `${node.name || node.id} (${node.rssi} dBm, ${node.hops} hops)`;

            if (node.status === 'offline') {
                option.disabled = true;
                option.textContent += ' [OFFLINE]';
            }
        }

        function updateStatus(status) {
            document.getElementById('nodeInfo').textContent = `EMRG-NODE-${status.nodeId}`;
            document.getElementById('batteryInfo').textContent = `🔋${status.battery?.percent || 0}%`;
            document.getElementById('meshSize').textContent = status.meshSize || 0;
            document.getElementById('clientCount').textContent = status.wifi?.clients || 0;
        }

        function getLocationString() {
            // Try to get GPS location if available
            if (navigator.geolocation) {
                navigator.geolocation.getCurrentPosition(
                    (pos) => {
                        return `${pos.coords.latitude.toFixed(4)}, ${pos.coords.longitude.toFixed(4)}`;
                    },
                    () => {
                        return 'Location unknown';
                    }
                );
            }
            return 'Location unavailable';
        }

        // Voice recording (simplified)
        let mediaRecorder;
        let audioChunks = [];

        function recordVoice() {
            if (!mediaRecorder) {
                navigator.mediaDevices.getUserMedia({ audio: true })
                    .then(stream => {
                        mediaRecorder = new MediaRecorder(stream);
                        mediaRecorder.ondataavailable = (e) => {
                            audioChunks.push(e.data);
                        };
                        mediaRecorder.onstop = sendVoiceMessage;

                        mediaRecorder.start();
                        event.target.textContent = '⏹ Stop Recording';
                    })
                    .catch(err => {
                        alert('Microphone access denied: ' + err);
                    });
            } else {
                mediaRecorder.stop();
                mediaRecorder = null;
                event.target.textContent = '🎤 Voice';
            }
        }

        function sendVoiceMessage() {
            const audioBlob = new Blob(audioChunks, { type: 'audio/webm' });
            audioChunks = [];

            // Send via binary WebSocket or chunk into multiple packets
            // Implementation depends on voice codec and mesh bandwidth
            // For now, just show confirmation
            alert('Voice message sent (implementation pending)');
        }

        function showFormUI() {
            // TODO: Show incident report form overlay
            alert('Incident report form (implementation pending)');
        }

        // Initialize on page load
        window.addEventListener('load', () => {
            connectWebSocket();

            // Fetch initial node list
            fetch('/api/nodes')
                .then(r => r.json())
                .then(data => {
                    data.nodes.forEach(updateNodeList);
                });

            // Fetch initial status
            fetch('/api/status')
                .then(r => r.json())
                .then(updateStatus);
        });
    </script>
</body>
</html>
```

---

## 3. Power Management

### 3.1 Power States

```cpp
enum PowerState {
    POWER_ON,           // WiFi AP active, LoRa RX, Display on
    POWER_STANDBY,      // WiFi off, LoRa RX, Display dim
    POWER_SLEEP         // WiFi off, LoRa wakeup, Display off
};

// Power consumption estimates
// ON:        ~240 mA (WiFi AP + LoRa RX + Display)
// STANDBY:   ~25 mA (LoRa RX + Display dim)
// SLEEP:     ~5 mA (LoRa wakeup only)
```

### 3.2 State Transitions

```cpp
void PowerFSM::updateState() {
    switch (currentState) {
        case POWER_ON:
            // Transition to STANDBY if no WiFi clients for 5 min
            if (wifiService.getClientCount() == 0 &&
                millis() - lastClientActivity > WIFI_IDLE_TIMEOUT_MS) {
                transitionTo(POWER_STANDBY);
            }
            break;

        case POWER_STANDBY:
            // Wake to ON if button pressed or LoRa message received
            if (buttonPressed() || loraActivity) {
                transitionTo(POWER_ON);
            }
            // Sleep if no activity for 15 min
            else if (millis() - lastActivity > SLEEP_TIMEOUT_MS) {
                transitionTo(POWER_SLEEP);
            }
            break;

        case POWER_SLEEP:
            // Wake on button or LoRa RX interrupt
            if (buttonPressed() || loraWakeup) {
                transitionTo(POWER_STANDBY);
            }
            break;
    }
}

void PowerFSM::transitionTo(PowerState newState) {
    switch (newState) {
        case POWER_ON:
            wifiService.start();
            screen.turnOn();
            break;

        case POWER_STANDBY:
            wifiService.stop();
            screen.dim();
            break;

        case POWER_SLEEP:
            screen.turnOff();
            esp_deep_sleep_start();
            break;
    }
    currentState = newState;
}
```

### 3.3 Battery Monitoring

```cpp
class BatteryMonitor {
public:
    void init() {
        analogReadResolution(12);  // 12-bit ADC
        adcAttachPin(BAT_VOLTAGE_PIN);
    }

    float getVoltage() {
        int raw = analogRead(BAT_VOLTAGE_PIN);
        // Heltec V2: 100k + 100k divider
        return (raw / 4095.0) * 3.3 * 2.0;
    }

    uint8_t getPercent() {
        float v = getVoltage();
        // LiPo voltage curve approximation
        if (v >= 4.2) return 100;
        if (v >= 4.0) return 80 + (v - 4.0) * 100;
        if (v >= 3.7) return 50 + (v - 3.7) * 100;
        if (v >= 3.4) return 20 + (v - 3.4) * 100;
        if (v >= 3.0) return (v - 3.0) * 50;
        return 0;
    }

    bool isCharging() {
        // Check USB power via GPIO
        return digitalRead(USB_POWER_PIN) == HIGH;
    }

    bool isCritical() {
        return getPercent() < 10;
    }
};
```

---

## 4. Build Configuration

### 4.1 PlatformIO Environment

**File**: `variants/esp32/heltec_v2/platformio.ini`

```ini
[env:heltec-v2-emergency-wifi]
extends = env:heltec-v2.1

build_flags =
  ${env:heltec-v2.1.build_flags}

  ; Disable standard Meshtastic features
  -DDISABLE_BLUETOOTH=1
  -DMESHTASTIC_EXCLUDE_ENVIRONMENTAL=1
  -DMESHTASTIC_EXCLUDE_DETECTION_SENSOR=1
  -DMESHTASTIC_EXCLUDE_POWERSTRESS=1
  -DMESHTASTIC_EXCLUDE_REMOTEHARDWARE=1
  -DMESHTASTIC_EXCLUDE_HEALTH_TELEMETRY=1
  -DMESHTASTIC_EXCLUDE_GENERIC_THREAD_MODULE=1

  ; Enable emergency WiFi features
  -DENABLE_WIFI_AP=1
  -DENABLE_EMERGENCY_BRIDGE=1
  -DENABLE_VOICE_CLIPS=1
  -DENABLE_INCIDENT_FORMS=1

  ; WiFi configuration
  -DWIFI_AP_SSID_PREFIX=\"EMRG-NODE-\"
  -DWIFI_AP_PASSWORD=\"emergency123\"
  -DWIFI_MAX_CLIENTS=6
  -DWIFI_CHANNEL=6

  ; Debug options
  -DDEBUG_WIFI=1
  -DLOG_LEVEL=LOG_LEVEL_DEBUG

lib_deps =
  ${env:heltec-v2.1.lib_deps}
  ; WiFi dependencies
  ESP Async WebServer
  WebSockets
  ArduinoJson
  ; Remove BLE libraries
  -NimBLE-Arduino

src_filter =
  ${env.src_filter}
  ; Remove BLE
  -<src/bluetooth/>
  -<src/nimble/>
  ; Remove unused modules
  -<src/modules/EnvironmentalMeasurementPlugin.cpp>
  -<src/modules/DetectionSensorModule.cpp>
  -<src/modules/HealthPlugin.cpp>
  -<src/modules/StoreForwardModule.cpp>
  ; Add WiFi components
  +<src/wifi/>
  +<src/modules/EmergencyWiFiBridge.cpp>

board_build.partitions = partition-emergency-wifi.csv
```

### 4.2 Partition Table

**File**: `partition-emergency-wifi.csv`

```csv
# Name,     Type, SubType, Offset,  Size,     Flags
nvs,        data, nvs,     0x9000,  0x5000,
otadata,    data, ota,     0xe000,  0x2000,
app0,       app,  ota_0,   0x10000, 0x1C0000,
app1,       app,  ota_1,   0x1D0000,0x1C0000,
spiffs,     data, spiffs,  0x390000,0x70000,
```

Partition sizes:
- **app0/app1**: 1.75 MB each (OTA firmware)
- **spiffs**: 448 KB (web UI, logs, config)
- Total: 4 MB flash

---

## 5. Implementation Roadmap

### Phase 1: Core WiFi Infrastructure (Week 1-2)

**Tasks**:
1. ✅ Create `src/wifi/` directory structure
2. ✅ Implement `EmergencyWiFiService` class
3. ✅ Setup WiFi AP with fixed SSID/IP
4. ✅ Implement basic HTTP server (serve static HTML)
5. ✅ Implement WebSocket server (echo test)
6. ✅ Test: Connect phone, load webpage, send WebSocket message

**Deliverable**: Phone can connect to ESP32 AP and load a basic webpage

### Phase 2: Bridge Module (Week 3)

**Tasks**:
1. ✅ Create `EmergencyWiFiBridge` module
2. ✅ Define `BridgePacket` structure
3. ✅ Implement WiFi → Mesh translation
4. ✅ Implement Mesh → WiFi translation
5. ✅ Integrate with Meshtastic `MeshService`
6. ✅ Test: Send text message from phone, receive on another node via LoRa

**Deliverable**: Bidirectional message flow between WiFi and LoRa mesh

### Phase 3: REST API (Week 4)

**Tasks**:
1. ✅ Implement `/api/send` endpoint
2. ✅ Implement `/api/nodes` endpoint
3. ✅ Implement `/api/status` endpoint
4. ✅ Add error handling and validation
5. ✅ Test with curl commands

**Deliverable**: Full REST API functional

### Phase 4: Web UI (Week 5-6)

**Tasks**:
1. ✅ Design mobile-first UI layout
2. ✅ Implement message feed (WebSocket updates)
3. ✅ Implement message compose and send
4. ✅ Implement node list dropdown
5. ✅ Implement SOS button
6. ✅ Add visual feedback and error handling
7. ✅ Embed HTML in `WebUI.h` as raw string
8. ✅ Test on multiple phones (iOS Safari, Android Chrome)

**Deliverable**: Fully functional web interface

### Phase 5: Emergency Features (Week 7)

**Tasks**:
1. ⏳ Implement voice recording and chunking
2. ⏳ Implement incident report form
3. ⏳ Add GPS location integration
4. ⏳ Implement message history/log
5. ⏳ Test end-to-end emergency scenarios

**Deliverable**: Emergency-specific features working

### Phase 6: Power Management (Week 8)

**Tasks**:
1. ⏳ Implement WiFi idle timeout
2. ⏳ Implement sleep modes
3. ⏳ Add battery monitoring
4. ⏳ Display power status on OLED
5. ⏳ Measure and optimize power consumption

**Deliverable**: 8+ hours battery life with periodic WiFi use

### Phase 7: Testing & Refinement (Week 9-10)

**Tasks**:
1. ⏳ Field testing with 3+ nodes
2. ⏳ Stress testing (6 simultaneous WiFi clients)
3. ⏳ Range testing (LoRa mesh)
4. ⏳ UI/UX refinements based on feedback
5. ⏳ Performance optimization
6. ⏳ Documentation and deployment guide

**Deliverable**: Production-ready firmware

---

## 6. Testing Strategy

### 6.1 Unit Tests

- WiFi AP connection/disconnection
- REST API endpoint responses
- WebSocket message parsing
- Bridge packet translation
- Message ID generation and tracking

### 6.2 Integration Tests

- WiFi → LoRa message delivery
- LoRa → WiFi message broadcast
- Multi-node mesh routing
- Client session management
- Power state transitions

### 6.3 Field Tests

- 3+ node mesh network
- 6 simultaneous WiFi clients
- Message delivery latency measurement
- Power consumption profiling
- UI usability testing with actual responders
- Range testing (LoRa and WiFi)

### 6.4 Acceptance Criteria

- ✅ WiFi AP discoverable and connectable on all phones
- ✅ Web UI loads in <2 seconds
- ✅ Messages deliver across mesh in <5 seconds (3 hops)
- ✅ SOS messages have <1 second latency
- ✅ 6+ concurrent WiFi clients supported
- ✅ 8+ hours battery life with moderate use
- ✅ WebSocket reconnect on connection loss
- ✅ No crashes after 24 hours continuous operation

---

## 7. Dependencies

### 7.1 Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| ESPAsyncWebServer | 1.2.3+ | HTTP server |
| WebSockets | 2.4.0+ | WebSocket server |
| ArduinoJson | 6.21.0+ | JSON parsing |
| RadioLib | 7.4.0+ | LoRa radio (existing) |
| Nanopb | 0.4.91 | Protobuf (existing) |

### 7.2 Hardware Requirements

| Component | Specification |
|-----------|---------------|
| MCU | ESP32 240MHz dual-core |
| RAM | 320KB minimum |
| Flash | 4MB minimum |
| LoRa Radio | SX1276/SX1278 |
| WiFi | 802.11 b/g/n 2.4GHz |
| Display | SSD1306 OLED 128x64 |
| Battery | 3.7V LiPo 1000mAh+ |

---

## 8. Security Considerations

### 8.1 WiFi Security

- **WPA2 password**: Default "emergency123" - should be changed per deployment
- **Hidden SSID option**: Available but defaults to visible for discovery
- **MAC filtering**: Optional, not enabled by default
- **Client limit**: Hard cap at 6 to prevent resource exhaustion

### 8.2 Mesh Security

- **AES-256 encryption**: Inherited from Meshtastic (unchanged)
- **Channel keys**: Pre-shared keys per mesh network
- **No authentication**: Emergency use case - rely on physical access control

### 8.3 Recommendations

- Change WiFi password for each deployment
- Use separate mesh channel keys for different incidents
- Physically secure nodes to prevent tampering
- Consider adding PIN code to web UI for sensitive deployments

---

## 9. Performance Targets

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| WiFi connection time | <3 sec | Phone WiFi settings |
| Web UI load time | <2 sec | Browser dev tools |
| Message send latency (1 hop) | <1 sec | Timestamp diff |
| Message send latency (3 hops) | <5 sec | Timestamp diff |
| SOS latency | <1 sec | Critical priority |
| WebSocket reconnect | <3 sec | Auto-retry test |
| Max concurrent clients | 6+ | Stress test |
| Battery life (active WiFi) | 4+ hours | Discharge test |
| Battery life (standby) | 24+ hours | Discharge test |
| LoRa range (line of sight) | 5+ km | Field test |
| WiFi range (outdoor) | 50+ m | Field test |

---

## 10. Future Enhancements

**Not in initial scope but worth considering**:

1. **HTTPS support**: Self-signed certificate for encrypted WiFi connection
2. **Multi-language UI**: Spanish, French for international deployments
3. **Offline maps**: Embedded map tiles for location display
4. **Message encryption**: End-to-end encryption on WiFi side
5. **File attachments**: Photos, documents via WiFi upload
6. **Mesh topology visualization**: Graph view of node connections
7. **Firmware OTA updates**: Update firmware over WiFi
8. **External sensors**: Temperature, gas detection, radiation
9. **Integration with CAD systems**: Computer-Aided Dispatch APIs
10. **Voice calls**: Real-time voice over mesh (challenging with LoRa bandwidth)

---

## Appendix A: File Structure

```
firmware/
├── src/
│   ├── wifi/                          # NEW
│   │   ├── EmergencyWiFiService.h
│   │   ├── EmergencyWiFiService.cpp
│   │   ├── APIHandlers.h
│   │   ├── APIHandlers.cpp
│   │   ├── WebSocketServer.h
│   │   ├── WebSocketServer.cpp
│   │   └── WebUI.h                    # Embedded HTML
│   ├── modules/
│   │   ├── EmergencyWiFiBridge.h      # NEW
│   │   ├── EmergencyWiFiBridge.cpp    # NEW
│   │   ├── RoutingModule.cpp          # KEEP
│   │   └── AdminModule.cpp            # KEEP
│   ├── mesh/                          # UNCHANGED
│   │   ├── RadioInterface.cpp
│   │   ├── FloodingRouter.cpp
│   │   ├── ReliableRouter.cpp
│   │   └── CryptoEngine.cpp
│   ├── PowerFSM.cpp                   # MODIFIED
│   └── configuration.h                # MODIFIED
├── variants/
│   └── esp32/
│       └── heltec_v2/
│           └── platformio.ini         # MODIFIED
├── CLAUDE.md                          # This file
├── DESIGN.md                          # Design spec
└── README.md                          # Updated docs
```

---

**Document Status**: Draft v1.0
**Last Updated**: 2025-01-14
**Author**: Claude Code
**Review Status**: Pending implementation
