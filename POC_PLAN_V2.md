# Emergency WiFi Bridge - Proof of Concept Plan v2
## WhatsApp-Style Contact Messaging

**Goal**: WhatsApp-like UX for emergency responders over LoRa mesh

**Success Criteria**: First responder on Node A can send a text message to a specific contact on Node B, message delivers asynchronously, sender sees delivery + read receipts, and group messaging works.

---

## POC Scope - WhatsApp-Like Features

### Core UX Requirements (from WhatsApp)
```
✅ Contact-based messaging (not broadcast)
✅ Send to specific contact → they receive async
✅ Delivery receipt (✓ message reached their device)
✅ Read receipt (✓✓ message was viewed)
✅ Group messaging (send to multiple contacts)
✅ Conversation view (message history per contact)
✅ Offline message queueing (store-and-forward)
```

### POC Architecture
```
[Responder A's Phone]
    ↓ WiFi
[Node A - "Fire-Chief-Alpha"]
    ↓ LoRa Mesh
[Node B - "Medic-Bravo"]
    ↓ WiFi
[Responder B's Phone]

Message Flow:
1. A types "Need medical at Grid 123" → selects contact "Medic-Bravo"
2. Node A sends via LoRa mesh with delivery tracking
3. Node B receives, stores, notifies WiFi client
4. B's phone displays message → sends read receipt
5. A's phone shows: "Need medical..." ✓✓ (delivered & read)
```

---

## Data Model Changes

### 1. User/Contact Identity

Each node has:
- **Node ID**: Hardware address (e.g., `0x1a2b3c4d`)
- **User Profile**: Name, role, unit (e.g., "Fire Chief Alpha", "Engine 5")
- **Contact List**: Known users on the mesh

```cpp
struct UserProfile {
    char name[32];          // "Fire Chief Alpha"
    char role[16];          // "Incident Commander"
    char unit[16];          // "Engine 5"
    uint32_t nodeId;        // LoRa node hardware ID
};

struct Contact {
    UserProfile profile;
    uint32_t lastSeen;      // Last time this node was on mesh
    int16_t rssi;           // Signal strength
    bool online;            // Currently reachable
    uint16_t unreadCount;   // Unread messages from this contact
};

std::vector<Contact> contactList;
```

### 2. Message Model

```cpp
struct Message {
    uint32_t msgId;         // Unique message ID
    uint32_t fromNode;      // Sender node ID
    uint32_t toNode;        // Recipient node ID (or group ID)
    uint32_t timestamp;     // Unix timestamp
    uint8_t type;           // DIRECT | GROUP | BROADCAST
    char text[256];         // Message content

    // Delivery tracking
    enum Status {
        SENDING,            // ⏳ Being sent
        SENT,               // ✓ Left this device
        DELIVERED,          // ✓✓ Reached recipient device
        READ,               // ✓✓ (blue) Recipient viewed it
        FAILED              // ✗ Delivery failed
    };
    Status status;

    uint32_t deliveredAt;   // Timestamp of delivery ACK
    uint32_t readAt;        // Timestamp of read receipt
};
```

### 3. Conversation Model

```cpp
struct Conversation {
    uint32_t contactNodeId; // Who this conversation is with
    std::vector<Message> messages;
    uint32_t lastActivity;
    uint16_t unreadCount;
};

std::map<uint32_t, Conversation> conversations; // nodeId → conversation
```

### 4. Group Model (for group messaging)

```cpp
struct Group {
    uint32_t groupId;       // Unique group identifier
    char name[32];          // "Incident Command Team"
    std::vector<uint32_t> members; // Node IDs of members
    uint32_t createdBy;
    uint32_t createdAt;
};

std::vector<Group> groups;
```

---

## POC Implementation Roadmap - WhatsApp Style

### Step 1: User Profile & Contact Discovery (Day 1-2)
**Goal**: Each node broadcasts its identity, discovers other nodes

**Tasks**:
1. Add user profile to node configuration
2. Broadcast user profile periodically (every 60s)
3. Listen for other nodes' profiles
4. Build contact list automatically
5. Display contacts in web UI

**Test**: Power on 2 nodes, wait 60s, see each other in contact list

**Meshtastic Integration**:
- Use `PortNum_NODEINFO_APP` for profile broadcasts (already exists!)
- Store in `NodeDB` (Meshtastic already does this)
- Just expose it to WiFi clients

