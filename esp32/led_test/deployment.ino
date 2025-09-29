/*
  ESP32 LED Blink Example

  This code blinks the built-in LED on an ESP32 development board
  every 3 seconds.

  Most ESP32 boards have a built-in LED connected to GPIO pin 2.
*/

// Define the LED pin
#define LED_BUILTIN 2

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(3000);                      // wait for 3 seconds
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(3000);                      // wait for 3 seconds
}