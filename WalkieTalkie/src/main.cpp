// #include <WiFi.h>
// #include <esp_now.h>
// #include "I2SManager.h"
// #include "AudioHandler.h"
// #include <SPI.h>
// #include "esp_system.h"
// #include <esp_wifi.h>
// #include <nRF24L01.h>
// #include <RF24.h>

// // μ-law constants
// #define MU_LAW_MAX 0x1FFF
// #define MU_LAW_BIAS 33
// #define MU_LAW_COMPAND_PARAM 255.0f 

// // I2S Pins and config
// #define I2S_WS      25  // LR clock (WS)
// #define I2S_SCK     26  // BCLK
// #define I2S_SD      32  // Data in from INMP441
// #define I2S_SD_OUT_PIN   22   // Data out to DAC
// #define SAMPLE_RATE 16000

// // Global objects for I2S and audio handling
// I2SManager i2sManager(I2S_NUM_0, SAMPLE_RATE, I2S_WS, I2S_SCK, I2S_SD, I2S_SD_OUT_PIN);
// AudioHandler audioHandler(i2sManager);

// #define BUTTON      21
// #define RED_LED     15 //receiving
// #define GREEN_LED   2  //wifi transmitting
// #define YELLOW_LED  13 //rf transmitting

// #define CE_PIN      4
// #define CSN_PIN     5
// RF24 radio(CE_PIN, CSN_PIN);
// const byte address[5] = {'W', 'A', 'L', 'K', 'I'};

// #define WIFI_CHANNEL     6
// bool broadcast_mode = true;
// uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
// uint8_t Mac1[] = {0xCC,0xDB,0xA7,0x96,0xFD,0x64};
// uint8_t Mac2[] = {0xCC,0xDB,0xA7,0x9E,0x8A,0xB4};
// // uint8_t Mac3[] = {0x88,0x57,0x21,0x8E,0xE0,0x4C};

// const int GAIN = 1;

// // state
// bool ButtonPressed = false;
// volatile unsigned long lastISRTime = 0;
// const unsigned long debounceDelay = 20;
// bool sending = false;
// bool prevButtonState = true;
// volatile bool ignoreButton = false;   
// unsigned long lastRecvMillis = 0;     // updated by OnDataRecv when audio arrives
// const unsigned long RECEIVE_TIMEOUT_MS = 600; // time after last packet to re-enable button
// bool receivingActive = false;  

// volatile bool acknowledged = false;


// void IRAM_ATTR onButtonPress() {
//   // If actively receiving, ignore button presses, do nothing
//   if (ignoreButton) return;

//   // unsigned long now = (unsigned long) (esp_timer_get_time() / 1000);
//   // if (now - lastISRTime > debounceDelay) {
//     // ButtonPressed = !ButtonPressed;
//     ButtonPressed = digitalRead(BUTTON);
//   //   lastISRTime = now;
//   // }
  
// }

// void radioSetup(){
//   // --- RADIO INIT ---
//   if (!radio.begin()) {
//     Serial.println("Radio not responding!");
//     while (1);
//   }
//   radio.openWritingPipe(address);
//   radio.openReadingPipe(1, address);
//   radio.setPALevel(RF24_PA_LOW);
//   radio.setDataRate(RF24_1MBPS);
//   radio.startListening();
// }

// void setPeers () {
//   esp_now_peer_info_t peerInfo = {};
//   memcpy(peerInfo.peer_addr, BroadcastMac, 6);
//   peerInfo.channel = 0;
//   peerInfo.encrypt = false;
//   if (esp_now_add_peer(&peerInfo) != ESP_OK) {
//     Serial.print("Failed to add peer");
//   }

//   // esp_now_peer_info_t peerInfo1 = {};
//   // memcpy(peerInfo1.peer_addr, Mac1, 6);
//   // peerInfo1.channel = 0;
//   // peerInfo1.encrypt = false;
//   // if (esp_now_add_peer(&peerInfo1) != ESP_OK) {
//   //   Serial.print("Failed to add peer 1");
//   // }

