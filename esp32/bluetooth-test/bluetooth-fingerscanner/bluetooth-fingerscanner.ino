#include <Adafruit_Fingerprint.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Fingerprint Sensor Configuration ---
#define FINGERPRINT_SERIAL Serial2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FINGERPRINT_SERIAL);

// --- BLE Configuration ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- Global BLE Objects ---
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// --- State Management ---
enum AppState {
  IDLE,
  WAITING_FOR_CHECK,
  ENROLL_START,
  ENROLL_STEP_1,
  ENROLL_WAIT_FOR_REMOVE, // <-- CHANGED: New state for non-blocking delay
  ENROLL_STEP_2
};
AppState currentState = IDLE;
unsigned long operationStartTime = 0;
const long operationTimeout = 10000; // 10 seconds timeout for scanning
const long removeFingerDelay = 2000; // 2 seconds for non-blocking delay
int enrollId = -1;

// --- Function Prototypes ---
void sendBleMessage(const String& message);
void startEnrollment();
// No longer need getNextFreeId

// --- BLE Server Callbacks ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      currentState = IDLE;
      Serial.println("Device disconnected - advertising again");
      BLEDevice::startAdvertising();
    }
};

// --- BLE Characteristic Callbacks (Handles incoming commands) ---
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("Received command: ");
        Serial.println(value);

        if (currentState == IDLE) {
            if (value == "check-user") {
                currentState = WAITING_FOR_CHECK;
                operationStartTime = millis();
                sendBleMessage("Place finger to check");
            } else if (value == "enroll-user") {
                currentState = ENROLL_START;
            } else {
                sendBleMessage("Error: Unknown command");
            }
        } else {
            sendBleMessage("Error: Device is busy");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 Fingerprint BLE Server...");

  FINGERPRINT_SERIAL.begin(57600, SERIAL_8N1, 16, 17);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }
  
  finger.getTemplateCount();
  Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");

  BLEDevice::init("ESP32_Fingerprint_Scanner");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("Ready to receive commands. Waiting for a client connection...");
  sendBleMessage("Ready");
}


void loop() {
  if (!deviceConnected) {
    // Delay is okay here because nothing important is happening
    delay(500); 
    return;
  }

  // --- Main State Machine ---
  switch (currentState) {
    case IDLE:
      // Waiting for commands
      break;
    case WAITING_FOR_CHECK:
      handleCheckFinger();
      break;
    case ENROLL_START:
      startEnrollment();
      break;
    case ENROLL_STEP_1:
    case ENROLL_STEP_2:
      handleEnrollment();
      break;
    // <-- CHANGED: Handle the non-blocking delay state ---
    case ENROLL_WAIT_FOR_REMOVE:
      if (millis() - operationStartTime >= removeFingerDelay) {
        sendBleMessage("Place same finger again");
        currentState = ENROLL_STEP_2;
        operationStartTime = millis(); // Reset timer for the main timeout
      }
      break;
  }

  // --- Timeout Handling for any active operation ---
  if (currentState != IDLE && currentState != ENROLL_WAIT_FOR_REMOVE && (millis() - operationStartTime > operationTimeout)) {
    Serial.println("Operation timed out.");
    sendBleMessage("Error: Scan timed out");
    currentState = IDLE;
  }
}

// --- Helper Functions ---
void sendBleMessage(const String& message) { 
  if (deviceConnected) {
    pCharacteristic->setValue(message.c_str()); 
    pCharacteristic->notify();
    Serial.print("Sent BLE message: ");
    Serial.println(message);
  }
}

void handleCheckFinger() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    sendBleMessage("Error: Could not process image");
    currentState = IDLE;
    return;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    String response = "Access Granted. User ID: " + String(finger.fingerID);
    sendBleMessage(response);
  } else {
    sendBleMessage("Access Denied: Finger not recognized");
  }
  currentState = IDLE;
}

// <-- CHANGED: New logic for getting enrollment ID
void startEnrollment() {
  // First, get the current number of templates stored
  finger.getTemplateCount();
  
  // The library stores templates from ID 1 up to a max (often 127 or more)
  if (finger.templateCount >= finger.capacity) {
      sendBleMessage("Error: Fingerprint database is full");
      currentState = IDLE;
      return;
  }

  // The next ID is the current count + 1 (since IDs are 1-based)
  enrollId = finger.templateCount + 1;
  
  String msg = "Enrolling ID " + String(enrollId) + ". Place finger.";
  sendBleMessage(msg);
  
  currentState = ENROLL_STEP_1;
  operationStartTime = millis(); // Start timeout timer for the first scan
}

// <-- CHANGED: Reworked the enrollment steps
void handleEnrollment() {
  Serial.println("Waiting for finger to enroll...");
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return; // Still waiting for a finger

  // Process the image, putting it in buffer 1 for the first step, or buffer 2 for the second
  p = finger.image2Tz(currentState == ENROLL_STEP_1 ? 1 : 2);
  if (p != FINGERPRINT_OK) {
      sendBleMessage("Error: Could not process image");
      currentState = IDLE;
      return;
  }

  if (currentState == ENROLL_STEP_1) {
    sendBleMessage("Remove finger");
    currentState = ENROLL_WAIT_FOR_REMOVE; // Go to our new waiting state
    operationStartTime = millis(); // Start the 2-second timer
  } else { // This is ENROLL_STEP_2
    Serial.println("Creating model...");
    p = finger.createModel();
    if (p != FINGERPRINT_OK) {
      sendBleMessage("Error: Fingerprints did not match");
      currentState = IDLE;
      return;
    }

    Serial.print("Storing model at ID #"); Serial.println(enrollId);
    p = finger.storeModel(enrollId);
    if (p == FINGERPRINT_OK) {
      String msg = "Success! Stored at ID " + String(enrollId);
      sendBleMessage(msg);
    } else {
      sendBleMessage("Error: Could not store template");
    }
    currentState = IDLE; // Finish the process
  }
}