**Code Stub**:
```cpp
// User profile broadcast (Meshtastic already does this!)
void broadcastUserProfile() {
    MeshPacket *p = allocMeshPacket();
    p->to = NODEDB_BROADCAST_ADDR;
    p->decoded.portnum = PortNum_NODEINFO_APP;

    User user;
    strncpy(user.long_name, config.device.name, sizeof(user.long_name));
    strncpy(user.short_name, config.device.short_name, sizeof(user.short_name));
    user.macaddr = getMacAddress();

    pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                       User_fields, &user);
    p->decoded.payload.size = ...;

    service.sendToMesh(p);
}

// Expose contacts to WiFi
void EmergencyWiFiBridge::sendContactListToWiFi(uint8_t clientId) {
    DynamicJsonDocument doc(2048);
    JsonArray contacts = doc.createNestedArray("contacts");

    // Iterate over NodeDB
    for (int i = 0; i < nodeDB.getNumNodes(); i++) {
        NodeInfo *node = nodeDB.getNodeByIndex(i);
        if (node && node->has_user) {
            JsonObject c = contacts.createNestedObject();
            c["id"] = node->num;
            c["name"] = node->user.long_name;
            c["role"] = node->user.role;
            c["online"] = (millis() - node->last_heard) < 300000; // 5 min
            c["rssi"] = node->snr;
        }
    }

    String json;
    serializeJson(doc, json);
    wifiService.sendToClient(clientId, json.c_str());
}
```

**Web UI Update**:
```javascript
// Contact list in UI
<div id="contacts"></div>
<div id="conversation"></div>

let contacts = [];
let currentContact = null;

ws.onmessage = (e) => {
    const data = JSON.parse(e.data);
    if (data.contacts) {
        contacts = data.contacts;
        renderContactList();
    }
};

function renderContactList() {
    const div = document.getElementById('contacts');
    div.innerHTML = '<h3>Contacts</h3>';
    contacts.forEach(c => {
        const online = c.online ? '🟢' : '⚫';
        const unread = c.unreadCount > 0 ? `(${c.unreadCount})` : '';
        div.innerHTML += `
            <div onclick="selectContact('${c.id}')" style="cursor:pointer;">
                ${online} ${c.name} ${unread}
            </div>
        `;
    });
}

function selectContact(contactId) {
    currentContact = contactId;
    loadConversation(contactId);
}
```

**Estimated Time**: 6-8 hours

---

### Step 2: Conversation View (Day 2-3)
**Goal**: WhatsApp-like conversation UI per contact

**Tasks**:
1. Store messages per contact (conversation model)
2. Load conversation when contact selected
3. Display messages with sender/timestamp
4. Show delivery status (⏳ ✓ ✓✓)

**Test**: Select contact, see message history, send new message

**Web UI** (WhatsApp-style):
```html
<div class="chat-container">
    <!-- Contact List (left side) -->
    <div class="contacts-panel">
        <h3>Contacts</h3>
        <div id="contactList"></div>
    </div>

    <!-- Conversation (right side) -->
    <div class="conversation-panel">
        <div class="conversation-header">
            <strong id="contactName">Select a contact</strong>
            <span id="contactStatus"></span>
        </div>
        <div class="messages" id="messages"></div>
        <div class="compose">
            <input type="text" id="msgInput" placeholder="Type message..." />
            <button onclick="send()">Send</button>
        </div>
    </div>
</div>

<style>
.chat-container { display: flex; height: 100vh; }
.contacts-panel { width: 30%; background: #2a2a2a; overflow-y: scroll; }
.conversation-panel { width: 70%; display: flex; flex-direction: column; }
.conversation-header { background: #333; padding: 15px; border-bottom: 1px solid #555; }
.messages { flex: 1; overflow-y: scroll; padding: 20px; background: #1a1a1a; }
.message { margin-bottom: 15px; }
.message.sent { text-align: right; }
.message.received { text-align: left; }
.message .bubble {
    display: inline-block;
    padding: 10px 15px;
    border-radius: 15px;
    max-width: 70%;
}
.message.sent .bubble { background: #005c4b; }
.message.received .bubble { background: #2a2a2a; }
.message .status { font-size: 0.8em; color: #888; margin-top: 5px; }
.message .status.delivered { color: #4CAF50; }
.message .status.read { color: #34B7F1; }
</style>

<script>
let conversations = {}; // contactId → array of messages

function loadConversation(contactId) {
    currentContact = contactId;
    const contact = contacts.find(c => c.id === contactId);

    document.getElementById('contactName').textContent = contact.name;
    document.getElementById('contactStatus').textContent = contact.online ? '🟢 Online' : '⚫ Offline';

    // Request conversation from node
    ws.send(JSON.stringify({type: 'get_conversation', contactId: contactId}));
}

function renderConversation(messages) {
    const div = document.getElementById('messages');
    div.innerHTML = '';

    messages.forEach(msg => {
        const isSent = msg.fromNode === myNodeId;
        const msgDiv = document.createElement('div');
        msgDiv.className = `message ${isSent ? 'sent' : 'received'}`;

        let statusIcon = '';
        if (isSent) {
            if (msg.status === 'read') statusIcon = '✓✓';
            else if (msg.status === 'delivered') statusIcon = '✓✓';
            else if (msg.status === 'sent') statusIcon = '✓';
            else if (msg.status === 'sending') statusIcon = '⏳';
            else if (msg.status === 'failed') statusIcon = '✗';
        }

        msgDiv.innerHTML = `
            <div class="bubble">
                ${msg.text}
                <div class="status ${msg.status}">${statusIcon} ${formatTime(msg.timestamp)}</div>
            </div>
        `;
        div.appendChild(msgDiv);
    });

    div.scrollTop = div.scrollHeight;
}

function send() {
    const input = document.getElementById('msgInput');
    if (!input.value || !currentContact) return;

    const msg = {
        type: 'send',
        to: currentContact,
        text: input.value,
        timestamp: Date.now()
    };

    ws.send(JSON.stringify(msg));
    input.value = '';
}
</script>
```

