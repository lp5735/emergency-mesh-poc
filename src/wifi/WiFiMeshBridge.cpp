#include "WiFiMeshBridge.h"

#ifdef ENABLE_WIFI_AP

#include <lwip/netif.h>
#include <lwip/pbuf.h>

WiFiMeshBridge wifiMeshBridge;

WiFiMeshBridge::WiFiMeshBridge() : apActive(false) {
}

void WiFiMeshBridge::init() {
    Serial.println("Initializing WiFi Mesh Bridge...");

    // Setup WiFi AP
    setupWiFiAP();

    apActive = true;
    Serial.println("WiFi Mesh Bridge initialized");
    Serial.println("Phones can now connect and use Jami!");
}

void WiFiMeshBridge::setupWiFiAP() {
    // Create AP with MAC-based SSID (unique per device)
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "EMRG-NODE-%02X%02X", mac[4], mac[5]);

    const char* password = "emergency123";

    Serial.printf("Starting WiFi AP: %s\n", ssid);
    Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password, 6, 0, 8); // Channel 6, not hidden, max 8 clients

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
    Serial.printf("Connect to: %s\n", ssid);
    Serial.printf("Password: %s\n", password);
    Serial.printf("\n*** Ready for Jami connections! ***\n");
    Serial.printf("Configure Jami to use this network for local P2P\n");
}

void WiFiMeshBridge::loop() {
    // Nothing to do in loop for now
    // Network bridging will be done via packet callbacks
}

void WiFiMeshBridge::stop() {
    WiFi.softAPdisconnect(true);
    apActive = false;
    Serial.println("WiFi Mesh Bridge stopped");
}

void WiFiMeshBridge::bridgePacketToMesh(const uint8_t* data, size_t len, uint32_t destIP) {
    // This will be called when we intercept WiFi packets that need to go to the mesh
    // For now, just log
    Serial.printf("Bridge: WiFi packet (%d bytes) → LoRa mesh (dest IP: %s)\n",
                  len, IPAddress(destIP).toString().c_str());

    // TODO: Compress packet and send via EmergencyWiFiBridge to LoRa mesh
}

void WiFiMeshBridge::injectPacketFromMesh(const uint8_t* data, size_t len, uint32_t sourceIP) {
    // This will be called when we receive a packet from LoRa mesh
    // Inject it back into the WiFi network
    Serial.printf("Bridge: LoRa mesh packet (%d bytes) → WiFi (source IP: %s)\n",
                  len, IPAddress(sourceIP).toString().c_str());

    // TODO: Decompress and inject into WiFi network stack
}

uint32_t WiFiMeshBridge::getNodeId() {
    // Use last 2 bytes of MAC address as node ID
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return (mac[4] << 8) | mac[5];
}

#endif // ENABLE_WIFI_AP
