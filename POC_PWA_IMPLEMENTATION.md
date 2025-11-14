# POC Implementation Plan - Progressive Web App

**Architecture**: Client-side storage (IndexedDB) + Stateless ESP32 bridge

---

## Implementation Phases

### Phase 1: Basic Infrastructure (Day 1-2)
**Goal**: PWA loads from ESP32, connects via WebSocket, basic UI works

#### Step 1.1: ESP32 WebSocket Server
**File**: `src/wifi/EmergencyWiFiService.cpp`

```cpp
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>

class EmergencyWiFiService {
private:
    AsyncWebServer httpServer;
    WebSocketsServer wsServer;

public:
    EmergencyWiFiService() : httpServer(80), wsServer(81) {}

    void init() {
        // Initialize SPIFFS for web app files
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS mount failed");
            return;
        }

        // Setup WiFi AP
        WiFi.softAP("EMRG-TEST-POC", "emergency123");
        WiFi.softAPConfig(
            IPAddress(192, 168, 4, 1),
            IPAddress(192, 168, 4, 1),
            IPAddress(255, 255, 255, 0)
        );

        Serial.printf("WiFi AP started: %s\n", WiFi.softAPIP().toString().c_str());

        // Serve PWA files from SPIFFS
        httpServer.serveStatic("/", SPIFFS, "/webapp/")
            .setDefaultFile("index.html")
            .setCacheControl("max-age=86400"); // Cache for 1 day

        // WebSocket event handler
        wsServer.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
            this->handleWebSocketEvent(num, type, payload, length);
        });

        httpServer.begin();
        wsServer.begin();

        Serial.println("HTTP server on port 80, WebSocket on port 81");
    }

    void loop() {
        wsServer.loop();
    }

    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        switch(type) {
            case WStype_CONNECTED:
                Serial.printf("Client %d connected\n", num);
                sendNodeInfo(num);
                break;

            case WStype_DISCONNECTED:
                Serial.printf("Client %d disconnected\n", num);
                break;

            case WStype_TEXT:
                Serial.printf("Client %d: %s\n", num, payload);
                handleClientMessage(num, (char*)payload);
                break;
        }
    }

    void handleClientMessage(uint8_t clientId, const char* json) {
        // Parse and forward to bridge module
        // Bridge will handle LoRa transmission
        Serial.printf("Received from client: %s\n", json);

        // For now, echo back
        wsServer.sendTXT(clientId, json);
    }

    void sendNodeInfo(uint8_t clientId) {
        // Send node info on connect
        char json[256];
        snprintf(json, sizeof(json),
            "{\"type\":\"node_info\",\"nodeId\":\"%04x\",\"name\":\"Test Node\"}",
            getNodeId());
        wsServer.sendTXT(clientId, json);
    }

    void broadcastToClients(const char* json) {
        wsServer.broadcastTXT(json);
    }

    uint32_t getNodeId() {
        // Use MAC address as node ID
        uint8_t mac[6];
        WiFi.macAddress(mac);
        return (mac[4] << 8) | mac[5];
    }
};
```

#### Step 1.2: PWA File Structure

**Create**: `data/webapp/` directory (will be uploaded to SPIFFS)

```
data/webapp/
├── index.html          # Main app
├── app.js              # Application logic
├── db.js               # IndexedDB wrapper
├── styles.css          # Styling
├── manifest.json       # PWA manifest
├── sw.js              # Service worker
└── icon-192.png       # App icon
```

#### Step 1.3: Basic HTML Structure

**File**: `data/webapp/index.html`

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <title>Emergency Mesh</title>
    <link rel="stylesheet" href="styles.css">
    <link rel="manifest" href="manifest.json">
    <link rel="icon" type="image/png" href="icon-192.png">
