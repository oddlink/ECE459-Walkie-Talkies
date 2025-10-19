#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN     4
#define CSN_PIN    5
#define BUTTON_PIN 22     // button on D22 / GPIO22
#define LED_PIN    15     // LED on D15 / GPIO15

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";   // same address for both

// ---- interrupt bookkeeping ----
volatile bool buttonFlag = false;
unsigned long lastISRTime = 0;
const unsigned long debounceDelay = 200;

// ---- disable window ----
bool buttonDisabled = false;
unsigned long disableStart = 0;

// ---- LED timing ----
bool ledOn = false;
unsigned long ledOnTime = 0;
const unsigned long ledDuration = 2000;  // 2 seconds

// interrupt routine for button press
void IRAM_ATTR onButtonPress() {
  unsigned long now = millis();
  if (now - lastISRTime > debounceDelay) {
    buttonFlag = true;
    lastISRTime = now;
  }
}

void sendMessage(const char* msg) {
  radio.stopListening();
  bool ok = radio.write(msg, strlen(msg) + 1);
  Serial.printf("%s: %s\n", ok ? "Sent" : "Send failed", msg);
  radio.startListening();
}

void turnLedOn() {
  digitalWrite(LED_PIN, HIGH);
  ledOn = true;
  ledOnTime = millis();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  if (!radio.begin()) {
    Serial.println("Radio not responding!");
    while (1);
  }

  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.startListening();

  Serial.println("Node ready (bidirectional mode)");
}

void loop() {
  // re-enable button after 5 s
  if (buttonDisabled && millis() - disableStart > 5000) {
    buttonDisabled = false;
    Serial.println("Button re-enabled");
  }

  // auto turn off LED after 2 s
  if (ledOn && millis() - ledOnTime > ledDuration) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
    Serial.println("LED OFF (auto timeout)");
  }

  // check for received messages
  if (radio.available()) {
    char text[32] = "";
    radio.read(&text, sizeof(text));
    Serial.printf("Received: %s\n", text);

    if (strcmp(text, "PING") == 0) {
      // got ping, this means we should light LED, disable button, send ack
      Serial.println("Got PING → LED ON, disable button, send ACK");
      turnLedOn();
      buttonDisabled = true;
      disableStart = millis();
      sendMessage("ACK");
    }
    else if (strcmp(text, "ACK") == 0) {
      // got ack so we should light our LED
      Serial.println("Got ACK → LED ON");
      turnLedOn();
    }
  }

  // handle interrupt-triggered button press
  if (buttonFlag) {
    noInterrupts();
    bool trig = buttonFlag;
    buttonFlag = false;
    interrupts();
    // send a ping if we press the button
    if (trig && !buttonDisabled) {
      Serial.println("Button pressed → sending PING");
      sendMessage("PING");
    // if our button is disabled, don't send a ping
    } else if (trig && buttonDisabled) {
      Serial.println("Button press ignored (temporarily disabled)");
    }
  }
}
