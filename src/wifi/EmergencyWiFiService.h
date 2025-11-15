#pragma once

#ifdef ENABLE_WIFI_AP

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

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

    // Mesh status updates
    void broadcastMeshStatus();

private:
    AsyncWebServer httpServer;
    WebSocketsServer wsServer;

    bool apActive;
    uint8_t clientCount;
    uint32_t lastClientActivity;
    uint32_t lastMeshStatusBroadcast;

    void setupWiFiAP();
    void setupWebServer();
    void setupWebSocket();
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void handleClientMessage(uint8_t clientId, const char* json);
    void sendNodeInfo(uint8_t clientId);
};

extern EmergencyWiFiService wifiService;

#endif // ENABLE_WIFI_AP
