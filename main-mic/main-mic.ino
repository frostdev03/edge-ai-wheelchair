// #include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"
// #include "driver/i2s.h"

/* ================= CONFIG ================= */

#define ESPNOW_CHANNEL 1

// GANTI DENGAN MAC ESP32-S3 (SLAVE)
uint8_t peerAddress[] = { 0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44 };

/* ================= AUDIO (DISABLED) ================= */

// #define I2S_PORT        I2S_NUM_0
// #define SAMPLE_RATE     16000
// #define I2S_BCK_PIN     20
// #define I2S_WS_PIN      21
// #define I2S_DATA_PIN    0
// #define AUDIO_CHUNK_SIZE 120
// #define RMS_THRESHOLD    600

// typedef struct {
//   int16_t samples[AUDIO_CHUNK_SIZE];
//   uint16_t count;
// } audio_packet_t;

/* ================= SIMPLE PAYLOAD ================= */

const char *testMessage = "PING_C3";

/* ================= ESPNOW CALLBACK ================= */

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("✅ ESPNOW SEND OK");
  } else {
    Serial.println("❌ ESPNOW SEND FAIL");
  }
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("🔥 ESP32-C3 ESPNOW MASTER (STRING TEST)");

  /* --- WiFi --- */
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(100);
  // WiFi.disconnect(true, true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  delay(100);

  /* --- ESP-NOW --- */
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Failed to add peer");
    return;
  }

  Serial.print("📡 MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("✅ MASTER READY");
}

/* ================= LOOP ================= */

// void loop() {
//   esp_err_t err = esp_now_send(
//     peerAddress,
//     (uint8_t *)testMessage,
//     strlen(testMessage) + 1
//   );

//   if (err != ESP_OK) {
//     Serial.printf("❌ TX ERROR: %d\n", err);
//   }

//   delay(500); // santai, jangan barbar
// }

void loop() {
  static bool sent = false;

  if (!sent) {
    const char *msg = "PING";

    esp_err_t err = esp_now_send(
      peerAddress,
      (uint8_t *)msg,
      strlen(msg) + 1
    );

    Serial.printf("SEND RESULT: %d\n", err);
    sent = true;
  }

  delay(1000);
}

