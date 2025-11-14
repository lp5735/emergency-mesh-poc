#pragma once

#ifdef ENABLE_WIFI_AP

#include <WiFi.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <map>

// Maximum clients supported
#define MAX_WIFI_CLIENTS 8

// IP address pool for clients (192.168.4.100 - 192.168.4.107)
#define DHCP_POOL_START 100

// Routing table entry - maps WiFi client IP to remote LoRa node
struct RouteEntry {
    uint32_t nodeId;        // Remote Meshtastic node ID
    uint32_t lastSeen;      // Last packet timestamp
    bool isLocal;           // True if client is local (same WiFi)
};

/**
 * WiFi Mesh Bridge - Transparent IP packet routing over LoRa mesh
 *
 * Features:
 * - WiFi AP with DHCP server for auto IP assignment
 * - Event-driven connection management
 * - Transparent IP packet interception and routing
 * - IP <-> LoRa packet compression/decompression
 * - Automatic routing table based on packet source
 *
 * Architecture:
 * [Phone A] → WiFi → [Node 1] → LoRa → [Node 2] → WiFi → [Phone B]
 *    P2P App                                                  P2P App
 */
class WiFiMeshBridge {
public:
    WiFiMeshBridge();

    void init();
    void loop();
    void stop();

    bool isActive() const { return apActive; }
    uint8_t getClientCount() const { return clientCount; }

    // Bridge network packet to LoRa mesh
    void bridgePacketToMesh(const uint8_t* data, size_t len, uint32_t destIP);

    // Receive packet from mesh and inject into WiFi network
    void injectPacketFromMesh(const uint8_t* data, size_t len, uint32_t sourceIP);

    // Routing table management
    void addRoute(uint32_t ip, uint32_t nodeId, bool isLocal);
    bool getRoute(uint32_t ip, uint32_t &nodeId);
    void removeStaleRoutes();

private:
    bool apActive;
    uint8_t clientCount;
    uint32_t lastClientCheck;

    // Routing table: IP address -> RouteEntry
    std::map<uint32_t, RouteEntry> routingTable;

    // WiFi setup
    void setupWiFiAP();
    void setupDHCP();
    void setupNetworkBridge();

    // Event handlers
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    void handleClientConnected(const uint8_t* mac);
    void handleClientDisconnected(const uint8_t* mac);

    // Connection management
    void checkConnections();
    void cleanupStaleConnections();

    // Packet processing
    static err_t netifInput(struct pbuf *p, struct netif *inp);
    static err_t netifOutput(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr);

    // Helpers
    uint32_t getNodeId();
    uint32_t assignClientIP(const uint8_t* mac);
    void printRoutingTable();
};

extern WiFiMeshBridge wifiMeshBridge;

#endif // ENABLE_WIFI_AP