**Estimated Time**: 8-10 hours

---

### Step 3: Message Persistence & Store-and-Forward (Day 3-4)
**Goal**: Messages persist, deliver when recipient comes online

**Tasks**:
1. Store sent messages in flash (SPIFFS)
2. Keep messages until delivery ACK received
3. Retry sending if no ACK within timeout
4. Queue incoming messages for offline WiFi clients

**Test**: Send message while Node B is off, power on Node B, message delivers

**Code Stub**:
```cpp
class MessageQueue {
private:
    std::vector<Message> outbox; // Pending sent messages
    std::vector<Message> inbox;  // Undelivered received messages

public:
    void addToOutbox(Message msg) {
        msg.status = Message::SENDING;
        outbox.push_back(msg);
        saveToDisk();

        // Send immediately if recipient online
        sendToMesh(msg);

        // Set retry timer
        scheduleRetry(msg.msgId, 30000); // 30 sec
    }

    void onDeliveryACK(uint32_t msgId) {
        for (auto it = outbox.begin(); it != outbox.end(); ++it) {
            if (it->msgId == msgId) {
                it->status = Message::DELIVERED;
                it->deliveredAt = millis();

                // Notify WiFi client
                notifyStatusChange(*it);

                // Keep for a while for read receipt
                // Will remove after read or 24h timeout
                saveToDisk();
                break;
            }
        }
    }

    void onReadReceipt(uint32_t msgId) {
        for (auto it = outbox.begin(); it != outbox.end(); ++it) {
            if (it->msgId == msgId) {
                it->status = Message::READ;
                it->readAt = millis();

                notifyStatusChange(*it);

                // Can remove from outbox now
                outbox.erase(it);
                saveToDisk();
                break;
            }
        }
    }

    void retryPending() {
        for (auto &msg : outbox) {
            if (msg.status == Message::SENDING &&
                millis() - msg.timestamp > 30000) {
                // Retry
                sendToMesh(msg);
            }
        }
    }

    void saveToDisk() {
        // Write to SPIFFS
        File f = SPIFFS.open("/messages.dat", "w");
        // Serialize outbox + inbox
        f.close();
    }

    void loadFromDisk() {
        // Read from SPIFFS on boot
        File f = SPIFFS.open("/messages.dat", "r");
        // Deserialize
        f.close();
    }
};
```

**Meshtastic Integration**:
- Meshtastic has `StoreForwardModule` - we can use it!
- Or implement lightweight version for POC

**Estimated Time**: 8-10 hours

---

### Step 4: Delivery & Read Receipts (Day 4-5)
**Goal**: WhatsApp-style ✓ (sent) ✓✓ (delivered) ✓✓ (read - blue)

**Tasks**:
1. Send delivery ACK when message arrives at node
2. Send read receipt when user views message in UI
3. Update message status on sender side
4. Show status in UI with icons

