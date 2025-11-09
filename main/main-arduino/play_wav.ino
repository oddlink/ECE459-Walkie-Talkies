/*
  ESP32 WAV Player (Mono TX)
  Reads a 16-bit PCM .wav file from SD card and plays through mono I²S DAC.
  Pins:
    BCLK  = 26
    LRCLK = 25
    SDOUT = 22
  SD (SPI):
    CS=5, SCK=18, MISO=19, MOSI=23
*/

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"

#define I2S_WS        25
#define I2S_SCK       26
#define I2S_SDOUT_PIN 32
#define SD_CS_PIN      5

const char* WAV_PATH = "/recording.wav";
static const i2s_port_t I2S_PORT = I2S_NUM_0;

struct WavInfo {
  uint16_t fmt, channels, bits;
  uint32_t rate, dataOffset, dataSize;
};

uint32_t rd_u32(File &f){uint8_t b[4];f.read(b,4);return b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);}
uint16_t rd_u16(File &f){uint8_t b[2];f.read(b,2);return b[0]|(b[1]<<8);}

bool parseWav(File &f, WavInfo &w){
  f.seek(0);
  char id[4];f.read((uint8_t*)id,4);if(strncmp(id,"RIFF",4))return false;
  rd_u32(f);f.read((uint8_t*)id,4);if(strncmp(id,"WAVE",4))return false;
  bool gotFmt=false,gotData=false;
  while(f.available()){
    f.read((uint8_t*)id,4);uint32_t sz=rd_u32(f);
    if(!strncmp(id,"fmt ",4)){
      w.fmt=rd_u16(f);w.channels=rd_u16(f);w.rate=rd_u32(f);
      rd_u32(f);rd_u16(f);w.bits=rd_u16(f);
      if(sz>16)f.seek(f.position()+sz-16);
      gotFmt=true;
    }else if(!strncmp(id,"data",4)){
      w.dataOffset=f.position();w.dataSize=sz;gotData=true;break;
    }else f.seek(f.position()+sz);
  }
  return gotFmt&&gotData&&w.fmt==1&&w.bits==16;
}

bool i2s_setup_tx(uint32_t rate){
  i2s_driver_uninstall(I2S_PORT);
  i2s_config_t cfg={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX),
    .sample_rate=(int)rate,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,      // mono TX
    .communication_format=(i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags=0,
    .dma_buf_count=8,
    .dma_buf_len=256,
    .use_apll=true,
    .tx_desc_auto_clear=true,
    .fixed_mclk=0
  };
  i2s_pin_config_t pins={I2S_SCK,I2S_WS,I2S_SDOUT_PIN,I2S_PIN_NO_CHANGE};
  if(i2s_driver_install(I2S_PORT,&cfg,0,NULL)!=ESP_OK)return false;
  if(i2s_set_pin(I2S_PORT,&pins)!=ESP_OK)return false;
  if(i2s_set_clk(I2S_PORT,rate,I2S_BITS_PER_SAMPLE_16BIT,I2S_CHANNEL_MONO)!=ESP_OK)return false;
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

void setup(){
  Serial.begin(115200);delay(200);
  Serial.println("\n=== Mono WAV Player ===");

  SPI.begin(18,19,23,SD_CS_PIN);
  if(!SD.begin(SD_CS_PIN)){Serial.println("SD failed");while(1)delay(1000);}
  File f=SD.open(WAV_PATH,FILE_READ);
  if(!f){Serial.println("open fail");while(1)delay(1000);}
  WavInfo w{};
  if(!parseWav(f,w)){Serial.println("bad wav");f.close();while(1)delay(1000);}
  Serial.printf("WAV: %u Hz, %u ch, %u-bit, data %u bytes\n",
                w.rate,w.channels,w.bits,w.dataSize);
  if(!i2s_setup_tx(w.rate)){Serial.println("i2s fail");while(1)delay(1000);}
  f.seek(w.dataOffset);

  const size_t N=1024;
  int16_t pcm[N*2]; int32_t i2sBuf[N];
  uint32_t remain=w.dataSize;
  Serial.println("Playing...");
  while(remain>0){
    size_t bytesToRead=min((size_t)remain,(size_t)(N*w.channels*sizeof(int16_t)));
    size_t got=f.read((uint8_t*)pcm,bytesToRead);
    if(!got)break;
    size_t frames=got/ (w.channels*sizeof(int16_t));
    for(size_t i=0;i<frames;i++){
      int16_t s;
      if(w.channels==1) s=pcm[i];
      else{ // simple L+R-->mono average
        int32_t mix=pcm[2*i]+pcm[2*i+1];
        s=(int16_t)(mix/2);
      }
      i2sBuf[i]=((int32_t)s)<<16; // left-align 16→32
    }
    size_t bytesOut=frames*sizeof(int32_t);
    size_t bw=0;
    i2s_write(I2S_PORT,(const char*)i2sBuf,bytesOut,&bw,portMAX_DELAY);
    remain-=got;
  }
  f.close();
  Serial.println("Done.");
}

void loop(){delay(1000);}