</head>
<body>
    <div id="app">
        <!-- Connection status -->
        <div id="connectionStatus" class="status-bar">
            <span id="statusDot" class="dot offline"></span>
            <span id="statusText">Connecting...</span>
        </div>

        <!-- Main container -->
        <div class="container">
            <!-- Contact list panel -->
            <div id="contactPanel" class="panel">
                <div class="panel-header">
                    <h2>Contacts</h2>
                    <button id="settingsBtn" class="icon-btn">⚙️</button>
                </div>
                <div id="contactList" class="contact-list">
                    <!-- Contacts will be dynamically added -->
                </div>
            </div>

            <!-- Conversation panel -->
            <div id="conversationPanel" class="panel">
                <div id="conversationHeader" class="panel-header">
                    <button id="backBtn" class="icon-btn mobile-only">←</button>
                    <div>
                        <h3 id="contactName">Select a contact</h3>
                        <span id="contactStatus" class="status-text"></span>
                    </div>
                </div>

                <div id="messageList" class="message-list">
                    <!-- Messages will be dynamically added -->
                </div>

                <div class="compose-bar">
                    <input type="text" id="messageInput" placeholder="Type a message..." />
                    <button id="sendBtn" class="send-btn">Send</button>
                </div>
            </div>
        </div>
    </div>

    <!-- Scripts -->
    <script src="https://cdn.jsdelivr.net/npm/idb@7/build/umd.js"></script>
    <script src="db.js"></script>
    <script src="app.js"></script>
</body>
</html>
```

#### Step 1.4: WhatsApp-Style CSS

**File**: `data/webapp/styles.css`

```css
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

:root {
    --bg-primary: #111b21;
    --bg-secondary: #202c33;
    --bg-tertiary: #2a3942;
    --text-primary: #e9edef;
    --text-secondary: #8696a0;
    --accent: #00a884;
    --accent-hover: #06cf9c;
    --sent-bubble: #005c4b;
    --received-bubble: #202c33;
    --border: #2a3942;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
    background: var(--bg-primary);
    color: var(--text-primary);
    height: 100vh;
    overflow: hidden;
}

#app {
    height: 100vh;
    display: flex;
    flex-direction: column;
}

.status-bar {
    background: var(--bg-tertiary);
    padding: 8px 16px;
    display: flex;
    align-items: center;
    gap: 8px;
    border-bottom: 1px solid var(--border);
}

.dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #888;
}

.dot.online {
    background: #25d366;
    box-shadow: 0 0 8px #25d366;
}

.dot.offline {
    background: #f44336;
}

.container {
    flex: 1;
    display: flex;
    overflow: hidden;
}

.panel {
    background: var(--bg-secondary);
    display: flex;
    flex-direction: column;
}

#contactPanel {
    width: 30%;
    min-width: 300px;
    border-right: 1px solid var(--border);
}

#conversationPanel {
    flex: 1;
}

.panel-header {
    background: var(--bg-tertiary);
    padding: 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    border-bottom: 1px solid var(--border);
}

.panel-header h2,
.panel-header h3 {
    font-size: 18px;
    font-weight: 500;
}

.status-text {
    font-size: 13px;
    color: var(--text-secondary);
}

.icon-btn {
    background: transparent;
    border: none;
    color: var(--text-secondary);
    font-size: 20px;
    cursor: pointer;
    padding: 8px;
}

.icon-btn:hover {
    color: var(--text-primary);
}

.contact-list {
    overflow-y: auto;
    flex: 1;
}

.contact-item {
    padding: 16px;
    border-bottom: 1px solid var(--border);
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 12px;
}

.contact-item:hover {
    background: var(--bg-tertiary);
}

.contact-item.active {
    background: var(--bg-tertiary);
}

.contact-avatar {
    width: 48px;
    height: 48px;
    border-radius: 50%;
    background: var(--accent);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 20px;
    font-weight: 500;
    flex-shrink: 0;
}

.contact-info {
    flex: 1;
    min-width: 0;
}