//   // esp_now_peer_info_t peerInfo2 = {};
//   // memcpy(peerInfo2.peer_addr, Mac2, 6);
//   // peerInfo2.channel = 0;
//   // peerInfo2.encrypt = false;
//   // if (esp_now_add_peer(&peerInfo2) != ESP_OK) {
//   //   Serial.println("Failed to add peer 2");
//   // }

//   // esp_now_peer_info_t peerInfo3 = {};
//   // memcpy(peerInfo3.peer_addr, Mac3, 6);
//   // peerInfo3.channel = 0;
//   // peerInfo3.encrypt = false;
//   // if (esp_now_add_peer(&peerInfo3) != ESP_OK) {
//   //   Serial.println("Failed to add peer 2");
//   // }
// }

// void setChannel(uint8_t ch) {
//   esp_wifi_set_promiscuous(true);
//   esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
//   esp_wifi_set_promiscuous(false);
// }

// void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
//   Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
// }

// void OnDataRecv(const uint8_t *info, const uint8_t *data, int len) {
//   // Expecting incoming buffer of int16_t samples
//   if (len <= 0) return;

//   // Serial.println(String((char*)data));
//   if((len == 12 && String((char*)data).equals("ANYONE HOME"))){
//     Serial.println("Received the correct ping. Sending back ACK");
//     esp_now_send(BroadcastMac, Mac1, 6);
//     return;
//   }

//   // Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
//   //             Mac1[0], Mac1[1], Mac1[2], Mac1[3], Mac1[4], Mac1[5]);
//   // Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
//   //             data[0], data[1], data[2], data[3], data[4], data[5]);
//   if(len == 6 && memcmp(data, Mac2, 6) == 0){
//     Serial.println("Received mac acknowledgement");
//     acknowledged = true;
//     return;
//   }

//   // Mark that we are actively receiving (prevent button toggles)
//   receivingActive = true;
//   ignoreButton = true;
//   digitalWrite(RED_LED, HIGH);
//   lastRecvMillis = millis();

//   // Don't play back while we're in sending state (still keep this guard)
//   if (sending) return;

//   // μ-law decode from 8-bit to 16-bit
//   int sampleCount = len;
//   int16_t inSamples[sampleCount];
//   audioHandler.decodeMuLaw(data, sampleCount, inSamples);

//   // Play audio using AudioHandler
//   audioHandler.playAudio(inSamples, sampleCount);

//   lastRecvMillis = millis();
// }

// void setup() {
//   Serial.begin(115200);

//   // Initialize I2S
//   i2sManager.init();
//   Serial.println("I2S Initialized");

//   pinMode(BUTTON, INPUT_PULLDOWN);
//   attachInterrupt(digitalPinToInterrupt(BUTTON), onButtonPress, CHANGE);

//   pinMode(RED_LED, OUTPUT);
//   digitalWrite(RED_LED, 0);
//   pinMode(GREEN_LED, OUTPUT);
//   digitalWrite(GREEN_LED, 0);
//   pinMode(YELLOW_LED, OUTPUT);
//   digitalWrite(YELLOW_LED, 0);

//   WiFi.mode(WIFI_STA);

//   setChannel(WIFI_CHANNEL);
//   if (esp_now_init() != ESP_OK) {
//     Serial.println("ESP-NOW Init Failed");
//     return;
//   }
//   setPeers();
//   esp_now_register_send_cb(OnDataSent);
//   esp_now_register_recv_cb(OnDataRecv);
//   radioSetup();
//   // radio.powerDown();//shut down radio to save power

//   Serial.println("Walkie Talkie (single I2S full-duplex) Set Up");
// }

