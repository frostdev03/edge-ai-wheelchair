#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "driver/i2s.h"
#include "esp_wifi.h"

/* ================== USER CONFIG ================== */

// GANTI DENGAN MAC ESP32-S3 TUJUAN 30:ED:A0:BD:6A:44
uint8_t peerAddress[] = { 0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44 };

// I2S config
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define I2S_BCK_PIN 4
#define I2S_WS_PIN 5
#define I2S_DATA_PIN 6

// Audio
#define AUDIO_CHUNK_SIZE 128  // samples per packet

// Voice threshold (tuning required)
#define RMS_THRESHOLD 600  // ≈ suara manusia dekat mic

/* ================================================ */

int16_t audioBuffer[AUDIO_CHUNK_SIZE];

/* ================== ESP-NOW ================== */

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {

  // optional debug
  Serial.print("[ESP-NOW] Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}


/* ================== RMS ================== */

uint32_t calculateRMS(int16_t *buf, size_t len) {
  uint64_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += (int32_t)buf[i] * buf[i];
  }
  return sqrt((double)sum / len);
}

/* ================== I2S ================== */

void i2sInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,  //8
    .dma_buf_len = 256,  //AUDIO_CHUNK_SIZE
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = -1,
    .data_in_num = I2S_DATA_PIN
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

/* ================== SETUP ================== */

void setup() {
  Serial.begin(115200);

  // WiFi for ESP-NOW
  WiFi.mode(WIFI_STA);
  // WiFi.disconnect();
  WiFi.disconnect(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 1;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  i2sInit();
  Serial.println("ESP32-C3 Audio Streamer Ready");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

/* ================== LOOP ================== */

void loop() {
  size_t bytesRead = 0;

  i2s_read(
    I2S_PORT,
    audioBuffer,
    sizeof(audioBuffer),
    &bytesRead,
    portMAX_DELAY);

  // if (bytesRead == 0) return;

  // uint32_t rms = calculateRMS(audioBuffer, AUDIO_CHUNK_SIZE);

  // // Voice Activity Detection
  // if (rms > RMS_THRESHOLD) {
  //     esp_now_send(peerAddress,
  //                   (uint8_t*)audioBuffer,
  //                   bytesRead);
  // }

  if (bytesRead != sizeof(audioBuffer)) return;

  uint32_t rms = calculateRMS(audioBuffer, AUDIO_CHUNK_SIZE);

  if (rms > RMS_THRESHOLD) {
    esp_err_t err = esp_now_send(
      peerAddress,
      (uint8_t *)audioBuffer,
      sizeof(audioBuffer));

    if (err != ESP_OK) {
      Serial.printf("ESP-NOW send error: %d\n", err);
    }
  }
  
  delay(1000);
}
