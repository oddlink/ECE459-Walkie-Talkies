#define LED_PIN 2       // onboard LED
#define DAC_PIN 22      // output pin
#define ADC_PIN 34      // input pin (ADC1_CHANNEL_6)

void setup() {
  Serial.begin(115200);

  // Configure LED and DAC pins as output
  pinMode(LED_PIN, OUTPUT);
  pinMode(DAC_PIN, OUTPUT);

  // Initialize LED on
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(DAC_PIN, HIGH);
}

void loop() {
  // Read ADC value (0–4095)
  int val = analogRead(ADC_PIN);

  // Convert to voltage
  float voltage = (val / 4095.0) * 3.3;

  Serial.print("ADC Raw: ");
  Serial.print(val);
  Serial.print("\tVoltage: ");
  Serial.println(voltage, 5);

  static int ledState = HIGH;

  // If the signal rises above threshold, turn on LED and output
  if (ledState != HIGH && voltage > 1.70) {
    ledState = HIGH;
    digitalWrite(LED_PIN, ledState);
    digitalWrite(DAC_PIN, ledState);
  }
  // If signal drops below threshold, turn off LED and output
  else if (ledState != LOW && voltage < 1.55) {
    ledState = LOW;
    digitalWrite(LED_PIN, ledState);
    digitalWrite(DAC_PIN, ledState);
  }

  delay(50); // 50 ms sampling delay
}
