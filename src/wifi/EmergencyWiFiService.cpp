#include "EmergencyWiFiService.h"

#ifdef ENABLE_WIFI_AP

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "modules/EmergencyWiFiBridge.h"

EmergencyWiFiService wifiService;

EmergencyWiFiService::EmergencyWiFiService()
    : httpServer(80), wsServer(81), apActive(false), clientCount(0), lastClientActivity(0) {
}

void EmergencyWiFiService::init() {
    Serial.println("\n=== Emergency WiFi Service Initialization ===");

    // Initialize LittleFS for web app files
    Serial.println("Step 1: Mounting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed");
        return;
    }
    Serial.println("✓ LittleFS mounted successfully");

    // List files in LittleFS for debugging
    Serial.println("\nStep 2: Checking web app files...");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }

    // Check webapp directory specifically
    File webappDir = LittleFS.open("/webapp");
    if (webappDir && webappDir.isDirectory()) {
        Serial.println("Files in /webapp:");
        File webappFile = webappDir.openNextFile();
        while (webappFile) {
            Serial.printf("  %s (%d bytes)\n", webappFile.name(), webappFile.size());
            webappFile = webappDir.openNextFile();
        }
    } else {
        Serial.println("WARNING: /webapp directory not found!");
    }

    // NOTE: WiFi AP already created by WiFiMeshBridge - skip setupWiFiAP()

    // Setup web server
    Serial.println("\nStep 3: Starting HTTP server (port 80)...");
    setupWebServer();
    Serial.println("✓ HTTP server started");

    // Setup WebSocket
    Serial.println("\nStep 4: Starting WebSocket server (port 81)...");
    setupWebSocket();
    Serial.println("✓ WebSocket server started");

    apActive = true;
    Serial.println("\n=== Emergency WiFi Service Ready ===");
    Serial.println("Access web UI at: http://192.168.4.1/");
    Serial.println("WebSocket available at: ws://192.168.4.1:81");
    Serial.println("=========================================\n");
}

void EmergencyWiFiService::setupWiFiAP() {
    // Create AP with MAC-based SSID (unique per device)
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "EMRG-NODE-%02X%02X", mac[4], mac[5]);

    Serial.printf("Starting WiFi AP: %s (OPEN - no password)\n", ssid);
    Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, NULL, 6, 0, 4); // Channel 6, not hidden, max 4 clients, no password

    // Configure static IP
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("ERROR: AP Config Failed");
        return;
    }

    Serial.printf("WiFi AP started successfully\n");
    Serial.printf("IP Address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("Connect to: %s (OPEN - no password required)\n", ssid);
    Serial.printf("Then browse to: http://192.168.4.1\n");
}