// void loop() {
//   // detect edge change and switch sending/receiving state
//   if (ignoreButton && (millis() - lastRecvMillis > RECEIVE_TIMEOUT_MS)) {
//     ignoreButton = false;
//     receivingActive = false;
//     digitalWrite(RED_LED, LOW);
//     // optional: give user feedback that button is enabled again
//     Serial.println("Receive idle — button re-enabled");
//   }

//   // existing edge-detection logic for ButtonPressed -> sending/receiving
//   if (ButtonPressed != prevButtonState) {
//     prevButtonState = ButtonPressed;
//     sending = ButtonPressed;
//     // digitalWrite(GREEN_LED, ButtonPressed);

//     if (sending) {
//       acknowledged = false;
//       unsigned long startTime = millis();
//       const unsigned long timeout = 3000; // 3 seconds
//       // stop rx callback while sending (optional)
//       esp_now_register_recv_cb(NULL);
//       Serial.println("Switching to SENDING (mic -> network)");

//       Serial.println("Pinging out to receivers");
//       uint8_t ping_message[12] = "ANYONE HOME";
//       esp_now_send(BroadcastMac, ping_message, 12);

//       while (!acknowledged && millis() - startTime < timeout && ButtonPressed) { //waits a max of 3 seconds for an acknowledgement to come through
//         delay(10); 
//       }
//       if(acknowledged && ButtonPressed){
//         Serial.println("Ping acknowledged with mac address");
//         digitalWrite(GREEN_LED, ButtonPressed);
//       }
//       else if(!digitalRead(BUTTON)){ //catches if button is released before wifi ack comes in
//         prevButtonState = 0;
//         sending = 0;
//       }
//       else{
//         Serial.println("Not acknowledged, try using RF");
//       }

//     } else {
//       // restore rx callback for playback
//       digitalWrite(GREEN_LED, ButtonPressed);
//       digitalWrite(YELLOW_LED, ButtonPressed);
//       esp_now_register_recv_cb(OnDataRecv);
//       radio.startListening(); //TO-DO change later so that the radio is not always on, this is a bad power move
//       Serial.println("Switching to RECEIVING (network -> speaker)");
//     }
//   }

//   if (sending && acknowledged) { //if acknowledged use the wifi route
//     // Capture from I2S RX, convert to int16_t network samples, send via ESP-NOW
//     const int SAMPLES = 100; // number of frames to read (frames are stereo 32-bit words)
//     // we'll read SAMPLES * 1 (frame per sample) but we only care about one channel (mic is left)
//     int32_t inBuf[SAMPLES]; // read 32-bit frames (we configured stereo but mic likely uses left only)
//     size_t bytes_read = 0;
//     // read SAMPLES frames of 32-bit each (mono word per frame because channel_format is stereo but mic provides data in left)
//     i2sManager.read(inBuf, SAMPLES);

//     // Convert frames to int16_t samples and apply gain
//     int16_t outSamples[SAMPLES];
//     for (int i = 0; i < SAMPLES; ++i) {
//       int16_t s16 = (int16_t)(inBuf[i] >> 14); // Extract 16-bit sample
//       int32_t amplified = (int32_t)s16 * GAIN;
//       if (amplified > 32767) amplified = 32767;
//       if (amplified < -32768) amplified = -32768;
//       outSamples[i] = (int16_t)amplified;
//     }

//     // Encode to μ-law
//     uint8_t encoded[SAMPLES];
//     audioHandler.encodeMuLaw(outSamples, SAMPLES, encoded);

//     // Send encoded data via ESP-NOW

//     esp_now_send(BroadcastMac, encoded, SAMPLES);
      

      
//       // esp_now_send(BroadcastMac, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
//       // esp_now_send(Mac1, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
//       // esp_now_send(Mac2, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
//     }
//     else if (sending && !acknowledged){
//       // radio.powerUp();
//       const int FRAMES = 32; // small chunk for RF packet
//       int32_t inBuf[FRAMES];
//       size_t bytesRead = 0;