**Receipt Flow**:
```
Sender                  Mesh                Recipient
  |                      |                      |
  |--[Message]---------->|--[Message]---------->|
  |                      |                      | (arrives at node)
  |<--[Delivery ACK]-----|<--[Delivery ACK]-----|
  |  (✓✓ delivered)      |                      |
  |                      |                      | (user opens UI)
  |                      |                      | (message displayed)
  |<--[Read Receipt]-----|<--[Read Receipt]-----|
  |  (✓✓ blue - read)    |                      |
```

**Code Stub**:
```cpp
// Recipient node: Send delivery ACK
void EmergencyWiFiBridge::handleReceived(const MeshPacket &mp) {
    if (mp.decoded.portnum != PortNum_TEXT_MESSAGE_APP)
        return;

    // Store message
    Message msg = parseMessage(mp);
    messageQueue.addToInbox(msg);

    // Send delivery ACK immediately
    sendDeliveryACK(mp.from, mp.id);

    // Forward to WiFi client if online
    if (wifiService.hasClients()) {
        forwardToWiFi(msg);
    }
}

void sendDeliveryACK(uint32_t toNode, uint32_t msgId) {
    MeshPacket *p = allocMeshPacket();
    p->to = toNode;
    p->decoded.portnum = PortNum_PRIVATE_APP; // Custom port for receipts

    // Encode ACK
    Receipt receipt;
    receipt.msgId = msgId;
    receipt.type = Receipt::DELIVERY;
    receipt.timestamp = millis();

    pb_encode_to_bytes(...);
    service.sendToMesh(p);
}

// Web UI: Send read receipt when message viewed
function markAsRead(msgId) {
    ws.send(JSON.stringify({
        type: 'read_receipt',
        msgId: msgId
    }));
}

// Node: Forward read receipt to sender
void EmergencyWiFiBridge::onReadReceipt(uint32_t msgId, uint8_t fromClient) {
    // Find original message sender
    Message *msg = messageQueue.findInInbox(msgId);
    if (msg) {
        sendReadReceipt(msg->fromNode, msgId);
    }
}

void sendReadReceipt(uint32_t toNode, uint32_t msgId) {
    MeshPacket *p = allocMeshPacket();
    p->to = toNode;
    p->decoded.portnum = PortNum_PRIVATE_APP;

    Receipt receipt;
    receipt.msgId = msgId;
    receipt.type = Receipt::READ;
    receipt.timestamp = millis();

    pb_encode_to_bytes(...);
    service.sendToMesh(p);
}
```

**Estimated Time**: 6-8 hours

---

### Step 5: Group Messaging (Day 5-6)
**Goal**: Send message to multiple contacts at once

**Tasks**:
1. Create group definition (name + members)
2. Send to group → broadcast to all members
3. Track delivery per member
4. Show group conversation in UI

**Test**: Create group "Incident Command" with 3 members, send message, all receive

**Code Stub**:
```cpp
struct Group {
    uint32_t groupId;
    char name[32];
    std::vector<uint32_t> members;
};

void sendToGroup(uint32_t groupId, const char* text) {
    Group *group = findGroup(groupId);
    if (!group) return;

    Message msg;
    msg.type = Message::GROUP;
    msg.toNode = groupId;
    strncpy(msg.text, text, sizeof(msg.text));

    // Send to each member
    for (uint32_t memberId : group->members) {
        MeshPacket *p = createMessagePacket(msg, memberId);
        service.sendToMesh(p);

        // Track per-member delivery
        trackGroupMessage(msg.msgId, memberId);
    }
}

// UI: Show group delivery status
function renderGroupMessage(msg) {
    // msg.deliveryStatus = {nodeA: 'delivered', nodeB: 'read', nodeC: 'sending'}
    let status = '';
    const delivered = Object.values(msg.deliveryStatus).filter(s => s === 'delivered' || s === 'read').length;
    const read = Object.values(msg.deliveryStatus).filter(s => s === 'read').length;

    if (read === msg.memberCount) status = '✓✓ Read by all';
    else if (delivered === msg.memberCount) status = '✓✓ Delivered to all';
    else status = `✓ ${delivered}/${msg.memberCount}`;

    return status;
}
```

**Estimated Time**: 8-10 hours

---

### Step 6: Offline Queueing & Sync (Day 6-7)
**Goal**: Messages deliver when recipient comes back online

**Tasks**:
1. Detect when contact comes online (via heartbeat)
2. Flush queued messages to newly online node
3. Show "offline" indicator in UI
4. Handle message conflicts (same message sent twice)

**Test**: Send 3 messages while Node B offline, power on Node B, all 3 deliver in order

