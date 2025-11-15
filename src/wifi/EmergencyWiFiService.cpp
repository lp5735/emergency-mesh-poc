#include "EmergencyWiFiService.h"

#ifdef ENABLE_WIFI_AP

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "modules/EmergencyWiFiBridge.h"
#include "mesh/NodeDB.h"

// Service Worker for PWA offline capability (~400 bytes)
static const char SERVICE_WORKER_JS[] PROGMEM = R"JS(const CACHE='v1';
self.addEventListener('install',e=>{
e.waitUntil(caches.open(CACHE).then(c=>c.addAll(['/'])));
});
self.addEventListener('fetch',e=>{
e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)));
});)JS";

// PWA Manifest for app installation (~250 bytes)
static const char MANIFEST_JSON[] PROGMEM = R"JSON({
"name":"Emergency Mesh Network",
"short_name":"EmrgMesh",
"start_url":"/",
"display":"standalone",
"background_color":"#111",
"theme_color":"#f00",
"icons":[{"src":"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Ccircle cx='50' cy='50' r='45' fill='%23f00'/%3E%3Ctext x='50' y='65' font-size='50' text-anchor='middle' fill='%23fff'%3E🚨%3C/text%3E%3C/svg%3E","sizes":"512x512","type":"image/svg+xml"}]
})JSON";

