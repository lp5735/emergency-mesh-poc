#pragma once

#include "configuration.h"

#ifdef ENABLE_WIFI_AP

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * Emergency WiFi Bridge Module
 *
 * Ultra-simple bridge between WiFi clients and LoRa mesh
 * - Receives text messages from WiFi clients
 * - Broadcasts them to LoRa mesh
 * - Receives LoRa messages and broadcasts to WiFi clients
 */
class EmergencyWiFiBridge : public SinglePortModule, private concurrency::OSThread
{
  public:
    EmergencyWiFiBridge();

    /**
     * Send a simple text message to the mesh from WiFi client
     * @param message Text to send (max 200 bytes)
     * @return true if sent successfully
     */
    bool sendTextToMesh(const char *message);

  protected:
    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if handled, ProcessMessage::CONTINUE if someone else should handle it
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual int32_t runOnce() override;
};

extern EmergencyWiFiBridge *emergencyWiFiBridge;

#endif // ENABLE_WIFI_AP
