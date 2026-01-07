#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"
/* ================= CONFIG ================= */

#define AUDIO_CHUNK_MAX 256  // samples (int16)
#define QUEUE_LENGTH 8

/* ========================================== */

// GANTI dengan MAC ESP32-C3 20:6E:F1:67:10:F4
uint8_t senderAddress[] = { 0x20, 0x6E, 0xF1, 0x67, 0x10, 0xF4 };

// Struktur payload (sementara: raw audio saja)
typedef struct {
  int16_t samples[AUDIO_CHUNK_MAX];
  size_t length;  // bytes
} audio_packet_t;

// Queue handle
QueueHandle_t audioQueue;

/* ================= ESP-NOW RX CALLBACK ================= */

void onDataRecv(const uint8_t *mac_addr,
                const uint8_t *data,
                int len) {
  Serial.print("RX from ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" | len=");
  Serial.println(len);

  Serial.print("Data: ");
  for (int i = 0; i < len; i++) {
    Serial.write(data[i]);
  }
  Serial.println();
}


/* ================= RTOS TASK ================= */

void audioRxTask(void *param) {
  audio_packet_t packet;

  while (true) {
    if (xQueueReceive(audioQueue, &packet, portMAX_DELAY) == pdTRUE) {

      // Debug minimal
      Serial.print("[RX] Audio packet | Bytes: ");
      Serial.print(packet.length);

      Serial.print(" | First sample: ");
      Serial.println(packet.samples[0]);
    }
  }
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32-S3 ESP-NOW Receiver (RTOS)");

  // WiFi in STA mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register RX callback
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderAddress, 6);
  peerInfo.channel = 1;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP32-C3 as peer");
    return;
  }


  // Create queue
  audioQueue = xQueueCreate(QUEUE_LENGTH, sizeof(audio_packet_t));
  if (audioQueue == NULL) {
    Serial.println("Failed to create queue");
    return;
  }

  // Create RTOS task
  xTaskCreatePinnedToCore(
    audioRxTask,
    "AudioRX",
    4096,
    NULL,
    5,  // priority (menengah)
    NULL,
    1  // core 1 (S3 dual-core)
  );

  Serial.println("Receiver ready, waiting for audio...");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

/* ================= LOOP ================= */

void loop() {
  // kosongkan
  vTaskDelay(portMAX_DELAY);
}
