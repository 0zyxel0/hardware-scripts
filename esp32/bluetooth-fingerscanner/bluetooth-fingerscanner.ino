// LIBRARIES for OLED ---
#include <Wire.h>
#include <U8g2lib.h>

// --- Original Libraries ---
#include <Adafruit_Fingerprint.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Buzzer Pin Definition ---
#define BUZZER_PIN 4 // GPIO pin connected to the active buzzer

// --- OLED Display Initialization ---
// This constructor is for a 128x64 I2C OLED display.
// Make sure your connections are:
// GND -> GND, VCC -> 3.3V, SCL -> GPIO 22, SDA -> GPIO 21
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);


// --- Communication Protocol Definition (Enums are kept for readability) ---
enum StatusCode
{
  S_SUCCESS = 1,
  S_ERROR = 2,
  S_PROMPT = 3,
  S_FAILURE = 4,
  S_READY = 5
};
enum MessageCode
{
  M_DEVICE_READY = 1,
  M_PLACE_FINGER = 10,
  M_REMOVE_FINGER = 11,
  M_PLACE_AGAIN = 12,
  M_ACCESS_GRANTED = 20,
  M_FINGER_NOT_RECOGNIZED = 21,
  M_FINGERPRINT_STORED = 22,
  M_ENROLLING_NEW_USER = 23,
  M_ERR_UNKNOWN_COMMAND = 90,
  M_ERR_DEVICE_BUSY = 91,
  M_ERR_TIMEOUT = 92,
  M_ERR_IMAGE_PROCESS = 93,
  M_ERR_DB_FULL = 94,
  M_ERR_MISMATCH = 95,
  M_ERR_STORE_FAILED = 96,
  M_DATABASE_CLEARED = 30,
  M_ERR_DATABASE_CLEAR_FAILED = 97
};

// --- Peripherals and State (No changes) ---
#define FINGERPRINT_SERIAL Serial2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FINGERPRINT_SERIAL);
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
enum AppState
{
  IDLE,
  WAITING_FOR_CHECK,
  ENROLL_START,
  ENROLL_STEP_1,
  ENROLL_WAIT_FOR_REMOVE,
  ENROLL_STEP_2
};
AppState currentState = IDLE;
unsigned long operationStartTime = 0;
const long operationTimeout = 10000;
const long removeFingerDelay = 2000;
int enrollId = -1;

// --- Buzzer Helper Function ---
void triggerBeep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000); // Beep for 100 milliseconds
  digitalWrite(BUZZER_PIN, LOW);
}

// --- OLED Helper Function ---
// A simple function to display one or two lines of text on the OLED
void showOledMessage(const char *line1, const char *line2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr); // Set a readable font
  u8g2.drawStr(0, 15, line1);        // Draw the first line
  if (strlen(line2) > 0) {
    u8g2.drawStr(0, 40, line2);     // Draw the second line if it exists
  }
  u8g2.sendBuffer(); // Send the content to the display
}

// --- Function Prototypes ---
void sendBleNotification(const String &message);
void sendShortcodeResponse(StatusCode status, MessageCode message, int dataValue = 0);
void startEnrollment();
void handleFactoryReset();

// --- Callbacks ---
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    Serial.println("Device connected");
    showOledMessage("Connecting..."); // Update OLED
    delay(1000);
    showOledMessage("Connected!"); // Update OLED
    delay(1500);
    showOledMessage("Device Ready");
  }
  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    currentState = IDLE;
    Serial.println("Device disconnected - advertising again");
    showOledMessage("Disconnected", "Advertising..."); // Update OLED
    BLEDevice::startAdvertising();
  }
};
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    String value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.print("Received command: ");
      Serial.println(value);
      if (currentState == IDLE)
      {
        if (value == "check-user")
        {
          currentState = WAITING_FOR_CHECK;
          operationStartTime = millis();
          sendShortcodeResponse(S_PROMPT, M_PLACE_FINGER);
          showOledMessage("Place Finger"); // Update OLED
        }
        else if (value == "enroll-user")
        {
          currentState = ENROLL_START;
        }
        else if (value == "factory-reset")
        {
          handleFactoryReset();
        }
        else
        {
          sendShortcodeResponse(S_ERROR, M_ERR_UNKNOWN_COMMAND);
        }
      }
      else
      {
        sendShortcodeResponse(S_ERROR, M_ERR_DEVICE_BUSY);
      }
    }
  }
};

// --- setup() and loop() (Updated to use new function) ---
void setup()
{
  Serial.begin(115200);

  // --- BUZZER INITIALIZATION ---
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Make sure buzzer is off initially

  // --- OLED INITIALIZATION ---
  u8g2.begin();
  showOledMessage("Booting..."); // <-- SHOW STARTUP MESSAGE
  delay(1500); // Optional: wait a moment so the message can be read

  Serial.println("Starting...");
  FINGERPRINT_SERIAL.begin(57600, SERIAL_8N1, 16, 17);
  if (!finger.verifyPassword())
  {
    showOledMessage("Fingerprint", "Sensor Error!"); // Show error on display
    while (1)
      delay(1);
  }
  finger.getTemplateCount();
  BLEDevice::init("SC_Finger_Reader");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  sendShortcodeResponse(S_READY, M_DEVICE_READY);
  
  showOledMessage("Device Ready"); // <-- SHOW READY MESSAGE
}
void loop()
{
  if (!deviceConnected)
  {
    // If not connected, we just wait. The onConnect callback will handle the screen update.
    delay(500);
    return;
  }
  switch (currentState)
  {
  case IDLE:
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
  case ENROLL_WAIT_FOR_REMOVE:
    if (millis() - operationStartTime >= removeFingerDelay)
    {
      sendShortcodeResponse(S_PROMPT, M_PLACE_AGAIN);
      showOledMessage("Place Again"); // Update OLED
      currentState = ENROLL_STEP_2;
      operationStartTime = millis();
    }
    break;
  }
  if (currentState != IDLE && currentState != ENROLL_WAIT_FOR_REMOVE && (millis() - operationStartTime > operationTimeout))
  {
    sendShortcodeResponse(S_ERROR, M_ERR_TIMEOUT);
    showOledMessage("Timeout!", "Please retry."); // Update OLED
    currentState = IDLE;
    delay(2000);
    showOledMessage("Device Ready");
  }
}

