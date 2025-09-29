#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// Initialize the OLED display using the U8g2 library
// It's common for 128x64 I2C OLEDs to use the SSD1306 or a compatible controller.
// If this constructor doesn't work, you might try U8G2_CH1116_128X64_NONAME_F_HW_I2C.

// GND to GND: Ensure the OLED's GND pin is firmly connected to a GND pin on the ESP32.
// VCC to 3.3V: Make sure the OLED's VCC is connected to the 3.3V pin on the ESP32, not the 5V (Vin) pin.
// SCL to GPIO 22: Confirm that SCL on the OLED is connected to pin GPIO 22 on your ESP32.
// SDA to GPIO 21: Confirm that SDA on the OLED is connected to pin GPIO 21 on your ESP32.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void setup(void) {
  // Begin communication with the display
  u8g2.begin();
}

void loop(void) {
  // Clear the internal buffer
  u8g2.clearBuffer();
  // Set the font for the text
  u8g2.setFont(u8g2_font_ncenB08_tr);
  // Draw the string at position (0, 15)
  u8g2.drawStr(0, 15, "Hello, World!");
  // Send the buffer to the display
  u8g2.sendBuffer();
  // Wait for 2 seconds before repeating
  delay(2000);
}