//       // read audio from i2s
//       i2sManager.read(inBuf, FRAMES);

//       // Encode to μ-law
//       int16_t outSamples[FRAMES];
//       for (int i = 0; i < FRAMES; i++) {
//           int16_t s16 = (int16_t)(inBuf[i] >> 16); // Extract 16-bit sample
//           int32_t amplified = (int32_t)s16 * GAIN;
//           if (amplified > 32767) amplified = 32767;
//           if (amplified < -32768) amplified = -32768;
//           outSamples[i] = (int16_t)amplified; // Store amplified sample
//       }

//       // Use AudioHandler to encode the entire buffer
//       uint8_t encoded[FRAMES];
//       audioHandler.encodeMuLaw(outSamples, FRAMES, encoded);
      
//       // Send via RF24
//       radio.write(&encoded, FRAMES);
//     }
//     delay(2);
//    // end sending

//    if (!sending && radio.available()) {
//     digitalWrite(RED_LED, HIGH);
//     uint8_t encoded[32];
//     radio.read(&encoded, sizeof(encoded));

//     // decode μ-law and play audio
//     int16_t decoded[32];
//     audioHandler.decodeMuLaw(encoded, 32, decoded);
//     audioHandler.playAudio(decoded, 32);
//   }
//   else if(!sending && !radio.available()){
//     digitalWrite(RED_LED, LOW);
//   }
//   }

//  ---------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>
#include <SPI.h>
#include "esp_system.h"
#include <esp_wifi.h>
#include <nRF24L01.h>
#include <RF24.h>

// μ-law constants
#define MU_LAW_MAX 0x1FFF
#define MU_LAW_BIAS 33
#define MU_LAW_COMPAND_PARAM 255.0f 

#define I2S_WS      25  // LR clock (WS)
#define I2S_SCK     26  // BCLK
#define I2S_SD      32  // Data in from INMP441
#define I2S_SD_OUT_PIN   22   // Data out to DAC

#define BUTTON      21
#define RED_LED     15 //receiving
#define GREEN_LED   2  //wifi transmitting
#define YELLOW_LED  13 //rf transmitting

#define CE_PIN      4
#define CSN_PIN     5
RF24 radio(CE_PIN, CSN_PIN);
const byte address[5] = {'W', 'A', 'L', 'K', 'I'};

#define WIFI_CHANNEL     6
bool broadcast_mode = true;
uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t Mac1[] = {0xCC,0xDB,0xA7,0x96,0xFD,0x64};
uint8_t Mac2[] = {0xCC,0xDB,0xA7,0x9E,0x8A,0xB4};
// uint8_t Mac3[] = {0x88,0x57,0x21,0x8E,0xE0,0x4C};

#define SAMPLE_RATE 16000
const int GAIN = 1;

const i2s_port_t I2S_PORT = I2S_NUM_0; // single I2S port used for both RX and TX

// state
bool ButtonPressed = false;
volatile unsigned long lastISRTime = 0;
const unsigned long debounceDelay = 20;
bool sending = false;
bool prevButtonState = true;
volatile bool ignoreButton = false;   
unsigned long lastRecvMillis = 0;     // updated by OnDataRecv when audio arrives
const unsigned long RECEIVE_TIMEOUT_MS = 600; // time after last packet to re-enable button
bool receivingActive = false;  

volatile bool acknowledged = false;


void IRAM_ATTR onButtonPress() {
  // If actively receiving, ignore button presses, do nothing
  if (ignoreButton) return;

  // unsigned long now = (unsigned long) (esp_timer_get_time() / 1000);
  // if (now - lastISRTime > debounceDelay) {
    // ButtonPressed = !ButtonPressed;
    ButtonPressed = digitalRead(BUTTON);
  //   lastISRTime = now;
  // }
  
}

