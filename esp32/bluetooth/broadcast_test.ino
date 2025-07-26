#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// #include <string> // No longer needed for this simple case

// --- BLE Configuration ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- Global BLE Objects ---
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// --- Variables for mock data generation ---
int mockValue = 1;
const int maxValue = 10;

// --- Variables for non-blocking timer ---
unsigned long lastSendTime = 0;
const long sendInterval = 2000; // 2 seconds

// This class handles connection and disconnection events
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected - advertising again");
      BLEDevice::startAdvertising(); // Restart advertising so it can be found again
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE server...");

  // 1. Initialize BLE Device with a name
  BLEDevice::init("ESP32_Counter_BLE");

  // 2. Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // 3. Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 4. Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  // A 2902 descriptor is required for notifications to work
  pCharacteristic->addDescriptor(new BLE2902());

  // 5. Start the service
  pService->start();

  // 6. Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("Characteristic created! Waiting for a client connection...");
}

void loop() {
  // We only send data if a client is connected
  if (deviceConnected) {
    unsigned long currentTime = millis();

    if (currentTime - lastSendTime >= sendInterval) {
      lastSendTime = currentTime;

      mockValue++;
      if (mockValue > maxValue) {
        mockValue = 1;
      }

      // --- THIS IS THE CORRECTED PART ---
      // Convert integer to an Arduino String object
      String valueStr = String(mockValue);
      
      // Set the new value on the characteristic
      pCharacteristic->setValue(valueStr.c_str()); // .c_str() is robust
      
      // Notify the connected client
      pCharacteristic->notify();

      Serial.print("Sent value via BLE Notify: ");
      Serial.println(valueStr);
    }
  }
}