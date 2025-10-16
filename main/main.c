#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(4, 5); // CE, CSN pins
const byte address[6] = "00001";

int nodeID = 0;  // Change this to 0 for one ESP32, 1 for the other
unsigned long lastSend = 0;
unsigned long interval = 2000; // send every 2 seconds

void setup() {
  Serial.begin(115200);
  radio.begin();
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_1MBPS);
  radio.startListening();

  Serial.print("Node ");
  Serial.print(nodeID);
  Serial.println(" started and listening...");
}

void loop() {
  // Check for incoming messages
  if (radio.available()) {
    char text[32] = "";
    radio.read(&text, sizeof(text));
    Serial.print("Received: ");
    Serial.println(text);
  }

  // Send a message every 2 seconds
  if (millis() - lastSend > interval) {
    lastSend = millis();
    radio.stopListening();  // stop listening so we can transmit

    char msg[32];
    snprintf(msg, sizeof(msg), "Hello from Node %d", nodeID);
    bool ok = radio.write(&msg, sizeof(msg));

    if (ok) Serial.print("Sent: ");
    else Serial.print("Send failed: ");
    Serial.println(msg);

    radio.startListening(); // return to listening mode
  }
}
