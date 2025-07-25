// This library is required for Bluetooth Classic functionality.
// It comes pre-installed with the ESP32 board package in Arduino.
#include "BluetoothSerial.h"

// Create an object of the BluetoothSerial class
BluetoothSerial SerialBT;

// --- Configuration ---
// The name your ESP32 will broadcast over Bluetooth
String bluetoothDeviceName = "ESP32_Counter";

// --- Variables for mock data generation ---
int mockValue = 1;       // The starting value
const int maxValue = 10; // The maximum value to send

// --- Variables for non-blocking timer ---
unsigned long lastSendTime = 0;
const long sendInterval = 2000; // Interval in milliseconds (2000ms = 2 seconds)

void setup() {
  // Start the serial monitor for debugging purposes (via USB)
  Serial.begin(115200);
  
  // Initialize the Bluetooth Serial device with the specified name
  // The begin() function returns true on success, false on failure.
  if (!SerialBT.begin(bluetoothDeviceName)) {
    Serial.println("An error occurred initializing Bluetooth");
  } else {
    Serial.print("Bluetooth device ready. Pair with \"");
    Serial.print(bluetoothDeviceName);
    Serial.println("\"");
  }
}

void loop() {
  // Check if a Bluetooth client (e.g., a phone) is connected
  if (SerialBT.hasClient()) {
    
    // Get the current time
    unsigned long currentTime = millis();

    // Check if the 2-second interval has passed since the last send
    if (currentTime - lastSendTime >= sendInterval) {
      
      // Update the last send time to the current time
      lastSendTime = currentTime;

      // --- Send the data ---
      // Send the current mockValue over Bluetooth.
      // Using println() adds a newline, which is easy to read in terminal apps.
      SerialBT.println(mockValue);
      
      // Also, print the sent value to the USB Serial Monitor for debugging
      Serial.print("Sent value via Bluetooth: ");
      Serial.println(mockValue);

      // --- Update the mock value for the next iteration ---
      mockValue++;
      
      // If the value has gone past our maximum (i.e., it is 11),
      // reset it back to 1 for the next loop.
      if (mockValue > maxValue) {
        mockValue = 1;
      }
    }
  }
  // No "else" part is needed, the ESP32 will just keep checking
  // for a client and for the timer until both conditions are met.
}
