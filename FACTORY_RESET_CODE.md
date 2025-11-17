# Factory Reset Code for ESP32

## Option 1: Add HTTP Endpoint for Remote Reset

Add to `EmergencyWiFiService.cpp` in `setupWebServer()`:

```cpp
// Factory reset endpoint - clears NVS and reboots
httpServer.on("/factory_reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Factory reset requested via HTTP");

    request->send(200, "text/plain", "Factory reset initiated. Device will reboot in 2 seconds.");

    delay(1000);

    // Erase NVS
    nvs_flash_erase();
    nvs_flash_init();

    delay(1000);
    ESP.restart();
});
```

Then visit: `http://192.168.4.1/factory_reset`

## Option 2: Button Hold (Physical Button)

If your Heltec V2 has a user button (usually GPIO 0 or the PRG button):

```cpp
// In main.cpp or EmergencyWiFiService.cpp setup()

#define FACTORY_RESET_BUTTON 0  // GPIO 0 (PRG button on Heltec)
#define FACTORY_RESET_HOLD_TIME 10000  // 10 seconds

void checkFactoryReset() {
    static uint32_t buttonPressStart = 0;

    if (digitalRead(FACTORY_RESET_BUTTON) == LOW) {  // Button pressed
        if (buttonPressStart == 0) {
            buttonPressStart = millis();
            Serial.println("Factory reset button pressed, hold for 10 seconds...");
        } else if (millis() - buttonPressStart > FACTORY_RESET_HOLD_TIME) {
            Serial.println("FACTORY RESET!");

            // Visual feedback - flash LED
            for (int i = 0; i < 10; i++) {
                digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
                delay(100);
            }

            // Erase NVS
            nvs_flash_erase();
            nvs_flash_init();

            ESP.restart();
        }
    } else {
        buttonPressStart = 0;  // Button released, reset timer
    }
}

// Call in loop()
void loop() {
    checkFactoryReset();
    // ... rest of loop
}
```

## Option 3: Serial Command (Add to firmware)

Add serial command handler:

```cpp
void handleSerialCommands() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "FACTORY_RESET") {
            Serial.println("Erasing NVS and rebooting...");
            nvs_flash_erase();
            nvs_flash_init();
            delay(1000);
            ESP.restart();
        } else if (cmd == "HEAP") {
            Serial.printf("Free heap: %u bytes (%.1f%%)\n",
                ESP.getFreeHeap(),
                (ESP.getFreeHeap() * 100.0) / 327680.0);
        } else if (cmd == "REBOOT") {
            Serial.println("Rebooting...");
            ESP.restart();
        }
    }
}

// Call in loop()
void loop() {
    handleSerialCommands();
    // ... rest of loop
}
```

Then via serial monitor type: `FACTORY_RESET`

## What Gets Cleared

NVS (Non-Volatile Storage) contains:
- WiFi credentials (if stored)
- Meshtastic preferences
- Channel keys
- Node database
- Any Preferences API data

**Important**: This does NOT clear:
- Firmware code (stays in flash)
- SPIFFS/LittleFS files (if any)
- Factory partition data

## Quick Method for Current Firmware

Since you can't rebuild yet, use `esptool.py` directly:

```bash
# Erase NVS partition (offset 0x9000, size 0x5000 = 20KB)
~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbserial-0001 erase_region 0x9000 0x5000

# Then reboot device
~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbserial-0001 --after hard_reset read_mac
```

This clears all stored data without reflashing firmware!