.contact-name {
    font-size: 16px;
    font-weight: 400;
    margin-bottom: 4px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.contact-preview {
    font-size: 14px;
    color: var(--text-secondary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.contact-meta {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 4px;
}

.contact-time {
    font-size: 12px;
    color: var(--text-secondary);
}

.unread-badge {
    background: var(--accent);
    color: var(--bg-primary);
    font-size: 12px;
    font-weight: 600;
    padding: 2px 6px;
    border-radius: 10px;
    min-width: 20px;
    text-align: center;
}

.message-list {
    flex: 1;
    overflow-y: auto;
    padding: 20px;
    background: var(--bg-primary);
    display: flex;
    flex-direction: column;
    gap: 8px;
}

.message {
    display: flex;
    gap: 8px;
    max-width: 70%;
}

.message.sent {
    align-self: flex-end;
    flex-direction: row-reverse;
}

.message.received {
    align-self: flex-start;
}

.message-bubble {
    padding: 8px 12px;
    border-radius: 8px;
    position: relative;
}

.message.sent .message-bubble {
    background: var(--sent-bubble);
    border-top-right-radius: 2px;
}

.message.received .message-bubble {
    background: var(--received-bubble);
    border-top-left-radius: 2px;
}

.message-text {
    font-size: 14px;
    line-height: 1.4;
    margin-bottom: 4px;
    word-wrap: break-word;
}

.message-meta {
    display: flex;
    align-items: center;
    gap: 4px;
    justify-content: flex-end;
    font-size: 11px;
    color: var(--text-secondary);
}

.message-status {
    display: inline-flex;
    align-items: center;
}

.compose-bar {
    background: var(--bg-tertiary);
    padding: 12px;
    display: flex;
    gap: 8px;
    border-top: 1px solid var(--border);
}

#messageInput {
    flex: 1;
    background: var(--bg-secondary);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 12px 16px;
    color: var(--text-primary);
    font-size: 15px;
    outline: none;
}

#messageInput:focus {
    border-color: var(--accent);
}

.send-btn {
    background: var(--accent);
    color: var(--bg-primary);
    border: none;
    border-radius: 8px;
    padding: 12px 24px;
    font-size: 15px;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.2s;
}

.send-btn:hover {
    background: var(--accent-hover);
}

.send-btn:disabled {
    background: var(--bg-tertiary);
    color: var(--text-secondary);
    cursor: not-allowed;
}

/* Mobile responsive */
.mobile-only {
    display: none;
}

@media (max-width: 768px) {
    #contactPanel {
        width: 100%;
        min-width: unset;
    }

    #conversationPanel {
        display: none;
    }

    .container.conversation-open #contactPanel {
        display: none;
    }

    .container.conversation-open #conversationPanel {
        display: flex;
    }

    .mobile-only {
        display: block;
    }
}

/* Scrollbar styling */
::-webkit-scrollbar {
    width: 6px;
}

::-webkit-scrollbar-track {
    background: var(--bg-secondary);
}

::-webkit-scrollbar-thumb {
    background: var(--bg-tertiary);
    border-radius: 3px;
}

::-webkit-scrollbar-thumb:hover {
    background: #3a4a53;
}
```

#### Step 1.5: PWA Manifest

**File**: `data/webapp/manifest.json`

```json
{
    "name": "Emergency Mesh",
    "short_name": "EMRG",
    "description": "Emergency Responder Mesh Network",
    "start_url": "/",
    "display": "standalone",
    "background_color": "#111b21",
    "theme_color": "#00a884",
    "orientation": "portrait",
    "icons": [
        {
            "src": "icon-192.png",
            "sizes": "192x192",
            "type": "image/png",
            "purpose": "any maskable"
        }
    ]
}
```

**Test Step 1**:
- Build firmware, upload to ESP32
- Upload SPIFFS data (`pio run -t uploadfs`)
- Connect phone to WiFi "EMRG-TEST-POC"
- Browse to http://192.168.4.1
- See WhatsApp-like UI load

---

### Phase 2: IndexedDB Storage (Day 2-3)

#### Step 2.1: Database Wrapper

**File**: `data/webapp/db.js`

```javascript
class MeshDB {
    constructor() {
        this.db = null;
        this.ready = false;
    }

