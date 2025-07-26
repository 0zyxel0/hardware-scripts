#include <Adafruit_Fingerprint.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Communication Protocol Definition (Enums are kept for readability) ---
enum StatusCode { S_SUCCESS = 1, S_ERROR = 2, S_PROMPT = 3, S_FAILURE = 4, S_READY = 5 };
enum MessageCode {
  M_DEVICE_READY = 1, M_PLACE_FINGER = 10, M_REMOVE_FINGER = 11, M_PLACE_AGAIN = 12,
  M_ACCESS_GRANTED = 20, M_FINGER_NOT_RECOGNIZED = 21, M_FINGERPRINT_STORED = 22,
  M_ENROLLING_NEW_USER = 23, M_ERR_UNKNOWN_COMMAND = 90, M_ERR_DEVICE_BUSY = 91,
  M_ERR_TIMEOUT = 92, M_ERR_IMAGE_PROCESS = 93, M_ERR_DB_FULL = 94, M_ERR_MISMATCH = 95,
  M_ERR_STORE_FAILED = 96
};

// --- Peripherals and State (No changes) ---
#define FINGERPRINT_SERIAL Serial2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FINGERPRINT_SERIAL);
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
enum AppState { IDLE, WAITING_FOR_CHECK, ENROLL_START, ENROLL_STEP_1, ENROLL_WAIT_FOR_REMOVE, ENROLL_STEP_2 };
AppState currentState = IDLE;
unsigned long operationStartTime = 0;
const long operationTimeout = 10000;
const long removeFingerDelay = 2000;
int enrollId = -1;

// --- Function Prototypes ---
void sendBleNotification(const String& message);
void sendShortcodeResponse(StatusCode status, MessageCode message);
void sendShortcodeResponse(StatusCode status, MessageCode message, int dataValue);
void startEnrollment();

// --- Callbacks (No changes) ---
class MyServerCallbacks: public BLEServerCallbacks { void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("Device connected"); } void onDisconnect(BLEServer* pServer) { deviceConnected = false; currentState = IDLE; Serial.println("Device disconnected - advertising again"); BLEDevice::startAdvertising(); } };
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("Received command: "); Serial.println(value);
        if (currentState == IDLE) {
            if (value == "check-user") {
                currentState = WAITING_FOR_CHECK; operationStartTime = millis(); sendShortcodeResponse(S_PROMPT, M_PLACE_FINGER);
            } else if (value == "enroll-user") {
                currentState = ENROLL_START;
            } else {
                sendShortcodeResponse(S_ERROR, M_ERR_UNKNOWN_COMMAND);
            }
        } else {
            sendShortcodeResponse(S_ERROR, M_ERR_DEVICE_BUSY);
        }
      }
    }
};

// --- setup() and loop() (No significant changes) ---
void setup() { Serial.begin(115200); Serial.println("Starting..."); FINGERPRINT_SERIAL.begin(57600, SERIAL_8N1, 16, 17); if (!finger.verifyPassword()) { while (1) delay(1); } finger.getTemplateCount(); BLEDevice::init("ESP32_Fingerprint_Scanner"); pServer = BLEDevice::createServer(); pServer->setCallbacks(new MyServerCallbacks()); BLEService *pService = pServer->createService(SERVICE_UUID); pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY); pCharacteristic->setCallbacks(new MyCharacteristicCallbacks()); pCharacteristic->addDescriptor(new BLE2902()); pService->start(); BLEAdvertising *pAdvertising = BLEDevice::getAdvertising(); pAdvertising->addServiceUUID(SERVICE_UUID); pAdvertising->setScanResponse(true); BLEDevice::startAdvertising(); sendShortcodeResponse(S_READY, M_DEVICE_READY); }
void loop() { if (!deviceConnected) { delay(500); return; } switch (currentState) { case IDLE: break; case WAITING_FOR_CHECK: handleCheckFinger(); break; case ENROLL_START: startEnrollment(); break; case ENROLL_STEP_1: case ENROLL_STEP_2: handleEnrollment(); break; case ENROLL_WAIT_FOR_REMOVE: if (millis() - operationStartTime >= removeFingerDelay) { sendShortcodeResponse(S_PROMPT, M_PLACE_AGAIN); currentState = ENROLL_STEP_2; operationStartTime = millis(); } break; } if (currentState != IDLE && currentState != ENROLL_WAIT_FOR_REMOVE && (millis() - operationStartTime > operationTimeout)) { sendShortcodeResponse(S_ERROR, M_ERR_TIMEOUT); currentState = IDLE; } }


// --- Helper Functions ---
void sendBleNotification(const String& message) { 
  if (deviceConnected) {
    pCharacteristic->setValue(message.c_str()); 
    pCharacteristic->notify();
    Serial.print("Sent Shortcode: "); Serial.println(message);
  }
}

// --- MODIFIED: Replaced JSON functions with Shortcode functions ---
void sendShortcodeResponse(StatusCode status, MessageCode message) {
  // Call the main function with a default data value of 0
  sendShortcodeResponse(status, message, 0);
}

void sendShortcodeResponse(StatusCode status, MessageCode message, int dataValue) {
  // Create a fixed-size character buffer
  char buffer[8]; // 7 characters + null terminator

  // Format the string with leading zeros: SSMMIII
  sprintf(buffer, "%02d%02d%03d", status, message, dataValue);
  
  // Send the resulting string
  sendBleNotification(String(buffer));
}


void handleCheckFinger() {
  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) { sendShortcodeResponse(S_ERROR, M_ERR_IMAGE_PROCESS); currentState = IDLE; return; }
  if (finger.fingerSearch() == FINGERPRINT_OK) {
    sendShortcodeResponse(S_SUCCESS, M_ACCESS_GRANTED, finger.fingerID);
  } else {
    sendShortcodeResponse(S_FAILURE, M_FINGER_NOT_RECOGNIZED);
  }
  currentState = IDLE;
}

void startEnrollment() {
  finger.getTemplateCount();
  if (finger.templateCount >= finger.capacity) { sendShortcodeResponse(S_ERROR, M_ERR_DB_FULL); currentState = IDLE; return; }
  enrollId = finger.templateCount + 1;
  sendShortcodeResponse(S_PROMPT, M_ENROLLING_NEW_USER, enrollId);
  currentState = ENROLL_STEP_1;
  operationStartTime = millis();
}

void handleEnrollment() {
  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz(currentState == ENROLL_STEP_1 ? 1 : 2) != FINGERPRINT_OK) { sendShortcodeResponse(S_ERROR, M_ERR_IMAGE_PROCESS); currentState = IDLE; return; }
  if (currentState == ENROLL_STEP_1) {
    sendShortcodeResponse(S_PROMPT, M_REMOVE_FINGER);
    currentState = ENROLL_WAIT_FOR_REMOVE;
    operationStartTime = millis();
  } else {
    if (finger.createModel() != FINGERPRINT_OK) { sendShortcodeResponse(S_ERROR, M_ERR_MISMATCH); currentState = IDLE; return; }
    if (finger.storeModel(enrollId) == FINGERPRINT_OK) {
      sendShortcodeResponse(S_SUCCESS, M_FINGERPRINT_STORED, enrollId);
    } else {
      sendShortcodeResponse(S_ERROR, M_ERR_STORE_FAILED);
    }
    currentState = IDLE;
  }
}