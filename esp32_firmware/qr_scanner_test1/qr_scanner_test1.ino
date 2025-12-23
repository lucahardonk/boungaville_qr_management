#include <Arduino.h>

static const uint32_t SCAN_BAUD = 9600;

// Try mapping #1 first:
static const int SCAN_RX = 16;  // ESP32 RX (connect to scanner TX)
static const int SCAN_TX = 17;  // ESP32 TX (connect to scanner RX)

// If you get nothing, swap these two constants:
// static const int SCAN_RX = 17;
// static const int SCAN_TX = 16;
httpsç--www.home'assistant.io-tag-a55e1ab9'08e6'4232'919d'ed5c347bb04e


void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\nESP32-S3 QR scanner UART test");
  Serial.printf("Serial1 baud=%lu RX=%d TX=%d\n", (unsigned long)SCAN_BAUD, SCAN_RX, SCAN_TX);

  Serial1.begin(SCAN_BAUD, SERIAL_8N1, SCAN_RX, SCAN_TX);
  Serial.println("Scan a QR code...");
}

void loop() {
  static String line;

  while (Serial1.available()) {
    char c = (char)Serial1.read();

    // Same behavior as PC terminal: lines end with CR/LF typically
    if (c == '\r' || c == '\n') {
      if (line.length() > 0) {
        Serial.print("QR: ");
        Serial.println(line);
        line = "";
      }
    } else {
      // keep it bounded
      if (line.length() < 512) line += c;
    }
  }
}