    async init() {
        this.db = await idb.openDB('emergency-mesh', 1, {
            upgrade(db) {
                // Messages table
                const messages = db.createObjectStore('messages', {
                    keyPath: 'id',
                    autoIncrement: true
                });
                messages.createIndex('contactId', 'contactId');
                messages.createIndex('timestamp', 'timestamp');
                messages.createIndex('status', 'status');

                // Contacts table
                const contacts = db.createObjectStore('contacts', {
                    keyPath: 'nodeId'
                });
                contacts.createIndex('lastSeen', 'lastSeen');

                // Conversations table (metadata)
                db.createObjectStore('conversations', {
                    keyPath: 'contactId'
                });

                // Settings table
                db.createObjectStore('settings', {
                    keyPath: 'key'
                });
            }
        });

        this.ready = true;
        console.log('Database initialized');
    }

    // Messages
    async saveMessage(msg) {
        const id = await this.db.add('messages', msg);
        await this.updateConversation(msg.contactId);
        return id;
    }

    async updateMessage(id, updates) {
        const tx = this.db.transaction('messages', 'readwrite');
        const msg = await tx.store.get(id);
        Object.assign(msg, updates);
        await tx.store.put(msg);
        await tx.done;
    }

    async getConversation(contactId) {
        const messages = await this.db.getAllFromIndex(
            'messages',
            'contactId',
            IDBKeyRange.only(contactId)
        );
        return messages.sort((a, b) => a.timestamp - b.timestamp);
    }

    async getRecentMessages(limit = 100) {
        const allMessages = await this.db.getAll('messages');
        return allMessages
            .sort((a, b) => b.timestamp - a.timestamp)
            .slice(0, limit);
    }

    // Contacts
    async saveContact(contact) {
        await this.db.put('contacts', contact);
    }

    async getContact(nodeId) {
        return await this.db.get('contacts', nodeId);
    }

    async getAllContacts() {
        const contacts = await this.db.getAll('contacts');
        return contacts.sort((a, b) => b.lastSeen - a.lastSeen);
    }

    async updateContactStatus(nodeId, isOnline) {
        const contact = await this.getContact(nodeId);
        if (contact) {
            contact.online = isOnline;
            contact.lastSeen = Date.now();
            await this.saveContact(contact);
        }
    }

    // Conversations (metadata)
    async updateConversation(contactId) {
        const messages = await this.getConversation(contactId);
        const unread = messages.filter(m => !m.fromMe && !m.read).length;
        const lastMessage = messages[messages.length - 1];

        await this.db.put('conversations', {
            contactId: contactId,
            lastMessageTime: lastMessage?.timestamp || 0,
            lastMessageText: lastMessage?.text || '',
            unreadCount: unread
        });
    }

    async getConversationMetadata(contactId) {
        return await this.db.get('conversations', contactId);
    }

    async getAllConversations() {
        return await this.db.getAll('conversations');
    }

    async markAsRead(contactId) {
        const tx = this.db.transaction('messages', 'readwrite');
        const messages = await tx.store.index('contactId').getAll(contactId);

        for (const msg of messages) {
            if (!msg.fromMe && !msg.read) {
                msg.read = true;
                await tx.store.put(msg);
            }
        }

        await tx.done;
        await this.updateConversation(contactId);
    }

    // Settings
    async saveSetting(key, value) {
        await this.db.put('settings', {key, value});
    }

    async getSetting(key, defaultValue = null) {
        const setting = await this.db.get('settings', key);
        return setting ? setting.value : defaultValue;
    }
}

