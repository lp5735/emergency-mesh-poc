#pragma once

#ifdef ENABLE_WIFI_AP

#include <WiFi.h>
#include <lwip/ip_addr.h>

/**
 * WiFi Mesh Bridge - Ultra-simple WiFi AP for Jami
 *
 * Creates a WiFi Access Point and bridges network traffic to LoRa mesh.
 * No HTTP server, no WebSocket - just a transparent network bridge.
 *
 * Designed for Jami P2P app to communicate over LoRa mesh.
 */
class WiFiMeshBridge {
public:
    WiFiMeshBridge();

    void init();
    void loop();
    void stop();

    bool isActive() const { return apActive; }
    uint8_t getClientCount() const { return WiFi.softAPgetStationNum(); }

    // Bridge network packet to LoRa mesh
    void bridgePacketToMesh(const uint8_t* data, size_t len, uint32_t destIP);

    // Receive packet from mesh and inject into WiFi network
    void injectPacketFromMesh(const uint8_t* data, size_t len, uint32_t sourceIP);

private:
    bool apActive;

    void setupWiFiAP();
    void setupNetworkBridge();

    uint32_t getNodeId();
};

extern WiFiMeshBridge wifiMeshBridge;

#endif // ENABLE_WIFI_AP