void EmergencyWiFiService::setupWebServer() {
    // Simple send endpoint - use like: http://192.168.4.1/send?msg=hello
    httpServer.on("/send", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("msg")) {
            request->send(400, "text/plain", "Missing 'msg' parameter. Use: /send?msg=your_message");
            return;
        }

        String msg = request->getParam("msg")->value();
        Serial.printf("Sending to mesh: %s\n", msg.c_str());

        if (emergencyWiFiBridge && emergencyWiFiBridge->sendTextToMesh(msg.c_str())) {
            request->send(200, "text/plain", "Sent: " + msg);
        } else {
            request->send(500, "text/plain", "Failed to send");
        }
    });

    // Test endpoint (still works for quick testing)
    httpServer.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Test endpoint hit!");
        if (emergencyWiFiBridge && emergencyWiFiBridge->sendTextToMesh("Test message")) {
            request->send(200, "text/plain", "Test message sent!");
        } else {
            request->send(500, "text/plain", "Failed");
        }
    });

    // Root path - serve interactive web UI
    httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Root path requested - serving web UI");

        // Use String to avoid stack overflow with large HTML
        String html = R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>Emergency Mesh</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; padding: 20px; background: #1e1e1e; color: #00ff00; max-width: 600px; margin: 0 auto; }
        h1 { color: #00ff00; border-bottom: 2px solid #00ff00; padding-bottom: 10px; }
        #status { padding: 10px; margin: 10px 0; border-radius: 5px; font-weight: bold; }
        #status.connected { background: #004400; color: #00ff00; }
        #status.disconnected { background: #440000; color: #ff0000; }
        #userInfo { padding: 10px; margin: 10px 0; background: #003300; border: 1px solid #00ff00; border-radius: 5px; display: flex; justify-content: space-between; align-items: center; }
        #currentUser { color: #00ff00; font-weight: bold; }
        #changeUserBtn { padding: 5px 10px; background: #006600; color: #00ff00; border: 1px solid #00ff00; cursor: pointer; border-radius: 3px; font-size: 12px; }
        #messages { border: 1px solid #00ff00; padding: 10px; height: 300px; overflow-y: scroll; margin: 10px 0; background: #000; border-radius: 5px; }
        .message { margin: 8px 0; padding: 8px; border-radius: 5px; }
        .message .username { font-weight: bold; margin-right: 5px; }
        .message .timestamp { color: #666; font-size: 11px; margin-right: 5px; }
        .message .text { display: inline; }
        .sent { background: #332200; border-left: 3px solid #ffff00; }
        .sent .username { color: #ffff00; }
        .sent .text { color: #ffff00; }
        .received { background: #002200; border-left: 3px solid #00ff00; }
        .received .username { color: #00ff00; }
        .received .text { color: #00ff00; }
        .other-user { background: #001a33; border-left: 3px solid #0088ff; }
        .other-user .username { color: #0088ff; }
        .other-user .text { color: #00aaff; }
        .error { color: #ff0000; background: #220000; border-left: 3px solid #ff0000; }
        .info { color: #00aaff; background: #001122; border-left: 3px solid #00aaff; }
        .input-group { margin: 10px 0; }
        input { padding: 10px; width: calc(100% - 22px); background: #000; color: #00ff00; border: 1px solid #00ff00; border-radius: 5px; font-size: 16px; }
        button { padding: 10px 20px; background: #00ff00; color: #000; border: none; cursor: pointer; margin: 5px 5px 5px 0; border-radius: 5px; font-weight: bold; }
        button:active { background: #00aa00; }
        .stats { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin: 10px 0; }
        .stat { background: #000; padding: 10px; border: 1px solid #00ff00; border-radius: 5px; text-align: center; }
        .stat-value { font-size: 24px; font-weight: bold; color: #00ff00; }
        .stat-label { font-size: 12px; color: #00aa00; }
        #usernameModal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.9); z-index: 1000; }
        #usernameModal.show { display: flex; align-items: center; justify-content: center; }
        #usernamePrompt { background: #1e1e1e; padding: 30px; border: 2px solid #00ff00; border-radius: 10px; max-width: 400px; }
        #usernamePrompt h2 { color: #00ff00; margin-top: 0; }
        #usernamePrompt input { width: calc(100% - 22px); margin: 10px 0; }
    </style>
</head>
<body>
    <!-- Username Modal -->
    <div id="usernameModal">
        <div id="usernamePrompt">
            <h2>Welcome to Emergency Mesh</h2>
            <p style="color: #00aa00;">Enter your name to start chatting:</p>
            <input type="text" id="usernameInput" placeholder="Your name..." maxlength="20">
            <button onclick="setUsername()" style="width: 100%; margin-top: 10px;">Start Chatting</button>
        </div>
    </div>

    <h1>Emergency Mesh Network</h1>
    <div id="status" class="disconnected">Connecting...</div>

    <div id="userInfo">
        <span>Logged in as: <span id="currentUser"></span></span>
        <button id="changeUserBtn" onclick="changeUsername()">Change Name</button>
    </div>

    <div class="stats">
        <div class="stat">
            <div class="stat-value" id="sentCount">0</div>
            <div class="stat-label">SENT</div>
        </div>
        <div class="stat">
            <div class="stat-value" id="receivedCount">0</div>
            <div class="stat-label">RECEIVED</div>
        </div>
    </div>

    <div id="messages"></div>

    <div class="input-group">
        <input type="text" id="messageInput" placeholder="Type message and press Enter...">
        <button onclick="sendMessage()">Send Message</button>
    </div>

    <script>
        let ws;
        let sentCount = 0;
        let receivedCount = 0;
        let username = localStorage.getItem('emergencyMeshUsername') || '';
        const messages = document.getElementById('messages');
        const status = document.getElementById('status');
        const input = document.getElementById('messageInput');

        function setUsername() {
            const nameInput = document.getElementById('usernameInput');
            const name = nameInput.value.trim();
            if (!name) {
                alert('Please enter a name');
                return;
            }
            username = name;
            localStorage.setItem('emergencyMeshUsername', username);
            document.getElementById('currentUser').textContent = username;
            document.getElementById('usernameModal').classList.remove('show');
            connect();
        }

        function changeUsername() {
            document.getElementById('usernameInput').value = username;
            document.getElementById('usernameModal').classList.add('show');
            document.getElementById('usernameInput').focus();
        }

        function log(msg, className = '', user = '', time = null) {
            const div = document.createElement('div');
            div.className = 'message ' + className;

            const timestamp = time || new Date().toLocaleTimeString();
            const timeSpan = document.createElement('span');
            timeSpan.className = 'timestamp';
            timeSpan.textContent = timestamp;
            div.appendChild(timeSpan);

            if (user) {
                const userSpan = document.createElement('span');
                userSpan.className = 'username';
                userSpan.textContent = user + ':';
                div.appendChild(userSpan);
            }

            const textSpan = document.createElement('span');
            textSpan.className = 'text';
            textSpan.textContent = msg;
            div.appendChild(textSpan);

            messages.appendChild(div);
            messages.scrollTop = messages.scrollHeight;
        }

        function updateStatus(text, connected) {
            status.textContent = text;
            status.className = connected ? 'connected' : 'disconnected';
        }

        function updateStats() {
            document.getElementById('sentCount').textContent = sentCount;
            document.getElementById('receivedCount').textContent = receivedCount;
        }

        function connect() {
            ws = new WebSocket('ws://' + window.location.hostname + ':81');

            ws.onopen = () => {
                updateStatus('Connected to Emergency Mesh', true);
                log('WebSocket connected', 'info');
            };

            ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === 'message') {
                        receivedCount++;
                        updateStats();

                        const msgUser = data.username || 'Unknown';
                        const isFromMe = msgUser === username;

                        // Determine message class based on source and user
                        let msgClass = 'received';
                        if (data.source === 'wifi') {
                            msgClass = isFromMe ? 'sent' : 'other-user';
                            log(data.text, msgClass, msgUser);
                        } else {
                            // LoRa message
                            const loraInfo = ' (LoRa: ' + data.from + ', RSSI: ' + data.rssi + ' dBm)';
                            log(data.text + loraInfo, 'received', msgUser);
                        }
                    } else if (data.type === 'node_info') {
                        log('Node ID: ' + data.nodeId, 'info');
                    } else {
                        log('Data: ' + event.data, 'info');
                    }
                } catch(e) {
                    log('Received: ' + event.data, 'received');
                }
            };

            ws.onerror = () => {
                log('WebSocket error', 'error');
                updateStatus('Connection error', false);
            };

            ws.onclose = () => {
                updateStatus('Disconnected - reconnecting...', false);
                log('WebSocket closed, reconnecting in 3s...', 'error');
                setTimeout(connect, 3000);
            };
        }

        function sendMessage() {
            const msg = input.value.trim();
            if (!msg) return;

            if (!ws || ws.readyState !== WebSocket.OPEN) {
                log('Error: Not connected to WebSocket', 'error');
                return;
            }

            // Send via WebSocket (will broadcast to all WiFi clients + LoRa mesh)
            const msgObj = {
                text: msg,
                username: username,
                timestamp: Date.now()
            };
            ws.send(JSON.stringify(msgObj));

            sentCount++;
            updateStats();
            log(msg, 'sent', username);
            input.value = '';
        }

        // Send on Enter key
        input.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') sendMessage();
        });

        // Username input - send on Enter
        document.getElementById('usernameInput').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') setUsername();
        });

        // Auto-connect on load or show username modal
        if (username) {
            document.getElementById('currentUser').textContent = username;
            connect();
            log('Emergency Mesh Web UI loaded', 'info');
            log('Waiting for messages...', 'info');
        } else {
            document.getElementById('usernameModal').classList.add('show');
            document.getElementById('usernameInput').focus();
        }
    </script>
</body>
</html>)HTML";

        request->send(200, "text/html", html);
    });

    // Log all other requests
    httpServer.onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("Request: %s %s\n", request->methodToString(), request->url().c_str());
        request->send(404, "text/plain", "Not found: " + request->url());
    });

    httpServer.begin();
    Serial.println("HTTP server started on port 80");
    Serial.println("Try: http://192.168.4.1/test");
}

void EmergencyWiFiService::setupWebSocket() {
    wsServer.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        this->handleWebSocketEvent(num, type, payload, length);
    });

    wsServer.begin();
    Serial.println("WebSocket server started on port 81");
}

void EmergencyWiFiService::handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(num);
            Serial.printf("WebSocket[%u] connected from %s\n", num, ip.toString().c_str());
            clientCount++;
            lastClientActivity = millis();

            // Send node info to new client
            sendNodeInfo(num);
            break;
        }

        case WStype_DISCONNECTED:
            Serial.printf("WebSocket[%u] disconnected\n", num);
            if (clientCount > 0) clientCount--;
            lastClientActivity = millis();
            break;

        case WStype_TEXT:
            Serial.printf("WebSocket[%u] received: %s\n", num, payload);
            handleClientMessage(num, (char*)payload);
            lastClientActivity = millis();
            break;

        case WStype_ERROR:
            Serial.printf("WebSocket[%u] error\n", num);
            break;

        case WStype_PING:
        case WStype_PONG:
            // Ignore ping/pong
            break;

        default:
            break;
    }
}

void EmergencyWiFiService::handleClientMessage(uint8_t clientId, const char* json) {
    Serial.printf("Handling message from client %u: %s\n", clientId, json);

    // Parse the incoming message
    DynamicJsonDocument inDoc(512);
    DeserializationError error = deserializeJson(inDoc, json);

    if (error) {
        Serial.printf("Failed to parse message: %s\n", error.c_str());
        return;
    }

    // Extract message text and username
    const char* msgText = inDoc["text"] | inDoc["msg"] | json;  // Try multiple formats
    const char* userName = inDoc["username"] | "Unknown";

    if (!msgText || strlen(msgText) == 0) {
        Serial.println("Empty message, ignoring");
        return;
    }

    Serial.printf("Message from '%s': %s\n", userName, msgText);

    // 1. Broadcast to all WiFi clients (local chat on same AP)
    DynamicJsonDocument outDoc(512);
    outDoc["type"] = "message";
    outDoc["from"] = "local";  // From another WiFi client on same AP
    outDoc["username"] = userName;  // Include username
    outDoc["text"] = msgText;
    outDoc["timestamp"] = millis();
    outDoc["rssi"] = 0;  // WiFi messages don't have RSSI
    outDoc["snr"] = 0;   // WiFi messages don't have SNR
    outDoc["source"] = "wifi";  // Mark as WiFi-originated

    String outJson;
    serializeJson(outDoc, outJson);

    broadcastToClients(outJson.c_str());
    Serial.printf("Broadcasted to WiFi clients: %s\n", outJson.c_str());

    // 2. Forward to LoRa mesh (if bridge module exists)
    if (emergencyWiFiBridge) {
        if (emergencyWiFiBridge->sendTextToMesh(msgText)) {
            Serial.println("Also sent to LoRa mesh");
        } else {
            Serial.println("Failed to send to LoRa mesh");
        }
    }
}

void EmergencyWiFiService::sendNodeInfo(uint8_t clientId) {
    DynamicJsonDocument doc(256);
    doc["type"] = "node_info";
    doc["nodeId"] = String(getNodeId(), HEX);
    doc["name"] = "Test Node";
    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);

    wsServer.sendTXT(clientId, json);
    Serial.printf("Sent node info to client %u: %s\n", clientId, json.c_str());
}

void EmergencyWiFiService::loop() {
    wsServer.loop();
}

void EmergencyWiFiService::stop() {
    wsServer.disconnect();
    httpServer.end();
    WiFi.softAPdisconnect(true);
    apActive = false;
    Serial.println("Emergency WiFi Service stopped");
}

void EmergencyWiFiService::broadcastToClients(const char* json) {
    wsServer.broadcastTXT(json);
    Serial.printf("Broadcast to all clients: %s\n", json);
}

void EmergencyWiFiService::sendToClient(uint8_t clientId, const char* json) {
    wsServer.sendTXT(clientId, json);
    Serial.printf("Sent to client %u: %s\n", clientId, json);
}

uint32_t EmergencyWiFiService::getNodeId() {
    // Use last 2 bytes of MAC address as node ID
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return (mac[4] << 8) | mac[5];
}

#endif // ENABLE_WIFI_AP