// Global instance
const meshDB = new MeshDB();
```

**Test Step 2**:
- Open browser console
- Run: `await meshDB.init()`
- Run: `await meshDB.saveMessage({contactId: '1234', text: 'Test', timestamp: Date.now(), fromMe: true})`
- Run: `await meshDB.getConversation('1234')`
- Verify message is stored and retrieved

---

### Phase 3: Application Logic (Day 3-4)

#### Step 3.1: Core App

**File**: `data/webapp/app.js`

```javascript
class EmergencyMeshApp {
    constructor() {
        this.ws = null;
        this.myNodeId = null;
        this.currentContact = null;
        this.contacts = new Map();
        this.reconnectTimer = null;
    }

    async init() {
        // Initialize database
        await meshDB.init();

        // Load saved settings
        this.myNodeId = await meshDB.getSetting('myNodeId');

        // Setup UI event listeners
        this.setupEventListeners();

        // Load contacts and conversations
        await this.loadContacts();

        // Connect to WebSocket
        this.connectWebSocket();

        console.log('App initialized');
    }

    setupEventListeners() {
        // Send message
        document.getElementById('sendBtn').addEventListener('click', () => {
            this.sendMessage();
        });

        document.getElementById('messageInput').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.sendMessage();
            }
        });

        // Back button (mobile)
        document.getElementById('backBtn').addEventListener('click', () => {
            this.closeConversation();
        });
    }

    connectWebSocket() {
        console.log('Connecting to WebSocket...');
        this.updateStatus('Connecting...', false);

        this.ws = new WebSocket('ws://192.168.4.1:81');

        this.ws.onopen = () => {
            console.log('WebSocket connected');
            this.updateStatus('Connected', true);
            clearTimeout(this.reconnectTimer);
        };

        this.ws.onclose = () => {
            console.log('WebSocket disconnected');
            this.updateStatus('Disconnected', false);

            // Auto-reconnect after 3 seconds
            this.reconnectTimer = setTimeout(() => {
                this.connectWebSocket();
            }, 3000);
        };

        this.ws.onmessage = (event) => {
            this.handleMessage(JSON.parse(event.data));
        };

        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };
    }

    handleMessage(data) {
        console.log('Received:', data);

        switch(data.type) {
            case 'node_info':
                this.handleNodeInfo(data);
                break;
            case 'contact_update':
                this.handleContactUpdate(data);
                break;
            case 'message':
                this.handleIncomingMessage(data);
                break;
            case 'delivery_ack':
                this.handleDeliveryAck(data);
                break;
            case 'read_receipt':
                this.handleReadReceipt(data);
                break;
        }
    }

    async handleNodeInfo(data) {
        this.myNodeId = data.nodeId;
        await meshDB.saveSetting('myNodeId', this.myNodeId);
        console.log('My node ID:', this.myNodeId);
    }

    async handleContactUpdate(data) {
        const contact = {
            nodeId: data.nodeId,
            name: data.name || `Node ${data.nodeId}`,
            role: data.role || '',
            online: data.online !== false,
            lastSeen: Date.now(),
            rssi: data.rssi || 0
        };

        await meshDB.saveContact(contact);
        this.contacts.set(contact.nodeId, contact);
        this.renderContactList();
    }

    async handleIncomingMessage(data) {
        const message = {
            contactId: data.from,
            text: data.text,
            timestamp: data.timestamp || Date.now(),
            fromMe: false,
            status: 'delivered',
            read: false
        };

        await meshDB.saveMessage(message);

        // Update UI if this conversation is open
        if (this.currentContact === data.from) {
            await this.renderConversation();
            await this.markAsRead(data.from);
        } else {
            await this.renderContactList(); // Update unread count
        }

        // Send delivery ACK
        this.sendDeliveryAck(data.from, data.msgId);
    }

    async handleDeliveryAck(data) {
        // Find message by ID and update status
        const messages = await meshDB.getRecentMessages();
        const msg = messages.find(m => m.tempId === data.msgId);

        if (msg) {
            await meshDB.updateMessage(msg.id, {
                status: 'delivered',
                deliveredAt: Date.now()
            });

            if (this.currentContact === msg.contactId) {
                await this.renderConversation();
            }
        }
    }

    async handleReadReceipt(data) {
        // Find message and update to read
        const messages = await meshDB.getRecentMessages();
        const msg = messages.find(m => m.tempId === data.msgId);

        if (msg) {
            await meshDB.updateMessage(msg.id, {
                status: 'read',
                readAt: Date.now()
            });

            if (this.currentContact === msg.contactId) {
                await this.renderConversation();
            }
        }
    }

    async sendMessage() {
        const input = document.getElementById('messageInput');
        const text = input.value.trim();

        if (!text || !this.currentContact) {
            return;
        }

        const tempId = Date.now();
        const message = {
            contactId: this.currentContact,
            text: text,
            timestamp: Date.now(),
            fromMe: true,
            status: 'sending',
            tempId: tempId
        };

        // Save to database
        await meshDB.saveMessage(message);

        // Update UI
        await this.renderConversation();
        input.value = '';

        // Send via WebSocket
        this.ws.send(JSON.stringify({
            type: 'send',
            to: this.currentContact,
            text: text,
            msgId: tempId
        }));
    }

    sendDeliveryAck(toNode, msgId) {
        this.ws.send(JSON.stringify({
            type: 'delivery_ack',
            to: toNode,
            msgId: msgId
        }));
    }

    async markAsRead(contactId) {
        await meshDB.markAsRead(contactId);

        // Send read receipt for all messages
        this.ws.send(JSON.stringify({
            type: 'read_receipt',
            to: contactId
        }));
    }

    async loadContacts() {
        const contacts = await meshDB.getAllContacts();
        contacts.forEach(c => this.contacts.set(c.nodeId, c));
        await this.renderContactList();
    }

    async renderContactList() {
        const list = document.getElementById('contactList');
        list.innerHTML = '';

        const contacts = await meshDB.getAllContacts();
        const conversations = await meshDB.getAllConversations();
        const convMap = new Map(conversations.map(c => [c.contactId, c]));

        for (const contact of contacts) {
            const conv = convMap.get(contact.nodeId) || {};
            const item = this.createContactItem(contact, conv);
            list.appendChild(item);
        }
    }

    createContactItem(contact, conversation) {
        const div = document.createElement('div');
        div.className = 'contact-item';
        if (this.currentContact === contact.nodeId) {
            div.classList.add('active');
        }

        const avatar = document.createElement('div');
        avatar.className = 'contact-avatar';
        avatar.textContent = contact.name.charAt(0).toUpperCase();

        const info = document.createElement('div');
        info.className = 'contact-info';

        const name = document.createElement('div');
        name.className = 'contact-name';
        name.textContent = contact.name;

        const preview = document.createElement('div');
        preview.className = 'contact-preview';
        preview.textContent = conversation.lastMessageText || 'No messages';

        info.appendChild(name);
        info.appendChild(preview);

        const meta = document.createElement('div');
        meta.className = 'contact-meta';

        if (conversation.unreadCount > 0) {
            const badge = document.createElement('div');
            badge.className = 'unread-badge';
            badge.textContent = conversation.unreadCount;
            meta.appendChild(badge);
        }

        div.appendChild(avatar);
        div.appendChild(info);
        div.appendChild(meta);

        div.addEventListener('click', () => {
            this.openConversation(contact.nodeId, contact.name);
        });

        return div;
    }

    async openConversation(nodeId, name) {
        this.currentContact = nodeId;

        // Update UI
        document.getElementById('contactName').textContent = name;
        document.querySelector('.container').classList.add('conversation-open');

        // Mark as read
        await this.markAsRead(nodeId);

        // Render messages
        await this.renderConversation();

        // Update contact list (remove unread badge)
        await this.renderContactList();
    }

    closeConversation() {
        this.currentContact = null;
        document.querySelector('.container').classList.remove('conversation-open');
    }

    async renderConversation() {
        if (!this.currentContact) return;

        const list = document.getElementById('messageList');
        list.innerHTML = '';

        const messages = await meshDB.getConversation(this.currentContact);

        for (const msg of messages) {
            const item = this.createMessageItem(msg);
            list.appendChild(item);
        }

        // Scroll to bottom
        list.scrollTop = list.scrollHeight;
    }

    createMessageItem(msg) {
        const div = document.createElement('div');
        div.className = `message ${msg.fromMe ? 'sent' : 'received'}`;

        const bubble = document.createElement('div');
        bubble.className = 'message-bubble';

        const text = document.createElement('div');
        text.className = 'message-text';
        text.textContent = msg.text;

        const meta = document.createElement('div');
        meta.className = 'message-meta';

        const time = document.createElement('span');
        time.textContent = this.formatTime(msg.timestamp);

        meta.appendChild(time);

        if (msg.fromMe) {
            const status = document.createElement('span');
            status.className = 'message-status';
            status.textContent = this.getStatusIcon(msg.status);
            meta.appendChild(status);
        }

        bubble.appendChild(text);
        bubble.appendChild(meta);
        div.appendChild(bubble);

        return div;
    }

    getStatusIcon(status) {
        switch(status) {
            case 'sending': return '⏳';
            case 'sent': return '✓';
            case 'delivered': return '✓✓';
            case 'read': return '✓✓'; // Could use blue color in CSS
            case 'failed': return '✗';
            default: return '';
        }
    }

    formatTime(timestamp) {
        const date = new Date(timestamp);
        const now = new Date();

        if (date.toDateString() === now.toDateString()) {
            // Today - show time only
            return date.toLocaleTimeString('en-US', {
                hour: '2-digit',
                minute: '2-digit'
            });
        } else {
            // Other day - show date
            return date.toLocaleDateString('en-US', {
                month: 'short',
                day: 'numeric'
            });
        }
    }

    updateStatus(text, online) {
        document.getElementById('statusText').textContent = text;
        const dot = document.getElementById('statusDot');
        dot.className = `dot ${online ? 'online' : 'offline'}`;
    }
}

