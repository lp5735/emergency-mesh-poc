#pragma once

#ifdef ENABLE_WIFI_AP

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

class EmergencyWiFiService {
public:
    EmergencyWiFiService();

    void init();
    void loop();
    void stop();

    bool isActive() const { return apActive; }
    uint8_t getClientCount() const { return clientCount; }

    // Messaging
    void broadcastToClients(const char* json);
    void sendToClient(uint8_t clientId, const char* json);

    // Node info
    uint32_t getNodeId();

private:
    AsyncWebServer httpServer;
    WebSocketsServer wsServer;

    bool apActive;
    uint8_t clientCount;
    uint32_t lastClientActivity;

    void setupWiFiAP();
    void setupWebServer();
    void setupWebSocket();
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void handleClientMessage(uint8_t clientId, const char* json);
    void handleTracerouteRequest(uint8_t clientId, JsonDocument& doc);
    void sendNodeInfo(uint8_t clientId);

public:
    // Traceroute result callback (called by EmergencyWiFiBridge)
    void broadcastTracerouteResult(const char* target, const char* route);
};

extern EmergencyWiFiService wifiService;

#endif // ENABLE_WIFI_AP
