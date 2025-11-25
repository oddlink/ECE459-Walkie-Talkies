/*
  nRF24L01 send-every-5s / receive sketch
  - Set IS_TRANSMITTER to true on one ESP, false on the other
  - Change NODE_ID to identify each board in messages
  - Wiring: CE=4, CSN=5, SCK=18, MOSI=23, MISO=19, VCC=3.3V, GND=GND
*/

#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// ------ CONFIG ------
#define IS_TRANSMITTER true   // <-- set true for the sender, false for the receiver
#define NODE_ID 1             // change per board for easier identification

// nRF pins (matches your project wiring)
const uint8_t CE_PIN  = 4;
const uint8_t CSN_PIN = 5;

// address shared by both nodes
const byte RF_ADDR[5] = {'W','A','L','K','I'};

RF24 radio(CE_PIN, CSN_PIN);

unsigned long lastSendMs = 0;
const unsigned long SEND_INTERVAL_MS = 5000; // 5 seconds
unsigned long seq = 0;

void setupRadio() {
  if (!radio.begin()) {
    Serial.println("⚠️ RF24 init failed! Check wiring/power.");
    while (1) delay(1000);
  }

  radio.setChannel(90);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(3, 5);      // small retries
  radio.openWritingPipe(RF_ADDR);
  radio.openReadingPipe(1, RF_ADDR);
  radio.setPayloadSize(32);    // 32-byte payloads
  if (IS_TRANSMITTER) {
    radio.stopListening();
  } else {
    radio.startListening();
  }
}

void setup() {
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.printf("Node %d starting as %s\n", NODE_ID, IS_TRANSMITTER ? "TRANSMITTER" : "RECEIVER");
  setupRadio();
  lastSendMs = millis();
}

void loop() {
  if (IS_TRANSMITTER) {
    unsigned long now = millis();
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
      lastSendMs = now;
      seq++;

      // Build message
      char msg[32];
      int n = snprintf(msg, sizeof(msg), "Node %d seq:%lu t:%lu", NODE_ID, seq, now / 1000UL);
      if (n < 0) n = 0;
      if (n >= (int)sizeof(msg)) msg[sizeof(msg)-1] = '\0';

      // Send
      radio.stopListening();
      bool ok = radio.write(msg, sizeof(msg)); // send fixed 32 bytes (receiver will get fixed size)
      if (ok) {
        Serial.printf("📤 Sent: \"%s\"\n", msg);
      } else {
        Serial.println("❌ Send failed");
      }
      radio.startListening(); // optional: return to listening (keeps both ends symmetric)
    }

    // Optionally check for incoming messages while transmitter waits
    if (radio.available()) {
      char buf[33] = {0};
      radio.read(buf, 32);
      buf[32] = '\0';
      Serial.printf("📥 (while TX) Received: %s\n", buf);
    }
  } else {
    // RECEIVER: listen continuously
    if (radio.available()) {
      char buf[33] = {0};
      radio.read(buf, 32);
      buf[32] = '\0'; // ensure null-terminated
      Serial.printf("📩 Received: %s\n", buf);
    }
  }

  delay(10); // small housekeeping delay
}