// --- Helper Functions ---
void sendBleNotification(const String &message)
{
  if (deviceConnected)
  {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
    Serial.print("Sent Shortcode: ");
    Serial.println(message);
  }
}

void sendShortcodeResponse(StatusCode status, MessageCode message, int dataValue)
{
  char buffer[8];
  sprintf(buffer, "%02d%02d%03d", (int)status, (int)message, dataValue);
  sendBleNotification(String(buffer));
}

void handleCheckFinger()
{
  if (finger.getImage() != FINGERPRINT_OK)
    return;

  // --- TRIGGER BEEP ON SCAN ---
  triggerBeep();

  if (finger.image2Tz() != FINGERPRINT_OK)
  {
    sendShortcodeResponse(S_ERROR, M_ERR_IMAGE_PROCESS);
    showOledMessage("Error", "Try again");
    currentState = IDLE;
    delay(2000);
    showOledMessage("Device Ready");
    return;
  }
  if (finger.fingerSearch() == FINGERPRINT_OK)
  {
    sendShortcodeResponse(S_SUCCESS, M_ACCESS_GRANTED, finger.fingerID);
    showOledMessage("Access Granted!");
  }
  else
  {
    sendShortcodeResponse(S_FAILURE, M_FINGER_NOT_RECOGNIZED);
    showOledMessage("Access Denied");
  }
  currentState = IDLE;
  delay(2000); // Wait so user can see the message
  showOledMessage("Device Ready");
}

void startEnrollment()
{
  finger.getTemplateCount();
  if (finger.templateCount >= finger.capacity)
  {
    sendShortcodeResponse(S_ERROR, M_ERR_DB_FULL);
    showOledMessage("Database Full");
    currentState = IDLE;
    delay(2000);
    showOledMessage("Device Ready");
    return;
  }
  enrollId = finger.templateCount + 1;
  sendShortcodeResponse(S_PROMPT, M_ENROLLING_NEW_USER, enrollId);
  showOledMessage("Enroll:", "Place Finger"); // Update OLED
  currentState = ENROLL_STEP_1;
  operationStartTime = millis();
}

void handleEnrollment()
{
  if (finger.getImage() != FINGERPRINT_OK)
    return;

  // --- TRIGGER BEEP ON SCAN ---
  triggerBeep();
  
  if (finger.image2Tz(currentState == ENROLL_STEP_1 ? 1 : 2) != FINGERPRINT_OK)
  {
    sendShortcodeResponse(S_ERROR, M_ERR_IMAGE_PROCESS);
    showOledMessage("Error", "Try Again");
    currentState = IDLE;
    delay(2000);
    showOledMessage("Device Ready");
    return;
  }
  if (currentState == ENROLL_STEP_1)
  {
    sendShortcodeResponse(S_PROMPT, M_REMOVE_FINGER);
    showOledMessage("Remove Finger");
    currentState = ENROLL_WAIT_FOR_REMOVE;
    operationStartTime = millis();
  }
  else
  {
    if (finger.createModel() != FINGERPRINT_OK)
    {
      sendShortcodeResponse(S_ERROR, M_ERR_MISMATCH);
      showOledMessage("Prints Don't", "Match. Retry.");
      currentState = IDLE;
    }
    else if (finger.storeModel(enrollId) == FINGERPRINT_OK)
    {
      sendShortcodeResponse(S_SUCCESS, M_FINGERPRINT_STORED, enrollId);
      showOledMessage("Success!", "Finger Stored");
    }
    else
    {
      sendShortcodeResponse(S_ERROR, M_ERR_STORE_FAILED);
      showOledMessage("Error", "Storing Failed");
    }
    currentState = IDLE;
    delay(2000);
    showOledMessage("Device Ready");
  }
}

void handleFactoryReset()
{
  Serial.println("Attempting to clear fingerprint database...");
  showOledMessage("Clearing DB...");
  uint8_t result = finger.emptyDatabase();
  if (result == FINGERPRINT_OK)
  {
    sendShortcodeResponse(S_SUCCESS, M_DATABASE_CLEARED);
    showOledMessage("Database Cleared");
    Serial.println("Database cleared successfully.");
  }
  else
  {
    sendShortcodeResponse(S_ERROR, M_ERR_DATABASE_CLEAR_FAILED);
    showOledMessage("Clear Failed!");
    Serial.println("Failed to clear database.");
  }
  delay(2000);
  showOledMessage("Device Ready");
}