// Initialize app when page loads
window.addEventListener('load', async () => {
    const app = new EmergencyMeshApp();
    await app.init();

    // Make available globally for debugging
    window.app = app;
});
```

**Test Step 3**:
- Load app in browser
- See "Connecting..." then "Connected" status
- Should see empty contact list
- Console should show "App initialized"

---

### Phase 4: ESP32 Bridge Integration (Day 4-5)

**File**: `src/modules/EmergencyWiFiBridge.cpp`

```cpp
#include "EmergencyWiFiBridge.h"
#include "MeshService.h"
#include "NodeDB.h"
#include <ArduinoJson.h>

EmergencyWiFiBridge emergencyBridge;

void EmergencyWiFiBridge::handleWiFiMessage(const char* json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return;
    }

    const char* type = doc["type"];

    if (strcmp(type, "send") == 0) {
        handleSendMessage(doc);
    } else if (strcmp(type, "delivery_ack") == 0) {
        handleDeliveryAck(doc);
    } else if (strcmp(type, "read_receipt") == 0) {
        handleReadReceipt(doc);
    }
}

void EmergencyWiFiBridge::handleSendMessage(const JsonDocument& doc) {
    const char* toStr = doc["to"];
    const char* text = doc["text"];
    uint32_t msgId = doc["msgId"];

    // Parse destination node ID
    uint32_t destNode = (uint32_t)strtol(toStr, NULL, 16);

    // Create Meshtastic packet
    MeshPacket *p = allocMeshPacket();
    p->to = destNode;
    p->from = nodeDB.getNodeNum();
    p->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
    p->want_ack = true;
    p->id = msgId;

    // Copy text to payload
    size_t len = strlen(text);
    if (len > sizeof(p->decoded.payload.bytes)) {
        len = sizeof(p->decoded.payload.bytes);
    }
    memcpy(p->decoded.payload.bytes, text, len);
    p->decoded.payload.size = len;

    // Send to mesh
    service.sendToMesh(p);

    Serial.printf("Sent to mesh: %s -> %04x\n", text, destNode);
}