**Code Stub**:
```cpp
void EmergencyWiFiBridge::onNodeOnline(uint32_t nodeId) {
    Serial.printf("Node %04x came online, flushing queue\n", nodeId);

    // Find all pending messages for this node
    std::vector<Message*> pending = messageQueue.getPendingForNode(nodeId);

    for (Message *msg : pending) {
        if (msg->status == Message::SENDING) {
            sendToMesh(*msg);
        }
    }
}

// Detect node online via NodeInfo broadcast
void handleNodeInfo(const MeshPacket &mp) {
    NodeInfo *node = nodeDB.getNode(mp.from);
    bool wasOffline = (node->last_heard < millis() - 300000); // Was offline >5 min

    if (wasOffline) {
        // Node just came back online
        onNodeOnline(mp.from);
    }

    // Update contact status in UI
    notifyContactStatusChange(mp.from, true);
}
```

**Estimated Time**: 6-8 hours

---

## POC Testing Scenarios - WhatsApp UX

### Scenario 1: Direct Message with Receipts
1. Node A (Fire Chief) sends "Status update needed" to Node B (Medic)
2. Node A UI shows: ⏳ Sending...
3. Message reaches Node B: Node A UI shows ✓ Sent
4. Node B delivers to WiFi client: Node A UI shows ✓✓ Delivered
5. Medic opens conversation on phone: Node A UI shows ✓✓ Read (blue)

### Scenario 2: Offline Message Delivery
1. Node B powered off
2. Node A sends "Grid ref 12S UD 123 456"
3. Node A UI shows: ⏳ Sending... (stays in outbox)
4. Power on Node B
5. Within 60 seconds: Message delivers, Node A sees ✓✓

### Scenario 3: Group Message
1. Create group "Rescue Team" with Node A, B, C
2. Node A sends "Mobilize to sector 7"
3. All members receive message
4. Node A shows: ✓ 2/3 delivered, then ✓✓ 3/3 delivered
5. As members read: ✓✓ 1/3 read, ✓✓ 2/3 read, ✓✓ Read by all

### Scenario 4: Conversation History
1. Send 10 messages back and forth between Node A and B
2. Close WiFi connection on A
3. Reconnect WiFi on A
4. Load conversation - all 10 messages still there (persisted)

---

## Implementation Priority

### POC Minimum (Week 1-2)
- ✅ Contact discovery
- ✅ Direct messaging (1-to-1)
- ✅ Delivery ACK (✓✓)
- ✅ Conversation view
- ✅ Message persistence

### POC Enhanced (Week 2-3)
- ✅ Read receipts (✓✓ blue)
- ✅ Offline queueing
- ✅ Group messaging (basic)

### Post-POC
- ⏳ Media messages (images, voice)
- ⏳ Message editing/deletion
- ⏳ Typing indicators
- ⏳ Push notifications
- ⏳ Multi-device sync

---

## Technical Debt from WhatsApp UX

**Storage Requirements**:
- Contact list: ~2KB (50 contacts)
- Message queue: ~50KB (200 messages)
- Conversations: ~100KB total
- **Total**: ~150KB flash needed (we have 448KB SPIFFS)

**Memory Requirements**:
- Active conversations: ~20KB RAM
- Contact list: ~5KB RAM
- Message buffers: ~10KB RAM
- **Total**: ~35KB RAM (we have 320KB)

**Performance Considerations**:
- Message latency: <5 sec (3 hops)
- Read receipt latency: <3 sec (1 hop)
- UI update frequency: Real-time via WebSocket
- Contact sync: Every 60 sec

---

## Time Estimate - WhatsApp UX POC

**Total**: 10-14 days (80-112 hours)

**Breakdown**:
- Contact discovery: 6-8h
- Conversation UI: 8-10h
- Message persistence: 8-10h
- Delivery receipts: 6-8h
- Read receipts: 4-6h
- Group messaging: 8-10h
- Offline queueing: 6-8h
- Integration & testing: 16-24h
- Bug fixes & polish: 12-16h

**Parallel work**: UI can be developed alongside backend logic

---

## Next Steps

1. **Review this plan** - Does this match your WhatsApp-like vision?
2. **Adjust priorities** - What's most important for POC?
3. **Start implementation** - Begin with contact discovery?

**Question**: For the POC, should we:
- **Option A**: Full WhatsApp UX (10-14 days, more complex)
- **Option B**: Simplified version first (contact + direct msg + ACK only, 5-7 days)

Which approach do you prefer?