// PWA-enabled HTML with message persistence - MUST be file-level const for AsyncWebServer multi-client reliability
static const char MINIMAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Emergency Mesh</title>
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#f00">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:#111;color:#0f0;padding:10px}
h1{font-size:18px;margin-bottom:10px;color:#f00}
a{color:#0ff;font-size:12px}
#status{padding:8px;background:#222;border:1px solid #0f0;margin-bottom:10px;font-size:12px}
#messages{height:250px;overflow-y:auto;background:#000;border:1px solid #0f0;padding:10px;margin-bottom:10px}
.msg{margin:5px 0;padding:5px;border-left:3px solid #0f0}
.msg.sent{border-color:#ff0}
.msg.lora{border-color:#0ff}
.msg.traceroute{border-color:#f80;color:#f80}
input{width:calc(100%% - 70px);padding:8px;background:#222;color:#0f0;border:1px solid #0f0}
button{padding:8px 15px;background:#0a0;color:#fff;border:none;cursor:pointer}
button:hover{background:#0c0}
.sos{background:#f00;color:#fff;width:48%%;padding:12px;margin-bottom:10px;font-weight:bold;margin-right:2%%}
.trace{background:#f80;color:#fff;width:48%%;padding:12px;margin-bottom:10px;font-weight:bold}
.trace-input{width:60px;padding:8px;background:#222;color:#f80;border:1px solid #f80;text-transform:uppercase;margin-left:5px}
.inline{display:inline-block}
</style>
</head>
<body>
<h1>🚨 Emergency Mesh <a href="/debug">Debug</a></h1>
<button class="sos inline" onclick="sendSOS()">🆘 EMERGENCY SOS</button>
<button class="trace inline" onclick="traceRoute()">🔍 Trace Route</button>
<div style="margin-bottom:10px">
<span style="color:#f80">Node ID:</span>
<input id="nodeId" class="trace-input" placeholder="1234" maxlength="4" value="">
<small style="color:#888;margin-left:10px">(4-char hex, e.g., "a1b2")</small>
</div>
<div id="status">Connecting...</div>
<div id="messages"></div>
<input id="msg" placeholder="Type message..." onkeypress="if(event.key==='Enter')send()">
<button onclick="send()">Send</button>

<script>
let ws;
function connect(){
ws=new WebSocket('ws://'+location.hostname+':81');
ws.onopen=()=>{log('Connected','info');updateStatus('Connected',true)};
ws.onmessage=e=>{
try{
const d=JSON.parse(e.data);
if(d.type==='message'){
log(d.text,d.source,d.from);
}else if(d.type==='traceroute_started'){
log('Trace route started to node 0x'+d.target,'traceroute');
}else if(d.type==='traceroute_result'){
log('Route: '+d.route,'traceroute');
}else if(d.type==='traceroute_error'){
log('Trace route error: '+d.error,'traceroute');
}
}catch(err){console.error(err)}
};
ws.onerror=()=>log('Error','error');
ws.onclose=()=>{log('Disconnected','error');setTimeout(connect,3000)};
}

function send(){
const m=document.getElementById('msg');
if(!m.value||!ws||ws.readyState!==1)return;
ws.send(JSON.stringify({text:m.value,timestamp:Date.now()}));
log(m.value,'sent');
m.value='';
}

function sendSOS(){
if(!confirm('Send EMERGENCY SOS?'))return;
if(ws&&ws.readyState===1){
ws.send(JSON.stringify({text:'🆘 EMERGENCY SOS',timestamp:Date.now()}));
log('🆘 EMERGENCY SOS SENT','sent');
}
}

function traceRoute(){
const n=document.getElementById('nodeId');
if(!n.value||n.value.length!==4){
alert('Please enter a 4-character hex node ID (e.g., "a1b2")');
return;
}
if(!ws||ws.readyState!==1){
alert('Not connected to mesh!');
return;
}
const nodeId=n.value.toLowerCase();
if(!/^[0-9a-f]{4}$/.test(nodeId)){
alert('Invalid hex characters. Use 0-9 and a-f only.');
return;
}
ws.send(JSON.stringify({type:'traceroute',target:nodeId,timestamp:Date.now()}));
log('Requesting trace route to 0x'+nodeId+'...','traceroute');
}

function updateStatus(text,connected){
const s=document.getElementById('status');
s.textContent=text;
s.style.background=connected?'#040':'#400';
s.style.borderColor=connected?'#0f0':'#f00';
}

function log(text,type='info',from=''){
const d=document.getElementById('messages');
const m=document.createElement('div');
m.className='msg '+(type==='sent'?'sent':type==='lora'?'lora':type==='traceroute'?'traceroute':'');
m.innerHTML='<b>'+new Date().toLocaleTimeString()+'</b> '+(from?from+': ':'')+text;
d.appendChild(m);
d.scrollTop=d.scrollHeight;
let h=JSON.parse(localStorage.getItem('msgHistory')||'[]');
h.push({text,type,from,time:Date.now()});
if(h.length>100)h.shift();
localStorage.setItem('msgHistory',JSON.stringify(h));
}

window.onload=()=>{
let h=JSON.parse(localStorage.getItem('msgHistory')||'[]');
h.forEach(m=>log(m.text,m.type,m.from));
connect();
if('serviceWorker'in navigator){
navigator.serviceWorker.register('/sw.js').then(()=>console.log('SW registered')).catch(e=>console.log('SW failed',e));
}
};
</script>
</body>
</html>)HTML";

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

    // Root path - serve PWA-enabled HTML
    httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.printf("Root path requested from %s\n", request->client()->remoteIP().toString().c_str());
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", MINIMAL_HTML);
        response->addHeader("Connection", "close");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
        Serial.printf("Sent PWA HTML to %s\n", request->client()->remoteIP().toString().c_str());
    });

    // Service Worker for PWA offline capability
    httpServer.on("/sw.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Service worker requested");
        AsyncWebServerResponse *response = request->beginResponse_P(200, "application/javascript", SERVICE_WORKER_JS);
        response->addHeader("Connection", "close");
        response->addHeader("Cache-Control", "no-cache"); // Don't cache SW, allow updates
        request->send(response);
    });

    // PWA Manifest for app installation
    httpServer.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Manifest requested");
        AsyncWebServerResponse *response = request->beginResponse_P(200, "application/json", MANIFEST_JSON);
        response->addHeader("Connection", "close");
        response->addHeader("Cache-Control", "public, max-age=86400"); // Cache for 24h
        request->send(response);
    });

    // Also serve index.html if anyone requests it explicitly
    httpServer.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/index.html requested - redirecting to /");
        request->redirect("/");
    });

    // Serve emergency.html directly
    httpServer.on("/emergency.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/emergency.html requested - redirecting to /");
        request->redirect("/");
    });

    // OLD EMBEDDED HTML APPROACH - DELETED (was lines 166-411)
    // Was causing stack overflow and empty responses
    // Replaced with LittleFS file serving like Meshtastic does

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

    // Check if this is a traceroute command
    const char* msgType = inDoc["type"];
    if (msgType && strcmp(msgType, "traceroute") == 0) {
        handleTracerouteRequest(clientId, inDoc);
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

void EmergencyWiFiService::handleTracerouteRequest(uint8_t clientId, JsonDocument& doc) {
    const char* targetStr = doc["target"];
    size_t targetLen = targetStr ? strlen(targetStr) : 0;

    // Only accept 4-character hex (last 4 hex digits)
    if (!targetStr || targetLen != 4) {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "traceroute_error";
        errorDoc["error"] = "Invalid target: must be 4-character hex";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        wsServer.sendTXT(clientId, errorJson);
        Serial.printf("Traceroute error: invalid target format (length=%d)\n", targetLen);
        return;
    }

    // Always look up in database
    uint16_t lastBytes = (uint16_t)strtoul(targetStr, NULL, 16);
    Serial.printf("Traceroute request: target=%s, searching for node with last bytes 0x%04x\n", targetStr, lastBytes);

    // Search node database for matching node
    uint32_t nodeNum = 0;
    bool found = false;

    for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (node && (node->num & 0xFFFF) == lastBytes) {
            nodeNum = node->num;
            found = true;
            Serial.printf("Found matching node in database: 0x%08x\n", nodeNum);
            break;
        }
    }

    if (!found) {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "traceroute_error";
        errorDoc["error"] = "Node not found in database";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        wsServer.sendTXT(clientId, errorJson);
        Serial.printf("Node 0x%04x not found in database\n", lastBytes);
        return;
    }

    Serial.printf("Traceroute target resolved to: 0x%08x\n", nodeNum);

    // Send acknowledgment to client
    DynamicJsonDocument ackDoc(256);
    ackDoc["type"] = "traceroute_started";
    ackDoc["target"] = targetStr;
    String ackJson;
    serializeJson(ackDoc, ackJson);
    wsServer.sendTXT(clientId, ackJson);

    // Trigger traceroute via EmergencyWiFiBridge
    if (emergencyWiFiBridge) {
        if (!emergencyWiFiBridge->startTraceroute(nodeNum, targetStr)) {
            DynamicJsonDocument errorDoc(256);
            errorDoc["type"] = "traceroute_error";
            errorDoc["error"] = "Failed to start traceroute";
            String errorJson;
            serializeJson(errorDoc, errorJson);
            wsServer.sendTXT(clientId, errorJson);
        }
    } else {
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "traceroute_error";
        errorDoc["error"] = "Mesh bridge not available";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        wsServer.sendTXT(clientId, errorJson);
    }
}

void EmergencyWiFiService::broadcastTracerouteResult(const char* target, const char* route) {
    DynamicJsonDocument doc(1024);
    doc["type"] = "traceroute_result";
    doc["target"] = target;
    doc["route"] = route;
    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);

    broadcastToClients(json.c_str());
    Serial.printf("Broadcast traceroute result: %s\n", json.c_str());
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