void EmergencyWiFiBridge::handleReceived(const MeshPacket &mp) {
    // Only handle text messages
    if (mp.decoded.portnum != PortNum_TEXT_MESSAGE_APP) {
        return;
    }

    // Extract text
    char text[256];
    size_t len = mp.decoded.payload.size;
    if (len > sizeof(text) - 1) len = sizeof(text) - 1;
    memcpy(text, mp.decoded.payload.bytes, len);
    text[len] = '\0';

    // Create JSON for WiFi clients
    DynamicJsonDocument doc(512);
    doc["type"] = "message";
    doc["from"] = String(mp.from, HEX);
    doc["text"] = text;
    doc["timestamp"] = millis();
    doc["msgId"] = mp.id;
    doc["rssi"] = mp.rx_rssi;
    doc["snr"] = mp.rx_snr;

    String json;
    serializeJson(doc, json);

    // Broadcast to all WiFi clients
    wifiService->broadcastToClients(json.c_str());

    Serial.printf("Forwarded to WiFi: %s from %04x\n", text, mp.from);
}

bool EmergencyWiFiBridge::wantPacket(const MeshPacket *p) {
    return p->decoded.portnum == PortNum_TEXT_MESSAGE_APP;
}
```

**Test Step 4**:
- Flash firmware to 2 nodes
- Connect phone to Node A
- Send message "Hello from A"
- Should appear in Node B serial monitor
- Connect another phone to Node B
- Should see message appear in conversation

---

## Build Configuration

**File**: `platformio.ini` addition

```ini
[env:heltec-v2-pwa-poc]
extends = env:heltec-v2.1

