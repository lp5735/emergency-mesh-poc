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

    // Root path - just show instructions
    httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Root path requested");
        String html = "<h1>Emergency Mesh</h1>";
        html += "<p>Send message: <a href='/send?msg=hello'>/send?msg=hello</a></p>";
        html += "<p>Quick test: <a href='/test'>/test</a></p>";
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
    // This will be forwarded to EmergencyWiFiBridge
    // For now, just echo back for testing
    Serial.printf("Handling message from client %u: %s\n", clientId, json);

    // TODO: Forward to bridge module
    // emergencyBridge.handleWiFiMessage(json);
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
