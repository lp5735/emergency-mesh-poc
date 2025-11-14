#include "EmergencyWiFiBridge.h"

#ifdef ENABLE_WIFI_AP

#include "MeshService.h"
#include "Router.h"
#include "wifi/EmergencyWiFiService.h"
#include <ArduinoJson.h>

EmergencyWiFiBridge *emergencyWiFiBridge;

EmergencyWiFiBridge::EmergencyWiFiBridge()
    : SinglePortModule("EmergencyWiFiBridge", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("WiFiBridge")
{
    Serial.println("EmergencyWiFiBridge: Initializing...");
}

bool EmergencyWiFiBridge::sendTextToMesh(const char *message)
{
    if (!message || strlen(message) == 0) {
        Serial.println("EmergencyWiFiBridge: Empty message, ignoring");
        return false;
    }

    Serial.printf("EmergencyWiFiBridge: Sending to mesh: %s\n", message);

    // Allocate a new mesh packet
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        Serial.println("EmergencyWiFiBridge: ERROR - Failed to allocate packet");
        return false;
    }

    // Set packet parameters
    p->to = NODENUM_BROADCAST; // Broadcast to all nodes
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false; // No ACK needed for broadcast
    p->priority = meshtastic_MeshPacket_Priority_DEFAULT;

    // Copy message into payload
    size_t msgLen = strlen(message);
    if (msgLen > sizeof(p->decoded.payload.bytes) - 1) {
        msgLen = sizeof(p->decoded.payload.bytes) - 1; // Truncate if too long
    }

    memcpy(p->decoded.payload.bytes, message, msgLen);
    p->decoded.payload.size = msgLen;

    Serial.printf("EmergencyWiFiBridge: Packet allocated, sending %d bytes\n", msgLen);

    // Send to mesh via router
    ErrorCode result = router->send(p);

    if (result == ERRNO_OK) {
        Serial.println("EmergencyWiFiBridge: ✓ Message sent to mesh successfully");
        return true;
    } else {
        Serial.printf("EmergencyWiFiBridge: ✗ Failed to send, error code: %d\n", result);
        return false;
    }
}

ProcessMessage EmergencyWiFiBridge::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Only handle text messages
    if (mp.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return ProcessMessage::CONTINUE;
    }

    Serial.printf("EmergencyWiFiBridge: Received message from mesh, from=0x%x, size=%d\n", mp.from, mp.decoded.payload.size);

    // Extract text from payload
    char message[256];
    size_t msgLen = mp.decoded.payload.size;
    if (msgLen > sizeof(message) - 1) {
        msgLen = sizeof(message) - 1;
    }

    memcpy(message, mp.decoded.payload.bytes, msgLen);
    message[msgLen] = '\0'; // Null terminate

    Serial.printf("EmergencyWiFiBridge: Message text: %s\n", message);

    // Create JSON for WiFi clients
    DynamicJsonDocument doc(512);
    doc["type"] = "message";
    doc["from"] = String(mp.from, HEX);
    doc["text"] = message;
    doc["timestamp"] = millis();
    doc["rssi"] = mp.rx_rssi;
    doc["snr"] = mp.rx_snr;

    String json;
    serializeJson(doc, json);

    // Broadcast to all WiFi clients via WebSocket
    wifiService.broadcastToClients(json.c_str());

    Serial.printf("EmergencyWiFiBridge: Broadcasted to WiFi clients: %s\n", json.c_str());

    return ProcessMessage::CONTINUE; // Let other modules see it too
}

int32_t EmergencyWiFiBridge::runOnce()
{
    // Nothing to do in background loop for now
    return INT32_MAX; // Sleep indefinitely, we're event-driven
}

#endif // ENABLE_WIFI_AP