build_flags =
  ${env:heltec-v2.1.build_flags}
  -DDISABLE_BLUETOOTH=1
  -DENABLE_WIFI_AP=1
  -DENABLE_EMERGENCY_BRIDGE=1

lib_deps =
  ${env:heltec-v2.1.lib_deps}
  ESP Async WebServer
  WebSockets
  ArduinoJson

board_build.filesystem = spiffs
```

## Build & Flash Commands

```bash
# Build firmware
pio run -e heltec-v2-pwa-poc

# Upload firmware
pio run -e heltec-v2-pwa-poc --target upload

# Upload web app to SPIFFS
pio run -e heltec-v2-pwa-poc --target uploadfs

# Monitor serial
pio device monitor -b 115200
```

---

## Testing Checklist

### Day 1-2: Infrastructure
- [ ] ESP32 WiFi AP starts
- [ ] Phone can connect to "EMRG-TEST-POC"
- [ ] PWA loads at http://192.168.4.1
- [ ] WebSocket connects (green status dot)
- [ ] UI looks good on mobile

### Day 3: Storage
- [ ] IndexedDB initializes
- [ ] Can save/load messages
- [ ] Can save/load contacts
- [ ] Browser DevTools shows data in IndexedDB

### Day 4: Messaging
- [ ] Can type and send message
- [ ] Message appears in sent conversation
- [ ] Status shows ⏳ → ✓
- [ ] Message saves to IndexedDB

### Day 5: Multi-Node
- [ ] Message sent from Node A
- [ ] Arrives at Node B via LoRa
- [ ] Appears in Node B connected phone
- [ ] Delivery ACK returns to Node A
- [ ] Status updates to ✓✓

---

## Next Steps After POC

1. **Contact Discovery**: Auto-populate contacts from NodeDB
2. **Read Receipts**: Blue checkmarks when message viewed
3. **Group Messaging**: Send to multiple nodes
4. **Offline Queue**: Retry messages when reconnected
5. **Service Worker**: True offline app capability

---

**Estimated Total Time**: 5-7 days for working POC

**Success Criteria**: WhatsApp-like messaging between 2 nodes with delivery confirmation
