#include "EmergencyWiFiService.h"

#ifdef ENABLE_WIFI_AP

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "modules/EmergencyWiFiBridge.h"
#include "mesh/NodeDB.h"
#include "gps/RTC.h"

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

// Ultra-minimal main page (~1.5KB) - messaging only
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
#s{padding:8px;background:#222;border:1px solid #0f0;margin-bottom:10px;font-size:12px}
#m{height:350px;overflow-y:auto;background:#000;border:1px solid #0f0;padding:10px;margin-bottom:10px}
.msg{margin:5px 0;padding:5px;border-left:3px solid #0f0}
.sent{border-color:#ff0}
.lora{border-color:#0ff}
input{width:calc(100% - 70px);padding:8px;background:#222;color:#0f0;border:1px solid #0f0}
button{padding:8px 15px;background:#0a0;color:#fff;border:none;cursor:pointer}
button:hover{background:#0c0}
.sos{background:#f00;color:#fff;width:100%;padding:12px;margin-bottom:10px;font-weight:bold}
</style>
</head>
<body>
<h1>🚨 Emergency Mesh <a href="/debug">Debug</a></h1>
<input id="u" placeholder="Your name..." style="width:100%;margin-bottom:10px;padding:8px;background:#222;color:#0f0;border:1px solid #0f0">
<button class="sos" onclick="sos()">🆘 EMERGENCY SOS</button>
<div id="s">Connecting...</div>
<div id="m"></div>
<input id="i" placeholder="Type message..." onkeypress="if(event.key==='Enter')send()">
<button onclick="send()">Send</button>

<script>
let ws,r=0;
function upd(t,c){
const s=document.getElementById('s');
s.textContent=t;
s.style.background=c?'#040':'#400';
s.style.borderColor=c?'#0f0':'#f00';
}
function con(){
if(ws&&ws.readyState===WebSocket.CONNECTING)return;
ws=new WebSocket('ws://'+location.hostname+':81');
ws.onopen=()=>{r=0;upd('Connected',1)};
ws.onmessage=e=>{
try{
const d=JSON.parse(e.data);
if(d.type==='message')log(d.text,d.source||'lora',d.username||d.from||'Unknown');
}catch(err){}
};
ws.onerror=()=>upd('Error',0);
ws.onclose=()=>{r++;upd('Reconnecting('+r+')...',0);setTimeout(con,Math.min(3000,1000*r))};
}
function send(){
const m=document.getElementById('i');
const u=document.getElementById('u');
if(!m.value||!ws||ws.readyState!==1)return;
const usr=u.value.trim()||'User';
localStorage.setItem('usr',usr);
ws.send(JSON.stringify({text:m.value,username:usr,timestamp:Date.now()}));
log(m.value,'sent',usr);
m.value='';
}
function sos(){
if(!confirm('Send SOS?'))return;
if(ws&&ws.readyState===1){
const u=document.getElementById('u');
const usr=u.value.trim()||'User';
localStorage.setItem('usr',usr);
ws.send(JSON.stringify({text:'🆘 SOS',username:usr,timestamp:Date.now()}));
log('🆘 SOS SENT','sent',usr);
}
}
function log(t,ty='info',usr=''){
const d=document.getElementById('m');
const m=document.createElement('div');
m.className='msg '+(ty==='sent'?'sent':'lora');
const prefix=usr?'<b style="color:#0ff">'+usr+'</b>: ':'';
m.innerHTML='<small>'+new Date().toLocaleTimeString()+'</small> '+prefix+t;
d.appendChild(m);
d.scrollTop=d.scrollHeight;
let h=JSON.parse(localStorage.getItem('h')||'[]');
h.push({t,ty,usr,tm:Date.now()});
if(h.length>50)h.shift();
localStorage.setItem('h',JSON.stringify(h));
}
window.onload=()=>{
const u=document.getElementById('u');
u.value=localStorage.getItem('usr')||'';
u.addEventListener('blur',()=>localStorage.setItem('usr',u.value.trim()));
let h=JSON.parse(localStorage.getItem('h')||'[]');
h.forEach(m=>log(m.t,m.ty,m.usr));
con();
if('serviceWorker'in navigator)navigator.serviceWorker.register('/sw.js');
};
window.addEventListener('online',()=>{if(!ws||ws.readyState===WebSocket.CLOSED)con()});
window.addEventListener('offline',()=>upd('Offline',0));
</script>
</body>
</html>)HTML";

// Debug page for mesh status (~2KB)
static const char DEBUG_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mesh Debug</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:#111;color:#0f0;padding:10px}
h1{font-size:18px;margin-bottom:10px;color:#f00}
a{color:#0ff;font-size:12px}
#s{padding:8px;background:#222;border:1px solid #0f0;margin-bottom:10px;font-size:12px}
#d{padding:10px;background:#000;border:1px solid #0f0}
#d b{color:#ff0}
#st{color:#0ff;font-weight:bold}
#n{margin-top:10px}
</style>
</head>
<body>
<h1>📡 Mesh Debug <a href="/">Back</a></h1>
<div id="s">Connecting...</div>
<div id="d"><b>Mesh Status:</b> <span id="st">...</span><div id="n"></div></div>
<script>
let ws,r=0;
function upd(t,c){
const s=document.getElementById('s');
s.textContent=t;
s.style.background=c?'#040':'#400';
s.style.borderColor=c?'#0f0':'#f00';
}
function con(){
if(ws&&ws.readyState===WebSocket.CONNECTING)return;
ws=new WebSocket('ws://'+location.hostname+':81');
ws.onopen=()=>{r=0;upd('Connected',1)};
ws.onmessage=e=>{
try{
const d=JSON.parse(e.data);
if(d.type==='mesh_status')mesh(d);
}catch(err){}
};
ws.onerror=()=>upd('Error',0);
ws.onclose=()=>{r++;upd('Reconnecting('+r+')...',0);setTimeout(con,Math.min(3000,1000*r))};
}
function mesh(d){
const st=document.getElementById('st');
const n=document.getElementById('n');
st.textContent=(d.online_nodes||0)+'/'+(d.total_nodes||0);
if(!d.nodes||!d.nodes.length){n.innerHTML='<small>No nodes</small>';return}
n.innerHTML=d.nodes.map(x=>{
const c=(x.snr+20)/30*100>60?'#0f0':(x.snr+20)/30*100>30?'#ff0':'#f00';
const ago=x.seconds_ago<60?x.seconds_ago+'s':Math.floor(x.seconds_ago/60)+'m';
return`<div style="margin:5px 0;padding:5px;background:#000;border-left:3px solid ${c}"><b style="color:#0ff">${x.id}</b> ${x.long_name||x.short_name||'?'}<br><small>SNR:${x.snr.toFixed(1)} | ${ago} ago ${x.is_online?'🟢':'🔴'}</small></div>`;
}).join('');
}
window.onload=con;
</script>
</body>
</html>)HTML";

EmergencyWiFiService wifiService;

EmergencyWiFiService::EmergencyWiFiService()
    : httpServer(80), wsServer(81), apActive(false), clientCount(0), lastClientActivity(0), lastMeshStatusBroadcast(0) {
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

    // Debug page - mesh status display
    httpServer.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.printf("Debug page requested from %s\n", request->client()->remoteIP().toString().c_str());
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", DEBUG_HTML);
        response->addHeader("Connection", "close");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
        Serial.printf("Sent debug HTML to %s\n", request->client()->remoteIP().toString().c_str());
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

void EmergencyWiFiService::loop() {
    wsServer.loop();

    // Broadcast mesh status every 5 seconds if we have connected clients
    if (clientCount > 0) {
        uint32_t now = millis();
        if (now - lastMeshStatusBroadcast > 5000) {  // 5 seconds
            broadcastMeshStatus();
            lastMeshStatusBroadcast = now;
        }
    }
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

void EmergencyWiFiService::broadcastMeshStatus() {
    // Create JSON document for mesh status
    DynamicJsonDocument doc(2048);
    doc["type"] = "mesh_status";
    doc["timestamp"] = millis();

    // Get mesh node count
    size_t numNodes = nodeDB->getNumMeshNodes();
    size_t numOnline = nodeDB->getNumOnlineMeshNodes();

    doc["total_nodes"] = numNodes;
    doc["online_nodes"] = numOnline;

    // Add node details
    JsonArray nodesArray = doc.createNestedArray("nodes");

    uint32_t readIndex = 0;
    const meshtastic_NodeInfoLite *nodeInfo = nodeDB->readNextMeshNode(readIndex);

    while (nodeInfo != NULL) {
        JsonObject node = nodesArray.createNestedObject();

        // Node ID
        char id[16];
        snprintf(id, sizeof(id), "!%08x", nodeInfo->num);
        node["id"] = id;

        // User info
        if (nodeInfo->has_user) {
            node["long_name"] = nodeInfo->user.long_name;
            node["short_name"] = nodeInfo->user.short_name;
        }

        // Signal quality
        node["snr"] = nodeInfo->snr;

        // Calculate RSSI from SNR (approximation)
        // Typical LoRa: RSSI = SNR - noise_floor
        // Assuming noise floor around -120dBm
        int rssi = (int)nodeInfo->snr - 120;
        node["rssi"] = rssi;

        // Last heard timestamp
        node["last_heard"] = nodeInfo->last_heard;

        // Calculate if online (heard in last 5 minutes = 300 seconds)
        // Use sinceLastSeen from NodeDB which handles time properly
        uint32_t secondsAgo = sinceLastSeen(nodeInfo);
        bool isOnline = secondsAgo < 300; // 5 minutes in seconds
        node["is_online"] = isOnline;
        node["seconds_ago"] = secondsAgo;

        // Hop limit (if available)
        if (nodeInfo->has_device_metrics) {
            node["battery_level"] = nodeInfo->device_metrics.battery_level;
        }

        // Get next node
        nodeInfo = nodeDB->readNextMeshNode(readIndex);
    }

    // Serialize and broadcast
    String json;
    serializeJson(doc, json);
    broadcastToClients(json.c_str());

    Serial.printf("Broadcasted mesh status: %d online / %d total nodes\n", numOnline, numNodes);
}

#endif // ENABLE_WIFI_AP
