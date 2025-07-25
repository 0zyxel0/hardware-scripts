#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_Fingerprint.h>
#include <ESPmDNS.h>      // For mDNS service discovery
#include "ArduinoJson.h" // For easy JSON parsing and creation

// --- Configuration ---
// These are for the Access Point the ESP32 will create
const char* ap_ssid = "ESP32-Fingerprint-Scanner"; // The name of the Wi-Fi network
const char* ap_password = "password123";           // The password for the Wi-Fi network. Use NULL for an open network.

// Use Serial2 for the fingerprint sensor. TX -> RX, RX -> TX
HardwareSerial mySerial(2); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

AsyncWebServer server(80); // Web server on port 80
AsyncWebSocket ws("/ws");   // WebSocket is available at ws://<ESP_IP>/ws

// A flag to indicate a scan is requested and in progress
volatile bool scanRequested = false; 

// --- WebSocket Event Handler (This section is unchanged) ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String message = (char*)data;
      Serial.printf("Received message: %s\n", message.c_str());

      JsonDocument doc;
      deserializeJson(doc, message);
      const char* command = doc["command"];

      if (strcmp(command, "start_scan") == 0) {
        Serial.println("Received start_scan command. Flagging for main loop.");
        scanRequested = true;
      }
    }
  }
}

// --- Helper function to send scan result (This section is unchanged) ---
void notifyClients(const String& status, const String& message, int fingerId = -1, int confidence = -1) {
    JsonDocument doc;
    doc["status"] = status;
    doc["message"] = message;
    if (fingerId != -1) {
        doc["fingerId"] = fingerId;
        doc["confidence"] = confidence;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.printf("Sending result: %s\n", jsonString.c_str());
    ws.textAll(jsonString);
}


void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- Fingerprint Sensor Setup (Unchanged) ---
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  // --- CHANGED SECTION: Wi-Fi Access Point (AP) Setup ---
  Serial.print("Setting up Access Point...");
  WiFi.softAP(ap_ssid, ap_password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.println(" AP Created!");
  Serial.print("AP IP address: ");
  Serial.println(myIP); // This will usually be 192.168.4.1

  // Start mDNS. This will allow you to connect using http://esp32.local
  // It's more user-friendly than remembering the IP address.
  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started. You can connect to http://esp32.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  // --- END OF CHANGED SECTION ---

  // --- WebSocket Server Setup (Unchanged) ---
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Start Server
  server.begin();
  Serial.println("Web server started.");
}

// --- Main loop (Unchanged) ---
void loop() {
  if (scanRequested) {
    scanRequested = false;
    Serial.println("Starting fingerprint scan...");

    int p = finger.getImage();
    if (p != FINGERPRINT_OK) {
        notifyClients("error", "Failed to get image");
        return;
    }

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) {
        notifyClients("error", "Failed to convert image");
        return;
    }

    p = finger.fingerSearch();
    if (p == FINGERPRINT_OK) {
        Serial.println("Found a print match!");
        notifyClients("success", "Fingerprint matched!", finger.fingerID, finger.confidence);
    } else if (p == FINGERPRINT_NOTFOUND) {
        Serial.println("Did not find a match");
        notifyClients("error", "Finger not found");
    } else {
        Serial.println("Unknown error");
        notifyClients("error", "Unknown sensor error");
    }
  }
}