uint8_t linearToMulaw(int16_t sample) {
  const float MU = 255.0f;
  float x = (float)sample / 32768.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float companded = sign * (log(1.0f + MU * x) / log(1.0f + MU));
  int8_t encoded = (int8_t)(companded * 127.0f);
  return (uint8_t)encoded;
}

int16_t mulawToLinear(uint8_t muSample) {
  const float MU = 255.0f;
  float x = (float)((int8_t)muSample) / 127.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float linear = sign * ((pow(1.0f + MU, x) - 1.0f) / MU);
  return (int16_t)(linear * 32767.0f);
}

void radioSetup(){
  // --- RADIO INIT ---
  if (!radio.begin()) {
    Serial.println("Radio not responding!");
    while (1);
  }
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.startListening();
}

void setPeers () {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BroadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.print("Failed to add peer");
  }

  // esp_now_peer_info_t peerInfo1 = {};
  // memcpy(peerInfo1.peer_addr, Mac1, 6);
  // peerInfo1.channel = 0;
  // peerInfo1.encrypt = false;
  // if (esp_now_add_peer(&peerInfo1) != ESP_OK) {
  //   Serial.print("Failed to add peer 1");
  // }

  // esp_now_peer_info_t peerInfo2 = {};
  // memcpy(peerInfo2.peer_addr, Mac2, 6);
  // peerInfo2.channel = 0;
  // peerInfo2.encrypt = false;
  // if (esp_now_add_peer(&peerInfo2) != ESP_OK) {
  //   Serial.println("Failed to add peer 2");
  // }

  // esp_now_peer_info_t peerInfo3 = {};
  // memcpy(peerInfo3.peer_addr, Mac3, 6);
  // peerInfo3.channel = 0;
  // peerInfo3.encrypt = false;
  // if (esp_now_add_peer(&peerInfo3) != ESP_OK) {
  //   Serial.println("Failed to add peer 2");
  // }
}

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t *info, const uint8_t *data, int len) {
  // Expecting incoming buffer of int16_t samples
  if (len <= 0) return;

  // Serial.println(String((char*)data));
  if((len == 12 && String((char*)data).equals("ANYONE HOME"))){
    Serial.println("Received the correct ping. Sending back ACK");
    esp_now_send(BroadcastMac, Mac1, 6);
    return;
  }

  // Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
  //             Mac1[0], Mac1[1], Mac1[2], Mac1[3], Mac1[4], Mac1[5]);
  // Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
  //             data[0], data[1], data[2], data[3], data[4], data[5]);
  if(len == 6 && memcmp(data, Mac2, 6) == 0){
    Serial.println("Received mac acknowledgement");
    acknowledged = true;
    return;
  }

  // Mark that we are actively receiving (prevent button toggles)
  receivingActive = true;
  ignoreButton = true;
  digitalWrite(RED_LED, HIGH);
  lastRecvMillis = millis();

  // Don't play back while we're in sending state (still keep this guard)
  if (sending) return;

  // μ-law decode from 8-bit to 16-bit
  int sampleCount = len;
  int16_t inSamples[sampleCount];
  for (int i = 0; i < sampleCount; ++i) {
    inSamples[i] = mulawToLinear(((uint8_t*)data)[i]);
  }


  // Prepare output buffer: each I2S "frame" is a 32-bit sample per channel, we will create stereo pairs
  // so outSamples length = sampleCount * 2 (left+right) of int32_t
  // For memory safety, chunk the write if sampleCount is large.
  const int CHUNK = 128;
  int idx = 0;
  while (idx < sampleCount) {
    int chunkSamples = min(CHUNK, sampleCount - idx);
    int32_t outBuf[CHUNK * 2]; // stereo
    for (int i = 0; i < chunkSamples; ++i) {
      int16_t s = inSamples[idx + i];
      // Convert 16-bit to 32-bit left-aligned (MSB) for the DAC:
      // place 16-bit sample into high 16 bits of 32-bit word (common approach)
      int32_t s32 = ((int32_t)s) << 16;
      // duplicate to stereo
      outBuf[2 * i + 0] = s32;
      outBuf[2 * i + 1] = s32;
    }
    size_t bytes_written = 0;
    i2s_write(I2S_PORT, outBuf, chunkSamples * 2 * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    idx += chunkSamples;

    lastRecvMillis = millis();
  }
}


// Setup single I2S driver in full-duplex mode
bool i2sInstalled = false;
void setupI2SFullDuplex() {
  if (i2sInstalled) return;

  // uninstall any previously installed driver on this port (safe)
  i2s_driver_uninstall(I2S_PORT);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // use 32-bit frames for flexibility
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // stereo frames
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false,//true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = I2S_SD
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing I2S driver: %d\n", err);
    return;
  }
  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("Failed setting I2S pins: %d\n", err);
    return;
  }
  // set clock explicitly (sample rate, bits, channels)
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
  i2s_zero_dma_buffer(I2S_PORT);

  i2sInstalled = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON), onButtonPress, CHANGE);

  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, 0);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, 0);
  pinMode(YELLOW_LED, OUTPUT);
  digitalWrite(YELLOW_LED, 0);

  WiFi.mode(WIFI_STA);
  setupI2SFullDuplex();

  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  setPeers();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  radioSetup();
  // radio.powerDown();//shut down radio to save power

  Serial.println("Walkie Talkie (single I2S full-duplex) Set Up");
}

void loop() {
  // detect edge change and switch sending/receiving state
  if (ignoreButton && (millis() - lastRecvMillis > RECEIVE_TIMEOUT_MS)) {
    ignoreButton = false;
    receivingActive = false;
    digitalWrite(RED_LED, LOW);
    // optional: give user feedback that button is enabled again
    Serial.println("Receive idle — button re-enabled");
  }

  // existing edge-detection logic for ButtonPressed -> sending/receiving
  if (ButtonPressed != prevButtonState) {
    prevButtonState = ButtonPressed;
    sending = ButtonPressed;
    // digitalWrite(GREEN_LED, ButtonPressed);

    if (sending) {
      acknowledged = false;
      unsigned long startTime = millis();
      const unsigned long timeout = 3000; // 3 seconds
      // stop rx callback while sending (optional)
      esp_now_register_recv_cb(NULL);
      Serial.println("Switching to SENDING (mic -> network)");

      Serial.println("Pinging out to receivers");
      uint8_t ping_message[12] = "ANYONE HOME";
      esp_now_send(BroadcastMac, ping_message, 12);

      while (!acknowledged && millis() - startTime < timeout && ButtonPressed) { //waits a max of 3 seconds for an acknowledgement to come through
        delay(10); 
      }
      if(acknowledged && ButtonPressed){
        Serial.println("Ping acknowledged with mac address");
        digitalWrite(GREEN_LED, ButtonPressed);
      }
      else if(!digitalRead(BUTTON)){ //catches if button is released before wifi ack comes in
        prevButtonState = 0;
        sending = 0;
      }
      else{
        Serial.println("Not acknowledged, try using RF");
      }

    } else {
      // restore rx callback for playback
      digitalWrite(GREEN_LED, ButtonPressed);
      digitalWrite(YELLOW_LED, ButtonPressed);
      esp_now_register_recv_cb(OnDataRecv);
      radio.startListening(); //TO-DO change later so that the radio is not always on, this is a bad power move
      Serial.println("Switching to RECEIVING (network -> speaker)");
    }
  }

  if (sending && acknowledged) { //if acknowledged use the wifi route
    // Capture from I2S RX, convert to int16_t network samples, send via ESP-NOW
    const int SAMPLES = 100; // number of frames to read (frames are stereo 32-bit words)
    // we'll read SAMPLES * 1 (frame per sample) but we only care about one channel (mic is left)
    int32_t inBuf[SAMPLES]; // read 32-bit frames (we configured stereo but mic likely uses left only)
    size_t bytes_read = 0;
    // read SAMPLES frames of 32-bit each (mono word per frame because channel_format is stereo but mic provides data in left)
    esp_err_t r = i2s_read(I2S_PORT, inBuf, sizeof(inBuf), &bytes_read, portMAX_DELAY);
    if (r == ESP_OK && bytes_read > 0) {
      int framesRead = bytes_read / sizeof(int32_t); // number of 32-bit frames read
      // convert frames to int16_t samples for sending
      int16_t outSamples[framesRead];
      for (int i = 0; i < framesRead; ++i) {
        int32_t w = inBuf[i];
        // Extract a 16-bit sample from the 32-bit word.
        // Common INMP441 alignment: 32-bit word left-aligned with 24-bit data in high bits.
        // Here we assume high 16 bits contain the meaningful audio; shift right 16 to get 16-bit sample.
        // If your mic alignment differs, change this shift.
        int16_t s16 = (int16_t)(w >> 14);

        // apply gain safelyb
        int32_t amplified = (int32_t)s16 * GAIN;
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        outSamples[i] = (int16_t)amplified;
      }
      // send the data over ESP-NOW
      if (r == ESP_OK && bytes_read > 0) {
        int framesRead = bytes_read / sizeof(int32_t);
        int16_t outSamples[framesRead];

        // Convert and apply gain
        for (int i = 0; i < framesRead; ++i) {
          int32_t w = inBuf[i];
          int16_t s16 = (int16_t)(w >> 14);
          int32_t amplified = (int32_t)s16 * GAIN;
          if (amplified > 32767) amplified = 32767;
          if (amplified < -32768) amplified = -32768;
          outSamples[i] = (int16_t)amplified;
        }

        // μ-law encode
        uint8_t encoded[framesRead];
        for (int i = 0; i < framesRead; ++i) {
          encoded[i] = linearToMulaw(outSamples[i]);
        }

        // Send 8-bit compressed samples
        esp_now_send(BroadcastMac, encoded, framesRead);
      }

      
      // esp_now_send(BroadcastMac, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
      // esp_now_send(Mac1, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
      // esp_now_send(Mac2, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
    }
    delay(2);
  } // end sending
  else if(sending && !acknowledged){ 
    // radio.powerUp();
    digitalWrite(YELLOW_LED, ButtonPressed);
    radio.stopListening();
    
    const int FRAMES = 32; // small chunk for RF packet
    int32_t inBuf[FRAMES];
    size_t bytesRead = 0;
    esp_err_t r = i2s_read(I2S_NUM_0, inBuf, sizeof(inBuf), &bytesRead, portMAX_DELAY);
    if (r == ESP_OK && bytesRead > 0) {
      int frames = bytesRead / sizeof(int32_t);
      uint8_t encoded[FRAMES];
      for (int i = 0; i < frames; i++) {
        int16_t s16 = (int16_t)(inBuf[i] >> 16);
        int32_t amplified = (int32_t)s16 * GAIN;
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        encoded[i] = linearToMulaw((int16_t)amplified);
      }
      radio.write(&encoded, frames);
    }
  }

   if (!sending && radio.available()) {
    digitalWrite(RED_LED, HIGH);
    uint8_t encoded[32];
    radio.read(&encoded, sizeof(encoded));

    int16_t decoded[32];
    for (int i = 0; i < 32; i++) {
      decoded[i] = mulawToLinear(encoded[i]);
    }

    int32_t outBuf[64];
    for (int i = 0; i < 32; i++) {
      int32_t s32 = ((int32_t)decoded[i]) << 16;
      outBuf[2 * i] = s32;
      outBuf[2 * i + 1] = s32;
    }
    size_t written;
    i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
  }
  else if(!sending && !radio.available()){
    digitalWrite(RED_LED, LOW);
  }
}
